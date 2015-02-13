/*
Highly Optimized Object-oriented Many-particle Dynamics -- Blue Edition
(HOOMD-blue) Open Source Software License Copyright 2009-2014 The Regents of
the University of Michigan All rights reserved.

HOOMD-blue may contain modifications ("Contributions") provided, and to which
copyright is held, by various Contributors who have granted The Regents of the
University of Michigan the right to modify and/or distribute such Contributions.

You may redistribute, use, and create derivate works of HOOMD-blue, in source
and binary forms, provided you abide by the following conditions:

* Redistributions of source code must retain the above copyright notice, this
list of conditions, and the following disclaimer both in the code and
prominently in any materials provided with the distribution.

* Redistributions in binary form must reproduce the above copyright notice, this
list of conditions, and the following disclaimer in the documentation and/or
other materials provided with the distribution.

* All publications and presentations based on HOOMD-blue, including any reports
or published results obtained, in whole or in part, with HOOMD-blue, will
acknowledge its use according to the terms posted at the time of submission on:
http://codeblue.umich.edu/hoomd-blue/citations.html

* Any electronic documents citing HOOMD-Blue will link to the HOOMD-Blue website:
http://codeblue.umich.edu/hoomd-blue/

* Apart from the above required attributions, neither the name of the copyright
holder nor the names of HOOMD-blue's contributors may be used to endorse or
promote products derived from this software without specific prior written
permission.

Disclaimer

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER AND CONTRIBUTORS ``AS IS'' AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, AND/OR ANY
WARRANTIES THAT THIS SOFTWARE IS FREE OF INFRINGEMENT ARE DISCLAIMED.

IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
// Maintainer: joaander

#include "ExecutionConfiguration.h"
#include "HOOMDVersion.h"

#ifdef WIN32
#pragma warning( push )
#pragma warning( disable : 4103 4244 )
#endif

#ifdef ENABLE_CUDA
#include <cuda_runtime.h>
#endif

#ifdef ENABLE_MPI
#include "HOOMDMPI.h"
#endif

#include <boost/python.hpp>
using namespace boost::python;

#include <boost/thread.hpp>

#include <stdexcept>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <algorithm>

using namespace std;
using namespace boost;

#ifdef ENABLE_CUDA
#include "CachedAllocator.h"
#endif

/*! \file ExecutionConfiguration.cc
    \brief Defines ExecutionConfiguration and related classes
*/

/*! \param mode Execution mode to set (cpu or gpu)
    \param gpu_id ID of the GPU on which to run, or -1 for automatic selection
    \param min_cpu If set to true, cudaDeviceBlockingSync is set to keep the CPU usage of HOOMD to a minimum
    \param ignore_display If set to true, try to ignore GPUs attached to the display
    \param _msg Messenger to use for status message printing
    \param n_ranks Number of ranks per partition

    Explicitly force the use of either CPU or GPU execution. If GPU exeuction is selected, then a default GPU choice
    is made by not calling cudaSetDevice.
*/
ExecutionConfiguration::ExecutionConfiguration(executionMode mode,
                                               int gpu_id,
                                               bool min_cpu,
                                               bool ignore_display,
                                               boost::shared_ptr<Messenger> _msg,
                                               unsigned int n_ranks)
    : m_cuda_error_checking(false), msg(_msg)
    {
    if (!msg)
        msg = boost::shared_ptr<Messenger>(new Messenger());

    msg->notice(5) << "Constructing ExecutionConfiguration: " << gpu_id << " " << min_cpu << " " << ignore_display << endl;
    exec_mode = mode;

    m_rank = 0;

#ifdef ENABLE_CUDA
    // scan the available GPUs
    scanGPUs(ignore_display);

    // auto select a mode
    if (exec_mode == AUTO)
        {
        // if there are available GPUs, initialize them. Otherwise, default to running on the CPU
        int dev_count = getNumCapableGPUs();

        if (dev_count > 0)
            {
            exec_mode = GPU;

            // if we are not running in compute exclusive mode, use
            // local MPI rank as preferred GPU id
            gpu_id = m_system_compute_exclusive ? -1 : guessLocalRank();
            }
        else
            exec_mode = CPU;
        }

    // now, exec_mode should be either CPU or GPU - proceed with initialization

    // initialize the GPU if that mode was requested
    if (exec_mode == GPU)
        {
        initializeGPU(gpu_id, min_cpu);
        }
#else
    if (exec_mode == GPU)
        {
        msg->error() << "GPU execution requested, but this hoomd was built without CUDA support" << endl;
        throw runtime_error("Error initializing execution configuration");
        }
    // "auto-select" the CPU
    exec_mode = CPU;
#endif

    #ifdef ENABLE_MPI
    m_n_rank = n_ranks;
    initializeMPI();
    #endif

    setupStats();

    #ifdef ENABLE_CUDA
    if (exec_mode == GPU)
        {
        // initialize cached allocator, max allocation 0.5*global mem
        m_cached_alloc = new CachedAllocator(this, (unsigned int)(0.5f*(float)dev_prop.totalGlobalMem));
        }
    #endif

    #ifdef ENABLE_MPI
    if (hoomd_launch_timing && getNRanksGlobal() > 1)
        {
        // compute the number of seconds to get an exec conf
        timeval t;
        gettimeofday(&t, NULL);
        unsigned int conf_time = t.tv_sec - hoomd_launch_time;

        // get the min and max times
        unsigned int start_time_min, start_time_max, mpi_init_time_min, mpi_init_time_max, conf_time_min, conf_time_max;
        MPI_Reduce(&hoomd_start_time, &start_time_min, 1, MPI_UNSIGNED, MPI_MIN, 0, MPI_COMM_WORLD);
        MPI_Reduce(&hoomd_start_time, &start_time_max, 1, MPI_UNSIGNED, MPI_MAX, 0, MPI_COMM_WORLD);

        MPI_Reduce(&hoomd_mpi_init_time, &mpi_init_time_min, 1, MPI_UNSIGNED, MPI_MIN, 0, MPI_COMM_WORLD);
        MPI_Reduce(&hoomd_mpi_init_time, &mpi_init_time_max, 1, MPI_UNSIGNED, MPI_MAX, 0, MPI_COMM_WORLD);

        MPI_Reduce(&conf_time, &conf_time_min, 1, MPI_UNSIGNED, MPI_MIN, 0, MPI_COMM_WORLD);
        MPI_Reduce(&conf_time, &conf_time_max, 1, MPI_UNSIGNED, MPI_MAX, 0, MPI_COMM_WORLD);

        // write them out to a file
        if (getRankGlobal() == 0)
            {
            msg->notice(2) << "start_time:    [" << start_time_min << ", " << start_time_max << "]" << std::endl;
            msg->notice(2) << "mpi_init_time: [" << mpi_init_time_min << ", " << mpi_init_time_max << "]" << std::endl;
            msg->notice(2) << "conf_time:     [" << conf_time_min << ", " << conf_time_max << "]" << std::endl;
            }
        }
    #endif
    }

ExecutionConfiguration::~ExecutionConfiguration()
    {
    msg->notice(5) << "Destroying ExecutionConfiguration" << endl;

    #if defined(ENABLE_CUDA)
    if (exec_mode == GPU)
        {
        delete m_cached_alloc;

        #ifndef ENABLE_MPI_CUDA
        cudaDeviceReset();
        #endif
        }
    #endif

    #ifdef ENABLE_MPI
    // enable Messenger to gracefully finish any MPI-IO
    msg->unsetMPICommunicator();
    #endif
    }

#ifdef ENABLE_MPI
void ExecutionConfiguration::initializeMPI()
    {
    m_mpi_comm = MPI_COMM_WORLD;

    int num_total_ranks;
    MPI_Comm_size(m_mpi_comm, &num_total_ranks);

    unsigned int partition = 0;

    if  (m_n_rank != 0)
        {
        int  rank;
        MPI_Comm_rank(m_mpi_comm, &rank);

        if (num_total_ranks % m_n_rank != 0)
            {
            msg->error() << "Invalid setting --nrank" << std::endl;
            throw(runtime_error("Error setting up MPI."));
            }

        partition = rank / m_n_rank;

        // Split the communicator
        MPI_Comm new_comm;
        MPI_Comm_split(m_mpi_comm, partition, rank, &new_comm);

        // update communicator
        m_mpi_comm = new_comm;
        }

    int rank;
    MPI_Comm_rank(m_mpi_comm, &rank);
    m_rank = rank;

    msg->setRank(rank, partition);
    msg->setMPICommunicator(m_mpi_comm);
    }
#endif

std::string ExecutionConfiguration::getGPUName() const
    {
    #ifdef ENABLE_CUDA
    if (exec_mode == GPU)
        return string(dev_prop.name);
    else
        return string();
    #else
    return string();
    #endif
    }


#ifdef ENABLE_CUDA
/*! \returns Compute capability of GPU 0 as a string
    \note Silently returns an emtpy string if no GPUs are specified
*/
std::string ExecutionConfiguration::getComputeCapabilityAsString() const
    {
    ostringstream s;

    if (exec_mode == GPU)
        {
        s << dev_prop.major << "." << dev_prop.minor;
        }

    return s.str();
    }

/*! \returns Compute capability of the GPU formated as 210 (for compute 2.1 as an example)
    \note Silently returns 0 if no GPU is being used
*/
unsigned int ExecutionConfiguration::getComputeCapability() const
    {
    unsigned int result = 0;

    if (exec_mode == GPU)
        {
        result = dev_prop.major * 100 + dev_prop.minor * 10;
        }

    return result;
    }

void ExecutionConfiguration::handleCUDAError(cudaError_t err, const char *file, unsigned int line) const
    {
    // if there was an error
    if (err != cudaSuccess)
        {
        // remove HOOMD_SOURCE_DIR from the front of the file
        if (strlen(file) > strlen(HOOMD_SOURCE_DIR))
            file += strlen(HOOMD_SOURCE_DIR);

        // print an error message
        msg->error() << string(cudaGetErrorString(err)) << " before "
                     << file << ":" << line << endl;

        // throw an error exception
        throw(runtime_error("CUDA Error"));
        }
    }

void ExecutionConfiguration::checkCUDAError(const char *file, unsigned int line) const
    {
    cudaThreadSynchronize();
    cudaError_t err = cudaGetLastError();
    handleCUDAError(err, file, line);
    }

/*! \param gpu_id Index for the GPU to initialize, set to -1 for automatic selection
    \param min_cpu If set to true, the cudaDeviceBlockingSync device flag is set

    \pre scanGPUs has been called

    initializeGPU will loop through the specified list of GPUs, validate that each one is available for CUDA use
    and then setup CUDA to use the given GPU. After initialzeGPU completes, cuda calls can be made by the main
    application.
*/
void ExecutionConfiguration::initializeGPU(int gpu_id, bool min_cpu)
    {
    int capable_count = getNumCapableGPUs();
    if (capable_count == 0)
        {
        msg->error() << "No capable GPUs were found!" << endl;
        throw runtime_error("Error initializing execution configuration");
        }

    // setup the flags
    int flags = 0;
    if (min_cpu)
        {
        if (CUDART_VERSION < 2020)
            msg->notice(2) << "--minimize-cpu-usage will have no effect because this hoomd was built " << endl;
            msg->notice(2) << "against a version of CUDA prior to 2.2" << endl;

        flags |= cudaDeviceBlockingSync;
        }
    else
        {
        flags |= cudaDeviceScheduleSpin;
        }

    if (gpu_id < -1)
        {
        msg->error() << "The specified GPU id (" << gpu_id << ") is invalid." << endl;
        throw runtime_error("Error initializing execution configuration");
        }

    if (gpu_id >= (int)getNumTotalGPUs())
        {
        msg->error() << "The specified GPU id (" << gpu_id << ") is not present in the system." << endl;
        msg->error() << "CUDA reports only " << getNumTotalGPUs() << endl;
        throw runtime_error("Error initializing execution configuration");
        }

    if (!isGPUAvailable(gpu_id))
        {
        msg->error() << "The specified GPU id (" << gpu_id << ") is not available for executing HOOMD." << endl;
        throw runtime_error("Error initializing execution configuration");
        }

    cudaSetDeviceFlags(flags | cudaDeviceMapHost);
    cudaSetValidDevices(&m_gpu_list[0], (int)m_gpu_list.size());

    if (gpu_id != -1)
        {
        cudaSetDevice(gpu_id);
        }
    else
        {
        // initialize the default CUDA context
        cudaFree(0);
        }
    checkCUDAError(__FILE__, __LINE__);
    }

/*! Prints out a status line for the selected GPU
*/
void ExecutionConfiguration::printGPUStats()
    {
    msg->notice(1) << "HOOMD-blue is running on the following GPU(s):" << endl;

    // build a status line
    ostringstream s;

    // start with the device ID and name
    int dev;
    cudaGetDevice(&dev);

    s << " [" << dev << "]";
    s << setw(22) << dev_prop.name;

    // then print the SM count and version
    s << setw(4) << dev_prop.multiProcessorCount << " SM_" << dev_prop.major << "." << dev_prop.minor;

    // and the clock rate
    float ghz = float(dev_prop.clockRate)/1e6;
    s.precision(3);
    s.fill('0');
    s << " @ " << setw(4) << ghz << " GHz";
    s.fill(' ');

    // and the total amount of memory
    int mib = int(float(dev_prop.totalGlobalMem) / float(1024*1024));
    s << ", " << setw(4) << mib << " MiB DRAM";

    // follow up with some flags to signify device features
    if (dev_prop.kernelExecTimeoutEnabled)
        s << ", DIS";

    s << std::endl;
    // We print this information in rank order
    msg->collectiveNoticeStr(1,s.str());

    // if the gpu is compute 1.1 or older, it is unsupported. Issue a warning to the user.
    if (dev_prop.major <= 1 && dev_prop.minor <= 1)
        {
        msg->notice(2) << "HOOMD-blue is not supported on compute 1.1 and older GPUs" << endl;
        }
    }

//! Element in a priority sort of GPUs
struct gpu_elem
    {
    //! Constructor
    gpu_elem(float p=0.0f, int g=0) : priority(p), gpu_id(g) {}
    float priority;    //!< determined priority of the GPU
    int gpu_id;        //!< ID of the GPU
    };

//! less than operator for sorting gpu_elem
/*! \param a first element in the comparison
    \param b second element in the comparison
*/
bool operator<(const gpu_elem& a, const gpu_elem& b)
    {
    if (a.priority == b.priority)
        return a.gpu_id < b.gpu_id;
    else
        return a.priority > b.priority;
    }

/*! \param ignore_display If set to true, try to ignore GPUs attached to the display
    Each GPU that CUDA reports to exist is scrutinized to determine if it is actually capable of running HOOMD
    When one is found to be lacking, it is marked as unavailable and a short notice is printed as to why.

    \post m_gpu_list, m_gpu_available and m_system_compute_exclusive are all filled out
*/
void ExecutionConfiguration::scanGPUs(bool ignore_display)
    {
    // check the CUDA driver version
    int driverVersion = 0;
    cudaDriverGetVersion(&driverVersion);

    // first handle the situation where no driver is installed (or it is a CUDA 2.1 or earlier driver)
    if (driverVersion == 0)
        {
        msg->notice(2) << "NVIDIA driver not installed or is too old, ignoring any GPUs in the system." << endl;
        return;
        }

    // next, check to see if the driver is capable of running the version of CUDART that HOOMD was compiled against
    if (driverVersion < CUDART_VERSION)
        {
        int driver_major = driverVersion / 1000;
        int driver_minor = (driverVersion - driver_major * 1000) / 10;
        int cudart_major = CUDART_VERSION / 1000;
        int cudart_minor = (CUDART_VERSION - cudart_major * 1000) / 10;

        msg->notice(2) << "The NVIDIA driver only supports CUDA versions up to " << driver_major << "."
             << driver_minor << ", but HOOMD was built against CUDA " << cudart_major << "." << cudart_minor << endl;
        msg->notice(2) << "Ignoring any GPUs in the system." << endl;
        return;
        }

    // determine the number of GPUs that CUDA thinks there is
    int dev_count;
    cudaError_t error = cudaGetDeviceCount(&dev_count);
    if (error != cudaSuccess)
        {
        msg->notice(2) << "Error calling cudaGetDeviceCount(). No NVIDIA driver is present, or this user" << endl;
        msg->notice(2) << "does not have readwrite permissions on /dev/nvidia*" << endl;
        msg->notice(2) << "Ignoring any GPUs in the system." << endl;
        return;
        }

    // initialize variables
    int n_exclusive_gpus = 0;
    m_gpu_available.resize(dev_count);

    // loop through each GPU and check it's properties
    for (int dev = 0; dev < dev_count; dev++)
        {
        // get the device properties
        cudaDeviceProp prop;
        cudaError_t error = cudaGetDeviceProperties(&prop, dev);
        if (error != cudaSuccess)
            {
            msg->error() << "Error calling cudaGetDeviceProperties()" << endl;
            throw runtime_error("Error initializing execution configuration");
            }

        // start by assuming that the device is available, it will be excluded later if it is not
        m_gpu_available[dev] = true;

        // exclude the device emulation device
        if (prop.major == 9999 && prop.minor == 9999)
            {
            m_gpu_available[dev] = false;
            msg->notice(2) << "GPU id " << dev << " is not available for computation because "
                           << "it is an emulated device" << endl;
            }

        // exclude a GPU if it's compute version is not high enough
        int compoundComputeVer = prop.minor + prop.major * 10;
        if (m_gpu_available[dev] && compoundComputeVer < CUDA_ARCH)
            {
            m_gpu_available[dev] = false;
            msg->notice(2) << "Notice: GPU id " << dev << " is not available for computation because "
                           << "it's compute capability is not high enough" << endl;

            int min_major = CUDA_ARCH/10;
            int min_minor = CUDA_ARCH - min_major*10;

            msg->notice(2) << "This build of hoomd was compiled for a minimum capability of of " << min_major << "."
                           << min_minor << " but the GPU is only " << prop.major << "." << prop.minor << endl;
            }

        // ignore the display gpu if that was requested
        if (m_gpu_available[dev] && ignore_display && prop.kernelExecTimeoutEnabled)
            {
            m_gpu_available[dev] = false;
            msg->notice(2) << "Notice: GPU id " << dev << " is not available for computation because "
                           << "it appears to be attached to a display" << endl;
            }

        // exclude a gpu if it is compute-prohibited
        if (m_gpu_available[dev] && prop.computeMode == cudaComputeModeProhibited)
            {
            m_gpu_available[dev] = false;
            msg->notice(2) << "Notice: GPU id " << dev << " is not available for computation because "
                           << "it is set in the compute-prohibited mode" << endl;
            }

        // count the number of compute-exclusive gpus
        if (m_gpu_available[dev] &&
            (prop.computeMode == cudaComputeModeExclusive || prop.computeMode == cudaComputeModeExclusiveProcess))
            n_exclusive_gpus++;
        }

    std::vector<gpu_elem> gpu_priorities;
    for (int dev = 0; dev < dev_count; dev++)
        {
        if (m_gpu_available[dev])
            {
            cudaDeviceProp prop;
            cudaError_t error = cudaGetDeviceProperties(&prop, dev);
            if (error != cudaSuccess)
                {
                msg->error() << "Error calling cudaGetDeviceProperties()" << endl;
                throw runtime_error("Error initializing execution configuration");
                }

            // calculate a simple priority: multiprocessors * clock = speed, then subtract a bit if the device is
            // attached to a display
            float priority = float(prop.clockRate * prop.multiProcessorCount) / float(1e7);
            // if the GPU is compute 2.x, multiply that by 4 as there are 4x more SPs in each MP
            if (prop.major == 2)
                priority *= 4.0f;
            if (prop.major == 3)
                priority *= 24.0f;

            if (prop.kernelExecTimeoutEnabled)
                priority -= 0.1f;

            gpu_priorities.push_back(gpu_elem(priority, dev));
            }
        }

    // sort the GPUs based on priority
    sort(gpu_priorities.begin(), gpu_priorities.end());
    // add the prioritized GPUs to the list
    for (unsigned int i = 0; i < gpu_priorities.size(); i++)
        {
        m_gpu_list.push_back(gpu_priorities[i].gpu_id);
        }

    // the system is fully compute-exclusive if all capable GPUs are compute-exclusive
    if (n_exclusive_gpus == getNumCapableGPUs())
        m_system_compute_exclusive = true;
    else
        m_system_compute_exclusive = false;
    }


/*! \param gpu_id ID of the GPU to check for availability
    \pre scanGPUs() has been called

    \return The availability statis of GPU \a gpu_id as determined by scanGPU()
*/
bool ExecutionConfiguration::isGPUAvailable(int gpu_id)
    {
    if (gpu_id < -1)
        return false;
    if (gpu_id == -1)
        return true;
    if ((unsigned int)gpu_id >= m_gpu_available.size())
        return false;

    return m_gpu_available[gpu_id];
    }


/*! \pre scanGPUs() has been called
    \return The count of avaialble GPUs deteremined by scanGPUs
*/
int ExecutionConfiguration::getNumCapableGPUs()
    {
    int count = 0;
    for (unsigned int i = 0; i < m_gpu_available.size(); i++)
        {
        if (m_gpu_available[i])
            count++;
        }
    return count;
    }
#endif

int ExecutionConfiguration::guessLocalRank()
    {
    std::vector<std::string> env_vars;

    // setup common environment variables containing local rank information
    env_vars.push_back("MV2_COMM_WORLD_LOCAL_RANK");
    env_vars.push_back("OMPI_COMM_WORLD_LOCAL_RANK");

    std::vector<std::string>::iterator it;

    for (it = env_vars.begin(); it != env_vars.end(); it++)
        {
        char *env;
        if ((env = getenv(it->c_str())) != NULL)
            return atoi(env);
        }

    return -1;
    }

/*! Print out GPU stats if running on the GPU, otherwise determine and print out the CPU stats
*/
void ExecutionConfiguration::setupStats()
    {
    n_cpu = 1;

    #ifdef ENABLE_CUDA
    if (exec_mode == GPU)
        {
        int dev;
        cudaGetDevice(&dev);
        cudaGetDeviceProperties(&dev_prop, dev);
        printGPUStats();

        // GPU runs only use 1 CPU core
        n_cpu = 1;
        }
    #endif

    if (exec_mode == CPU)
        {
        ostringstream s;

        s << "HOOMD-blue is running on the CPU" << endl;
        msg->collectiveNoticeStr(1,s.str());
        }
    }

#ifdef ENABLE_MPI
unsigned int ExecutionConfiguration::getNRanks() const
    {
    int size;
    MPI_Comm_size(m_mpi_comm, &size);
    return size;
    }
#endif

void export_ExecutionConfiguration()
    {
    scope in_exec_conf = class_<ExecutionConfiguration, boost::shared_ptr<ExecutionConfiguration>, boost::noncopyable >
                         ("ExecutionConfiguration", init< ExecutionConfiguration::executionMode, int, bool, bool, boost::shared_ptr<Messenger>, unsigned int >())
                         .def("isCUDAEnabled", &ExecutionConfiguration::isCUDAEnabled)
                         .def("setCUDAErrorChecking", &ExecutionConfiguration::setCUDAErrorChecking)
                         .def("getGPUName", &ExecutionConfiguration::getGPUName)
                         .def_readonly("n_cpu", &ExecutionConfiguration::n_cpu)
                         .def_readonly("msg", &ExecutionConfiguration::msg)
#ifdef ENABLE_CUDA
                         .def("getComputeCapability", &ExecutionConfiguration::getComputeCapabilityAsString)
#endif
#ifdef ENABLE_MPI
                         .def("getPartition", &ExecutionConfiguration::getPartition)
                         .def("getNRanks", &ExecutionConfiguration::getNRanks)
                         .def("getRank", &ExecutionConfiguration::getRank)
                         .def("guessLocalRank", &ExecutionConfiguration::guessLocalRank)
                         .def("getNRanksGlobal", &ExecutionConfiguration::getNRanksGlobal)
                         .def("getRankGlobal", &ExecutionConfiguration::getRankGlobal)
                         .staticmethod("guessLocalRank")
                         .staticmethod("getNRanksGlobal")
                         .staticmethod("getRankGlobal")
#endif
                         ;

    enum_<ExecutionConfiguration::executionMode>("executionMode")
    .value("GPU", ExecutionConfiguration::GPU)
    .value("CPU", ExecutionConfiguration::CPU)
    .value("AUTO", ExecutionConfiguration::AUTO)
    ;

    // allow classes to take shared_ptr<const ExecutionConfiguration> arguments
    implicitly_convertible<boost::shared_ptr<ExecutionConfiguration>, boost::shared_ptr<const ExecutionConfiguration> >();
    }

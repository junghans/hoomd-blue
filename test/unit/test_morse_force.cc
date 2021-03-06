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

#ifdef WIN32
#pragma warning( push )
#pragma warning( disable : 4103 4244 )
#endif

#include <iostream>
#include <fstream>

#include <boost/bind.hpp>
#include <boost/function.hpp>
#include <boost/shared_ptr.hpp>

#include "AllPairPotentials.h"

#include "NeighborListBinned.h"
#include "Initializers.h"

#include <math.h>

using namespace std;
using namespace boost;

/*! \file morse_force_test.cc
    \brief Implements unit tests for PotentialPairMorse and descendants
    \ingroup unit_tests
*/

//! Name the unit test module
#define BOOST_TEST_MODULE PotentialPairMorseTests
#include "boost_utf_configure.h"

//! Typedef'd PotentialPairMorse factory
typedef boost::function<boost::shared_ptr<PotentialPairMorse> (boost::shared_ptr<SystemDefinition> sysdef,
                                                         boost::shared_ptr<NeighborList> nlist)> morseforce_creator;

//! Test the ability of the morse force compute to actually calucate forces
void morse_force_particle_test(morseforce_creator morse_creator, boost::shared_ptr<ExecutionConfiguration> exec_conf)
    {
    // this 3-particle test subtly checks several conditions
    // the particles are arranged on the x axis,  1   2   3
    // such that 2 is inside the cuttoff radius of 1 and 3, but 1 and 3 are outside the cuttoff
    // of course, the buffer will be set on the neighborlist so that 3 is included in it
    // thus, this case tests the ability of the force summer to sum more than one force on
    // a particle and ignore a particle outside the radius

    // periodic boundary conditions will be handeled in another test
    boost::shared_ptr<SystemDefinition> sysdef_3(new SystemDefinition(3, BoxDim(1000.0), 1, 0, 0, 0, 0, exec_conf));
    boost::shared_ptr<ParticleData> pdata_3 = sysdef_3->getParticleData();
    pdata_3->setFlags(~PDataFlags(0));

    {
    ArrayHandle<Scalar4> h_pos(pdata_3->getPositions(), access_location::host, access_mode::readwrite);

    h_pos.data[0].x = h_pos.data[0].y = h_pos.data[0].z = 0.0;
    h_pos.data[1].x = Scalar(1.0); h_pos.data[1].y = h_pos.data[1].z = 0.0;
    h_pos.data[2].x = Scalar(2.0); h_pos.data[2].y = h_pos.data[2].z = 0.0;
    }
    boost::shared_ptr<NeighborList> nlist_3(new NeighborList(sysdef_3, Scalar(1.3), Scalar(3.0)));
    boost::shared_ptr<PotentialPairMorse> fc_3 = morse_creator(sysdef_3, nlist_3);
    fc_3->setRcut(0, 0, Scalar(1.3));

    // first test: choose a basic set of values
    Scalar D0 = Scalar(1.0);
    Scalar alpha = Scalar(3.0);
    Scalar r0 = Scalar(0.9);
    fc_3->setParams(0,0,make_scalar4(D0,alpha,r0,Scalar(0.0)));

    // compute the forces
    fc_3->compute(0);

    {
    GPUArray<Scalar4>& force_array_1 =  fc_3->getForceArray();
    GPUArray<Scalar>& virial_array_1 =  fc_3->getVirialArray();
    ArrayHandle<Scalar4> h_force_1(force_array_1,access_location::host,access_mode::read);
    ArrayHandle<Scalar> h_virial_1(virial_array_1,access_location::host,access_mode::read);
    MY_BOOST_CHECK_CLOSE(h_force_1.data[0].x, 1.1520395075261485, tol);
    MY_BOOST_CHECK_SMALL(h_force_1.data[0].y, tol_small);
    MY_BOOST_CHECK_SMALL(h_force_1.data[0].z, tol_small);
    MY_BOOST_CHECK_CLOSE(h_force_1.data[0].w, -0.9328248052694093/2.0, tol);

    MY_BOOST_CHECK_SMALL(h_force_1.data[1].x, tol_small);
    MY_BOOST_CHECK_SMALL(h_force_1.data[1].y, tol_small);
    MY_BOOST_CHECK_SMALL(h_force_1.data[1].z, tol_small);
    MY_BOOST_CHECK_CLOSE(h_force_1.data[1].w, -0.9328248052694093, tol);

    MY_BOOST_CHECK_CLOSE(h_force_1.data[2].x, -1.1520395075261485, tol);
    MY_BOOST_CHECK_SMALL(h_force_1.data[2].y, tol_small);
    MY_BOOST_CHECK_SMALL(h_force_1.data[2].z, tol_small);
    MY_BOOST_CHECK_CLOSE(h_force_1.data[2].w, -0.9328248052694093/2.0, tol);
    }

    // swap the order of particles 0 and 2 in memory to check that the force compute handles this properly
    {
    ArrayHandle<Scalar4> h_pos(pdata_3->getPositions(), access_location::host, access_mode::readwrite);
    ArrayHandle<unsigned int> h_tag(pdata_3->getTags(), access_location::host, access_mode::readwrite);
    ArrayHandle<unsigned int> h_rtag(pdata_3->getRTags(), access_location::host, access_mode::readwrite);

    h_pos.data[2].x = h_pos.data[2].y = h_pos.data[2].z = 0.0;
    h_pos.data[0].x = Scalar(2.0); h_pos.data[0].y = h_pos.data[0].z = 0.0;

    h_tag.data[0] = 2;
    h_tag.data[2] = 0;
    h_rtag.data[0] = 2;
    h_rtag.data[2] = 0;
    }

    // notify the particle data that we changed the order
    pdata_3->notifyParticleSort();

    // recompute the forces at the same timestep, they should be updated
    fc_3->compute(1);

    {
    GPUArray<Scalar4>& force_array_2 =  fc_3->getForceArray();
    GPUArray<Scalar>& virial_array_2 =  fc_3->getVirialArray();
    ArrayHandle<Scalar4> h_force_2(force_array_2,access_location::host,access_mode::read);
    ArrayHandle<Scalar> h_virial_2(virial_array_2,access_location::host,access_mode::read);
    MY_BOOST_CHECK_CLOSE(h_force_2.data[0].x, -1.1520395075261485, tol);
    MY_BOOST_CHECK_CLOSE(h_force_2.data[2].x, 1.1520395075261485, tol);
    }
    }

//! Unit test a comparison between 2 PotentialPairMorse's on a "real" system
void morse_force_comparison_test(morseforce_creator morse_creator1,
                                  morseforce_creator morse_creator2,
                                  boost::shared_ptr<ExecutionConfiguration> exec_conf)
    {
    const unsigned int N = 5000;

    // create a random particle system to sum forces on
    RandomInitializer rand_init(N, Scalar(0.1), Scalar(1.0), "A");
    boost::shared_ptr<SnapshotSystemData> snap = rand_init.getSnapshot();
    boost::shared_ptr<SystemDefinition> sysdef(new SystemDefinition(snap, exec_conf));
    boost::shared_ptr<ParticleData> pdata = sysdef->getParticleData();
    pdata->setFlags(~PDataFlags(0));
    boost::shared_ptr<NeighborListBinned> nlist(new NeighborListBinned(sysdef, Scalar(3.0), Scalar(0.8)));

    boost::shared_ptr<PotentialPairMorse> fc1 = morse_creator1(sysdef, nlist);
    boost::shared_ptr<PotentialPairMorse> fc2 = morse_creator2(sysdef, nlist);
    fc1->setRcut(0, 0, Scalar(3.0));
    fc2->setRcut(0, 0, Scalar(3.0));

    // setup some values for epsilon and sigma
    Scalar D0 = Scalar(1.0);
    Scalar alpha = Scalar(1.0);
    Scalar r0 = Scalar(1.0);

    // specify the force parameters
    fc1->setParams(0,0,make_scalar4(D0,alpha,r0,Scalar(0.0)));
    fc2->setParams(0,0,make_scalar4(D0,alpha,r0,Scalar(0.0)));

    // compute the forces
    fc1->compute(0);
    fc2->compute(0);

    {
    // verify that the forces are identical (within roundoff errors)
    GPUArray<Scalar4>& force_array_3 =  fc1->getForceArray();
    GPUArray<Scalar>& virial_array_3 =  fc1->getVirialArray();
    unsigned int pitch = virial_array_3.getPitch();
    ArrayHandle<Scalar4> h_force_3(force_array_3,access_location::host,access_mode::read);
    ArrayHandle<Scalar> h_virial_3(virial_array_3,access_location::host,access_mode::read);
    GPUArray<Scalar4>& force_array_4 =  fc2->getForceArray();
    GPUArray<Scalar>& virial_array_4 =  fc2->getVirialArray();
    ArrayHandle<Scalar4> h_force_4(force_array_4,access_location::host,access_mode::read);
    ArrayHandle<Scalar> h_virial_4(virial_array_4,access_location::host,access_mode::read);

    // compare average deviation between the two computes
    double deltaf2 = 0.0;
    double deltape2 = 0.0;
    double deltav2[6];
    for (unsigned int j = 0; j < 6; j++)
        deltav2[j] = 0.0;

    for (unsigned int i = 0; i < N; i++)
        {
        deltaf2 += double(h_force_4.data[i].x - h_force_3.data[i].x) * double(h_force_4.data[i].x - h_force_3.data[i].x);
        deltaf2 += double(h_force_4.data[i].y - h_force_3.data[i].y) * double(h_force_4.data[i].y - h_force_3.data[i].y);
        deltaf2 += double(h_force_4.data[i].z - h_force_3.data[i].z) * double(h_force_4.data[i].z - h_force_3.data[i].z);
        deltape2 += double(h_force_4.data[i].w - h_force_3.data[i].w) * double(h_force_4.data[i].w - h_force_3.data[i].w);
        for (unsigned int j = 0; j < 6; j++)
            deltav2[j] += double(h_virial_4.data[j*pitch+i] - h_virial_3.data[j*pitch+i]) * double(h_virial_4.data[j*pitch+i] - h_virial_3.data[j*pitch+i]);

        // also check that each individual calculation is somewhat close
        }
    deltaf2 /= double(pdata->getN());
    deltape2 /= double(pdata->getN());
    for (unsigned int j = 0; j < 6; j++)
        deltav2[j] /= double(pdata->getN());
    BOOST_CHECK_SMALL(deltaf2, double(tol_small));
    BOOST_CHECK_SMALL(deltape2, double(tol_small));
    BOOST_CHECK_SMALL(deltav2[0], double(tol_small));
    BOOST_CHECK_SMALL(deltav2[1], double(tol_small));
    BOOST_CHECK_SMALL(deltav2[2], double(tol_small));
    BOOST_CHECK_SMALL(deltav2[3], double(tol_small));
    BOOST_CHECK_SMALL(deltav2[4], double(tol_small));
    BOOST_CHECK_SMALL(deltav2[5], double(tol_small));
    }
    }

//! PotentialPairMorse creator for unit tests
boost::shared_ptr<PotentialPairMorse> base_class_morse_creator(boost::shared_ptr<SystemDefinition> sysdef,
                                                          boost::shared_ptr<NeighborList> nlist)
    {
    return boost::shared_ptr<PotentialPairMorse>(new PotentialPairMorse(sysdef, nlist));
    }

#ifdef ENABLE_CUDA
//! PotentialPairMorseGPU creator for unit tests
boost::shared_ptr<PotentialPairMorseGPU> gpu_morse_creator(boost::shared_ptr<SystemDefinition> sysdef,
                                                      boost::shared_ptr<NeighborList> nlist)
    {
    nlist->setStorageMode(NeighborList::full);
    boost::shared_ptr<PotentialPairMorseGPU> morse(new PotentialPairMorseGPU(sysdef, nlist));
    return morse;
    }
#endif

//! boost test case for particle test on CPU
BOOST_AUTO_TEST_CASE( MorseForce_particle )
    {
    morseforce_creator morse_creator_base = bind(base_class_morse_creator, _1, _2);
    morse_force_particle_test(morse_creator_base, boost::shared_ptr<ExecutionConfiguration>(new ExecutionConfiguration(ExecutionConfiguration::CPU)));
    }

# ifdef ENABLE_CUDA
//! boost test case for particle test on GPU
BOOST_AUTO_TEST_CASE( MorseForceGPU_particle )
    {
    morseforce_creator morse_creator_gpu = bind(gpu_morse_creator, _1, _2);
    morse_force_particle_test(morse_creator_gpu, boost::shared_ptr<ExecutionConfiguration>(new ExecutionConfiguration(ExecutionConfiguration::GPU)));
    }

//! boost test case for comparing GPU output to base class output
BOOST_AUTO_TEST_CASE( MorseForceGPU_compare )
    {
    morseforce_creator morse_creator_gpu = bind(gpu_morse_creator, _1, _2);
    morseforce_creator morse_creator_base = bind(base_class_morse_creator, _1, _2);
    morse_force_comparison_test(morse_creator_base,
                                 morse_creator_gpu,
                                 boost::shared_ptr<ExecutionConfiguration>(new ExecutionConfiguration(ExecutionConfiguration::GPU)));
    }

#endif

#ifdef WIN32
#pragma warning( pop )
#endif

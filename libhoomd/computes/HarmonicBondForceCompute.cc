/*
Highly Optimized Object-oriented Many-particle Dynamics -- Blue Edition
(HOOMD-blue) Open Source Software License Copyright 2008, 2009 Ames Laboratory
Iowa State University and The Regents of the University of Michigan All rights
reserved.

HOOMD-blue may contain modifications ("Contributions") provided, and to which
copyright is held, by various Contributors who have granted The Regents of the
University of Michigan the right to modify and/or distribute such Contributions.

Redistribution and use of HOOMD-blue, in source and binary forms, with or
without modification, are permitted, provided that the following conditions are
met:

* Redistributions of source code must retain the above copyright notice, this
list of conditions, and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice, this
list of conditions, and the following disclaimer in the documentation and/or
other materials provided with the distribution.

* Neither the name of the copyright holder nor the names of HOOMD-blue's
contributors may be used to endorse or promote products derived from this
software without specific prior written permission.

Disclaimer

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER AND CONTRIBUTORS ``AS IS''
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, AND/OR
ANY WARRANTIES THAT THIS SOFTWARE IS FREE OF INFRINGEMENT ARE DISCLAIMED.

IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

// $Id$
// $URL$
// Maintainer: joaander

#ifdef WIN32
#pragma warning( push )
#pragma warning( disable : 4244 )
#endif

#include <boost/python.hpp>
using namespace boost::python;

#include "HarmonicBondForceCompute.h"

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <math.h>

using namespace std;

/*! \file HarmonicBondForceCompute.cc
    \brief Contains code for the HarmonicBondForceCompute class
*/

/*! \param sysdef System to compute forces on
    \post Memory is allocated, and forces are zeroed.
*/
HarmonicBondForceCompute::HarmonicBondForceCompute(boost::shared_ptr<SystemDefinition> sysdef, const std::string& log_suffix) 
    : ForceCompute(sysdef), m_K(NULL), m_r_0(NULL)
    {
    // access the bond data for later use
    m_bond_data = m_sysdef->getBondData();
    m_log_name = std::string( "bond_harmonic_energy") + log_suffix;
    cout << "My log name is " << m_log_name << endl;
    
    // check for some silly errors a user could make
    if (m_bond_data->getNBondTypes() == 0)
        {
        cout << endl << "***Error! No bond types specified" << endl << endl;
        throw runtime_error("Error initializing HarmonicBondForceCompute");
        }
        
    // allocate the parameters
    m_K = new Scalar[m_bond_data->getNBondTypes()];
    m_r_0 = new Scalar[m_bond_data->getNBondTypes()];
    
    // zero parameters
    memset(m_K, 0, sizeof(Scalar) * m_bond_data->getNBondTypes());
    memset(m_r_0, 0, sizeof(Scalar) * m_bond_data->getNBondTypes());
    }

HarmonicBondForceCompute::~HarmonicBondForceCompute()
    {
    delete[] m_K;
    delete[] m_r_0;
    }

/*! \param type Type of the bond to set parameters for
    \param K Stiffness parameter for the force computation
    \param r_0 Equilibrium length for the force computation

    Sets parameters for the potential of a particular bond type
*/
void HarmonicBondForceCompute::setParams(unsigned int type, Scalar K, Scalar r_0)
    {
    // make sure the type is valid
    if (type >= m_bond_data->getNBondTypes())
        {
        cout << endl << "***Error! Invalid bond type specified" << endl << endl;
        throw runtime_error("Error setting parameters in HarmonicBondForceCompute");
        }
        
    m_K[type] = K;
    m_r_0[type] = r_0;
    
    // check for some silly errors a user could make
    if (K <= 0)
        cout << "***Warning! K <= 0 specified for harmonic bond" << endl;
    if (r_0 <= 0)
        cout << "***Warning! r_0 <= 0 specified for harmonic bond" << endl;
    }

/*! BondForceCompute provides
    - \c harmonic_energy
*/
std::vector< std::string > HarmonicBondForceCompute::getProvidedLogQuantities()
    {
    vector<string> list;
    list.push_back(m_log_name);
    return list;
    }

/*! \param quantity Name of the quantity to get the log value of
    \param timestep Current time step of the simulation
*/
Scalar HarmonicBondForceCompute::getLogValue(const std::string& quantity, unsigned int timestep)
    {
    if (quantity == string(m_log_name))
        {
        compute(timestep);
        return calcEnergySum();
        }
    else
        {
        cerr << endl << "***Error! " << quantity << " is not a valid log quantity for BondForceCompute" << endl << endl;
        throw runtime_error("Error getting log value");
        }
    }

/*! Actually perform the force computation
    \param timestep Current time step
 */
void HarmonicBondForceCompute::computeForces(unsigned int timestep)
    {
    if (m_prof) m_prof->push("Harmonic");
    
    assert(m_pdata);
    // access the particle data arrays
    ParticleDataArraysConst arrays = m_pdata->acquireReadOnly();
    // there are enough other checks on the input data: but it doesn't hurt to be safe
    assert(m_fx);
    assert(m_fy);
    assert(m_fz);
    assert(m_pe);
    assert(arrays.x);
    assert(arrays.y);
    assert(arrays.z);
    
    // get a local copy of the simulation box too
    const BoxDim& box = m_pdata->getBox();
    // sanity check
    assert(box.xhi > box.xlo && box.yhi > box.ylo && box.zhi > box.zlo);
    
    // precalculate box lenghts
    Scalar Lx = box.xhi - box.xlo;
    Scalar Ly = box.yhi - box.ylo;
    Scalar Lz = box.zhi - box.zlo;
    Scalar Lx2 = Lx / Scalar(2.0);
    Scalar Ly2 = Ly / Scalar(2.0);
    Scalar Lz2 = Lz / Scalar(2.0);
    
    // need to start from a zero force
    // MEM TRANSFER: 5*N Scalars
    memset((void*)m_fx, 0, sizeof(Scalar) * m_pdata->getN());
    memset((void*)m_fy, 0, sizeof(Scalar) * m_pdata->getN());
    memset((void*)m_fz, 0, sizeof(Scalar) * m_pdata->getN());
    memset((void*)m_pe, 0, sizeof(Scalar) * m_pdata->getN());
    memset((void*)m_virial, 0, sizeof(Scalar) * m_pdata->getN());
    
    // for each of the bonds
    const unsigned int size = (unsigned int)m_bond_data->getNumBonds();
    for (unsigned int i = 0; i < size; i++)
        {
        // lookup the tag of each of the particles participating in the bond
        const Bond& bond = m_bond_data->getBond(i);
        assert(bond.a < m_pdata->getN());
        assert(bond.b < m_pdata->getN());
        
        // transform a and b into indicies into the particle data arrays
        // MEM TRANSFER: 4 ints
        unsigned int idx_a = arrays.rtag[bond.a];
        unsigned int idx_b = arrays.rtag[bond.b];
        assert(idx_a < m_pdata->getN());
        assert(idx_b < m_pdata->getN());
        
        // calculate d\vec{r}
        // MEM_TRANSFER: 6 Scalars / FLOPS 3
        Scalar dx = arrays.x[idx_b] - arrays.x[idx_a];
        Scalar dy = arrays.y[idx_b] - arrays.y[idx_a];
        Scalar dz = arrays.z[idx_b] - arrays.z[idx_a];
        
        // if the vector crosses the box, pull it back
        // (FLOPS: 9 (worst case: first branch is missed, the 2nd is taken and the add is done))
        if (dx >= Lx2)
            dx -= Lx;
        else if (dx < -Lx2)
            dx += Lx;
            
        if (dy >= Ly2)
            dy -= Ly;
        else if (dy < -Ly2)
            dy += Ly;
            
        if (dz >= Lz2)
            dz -= Lz;
        else if (dz < -Lz2)
            dz += Lz;
            
        // sanity check
        assert(dx >= box.xlo && dx < box.xhi);
        assert(dy >= box.ylo && dx < box.yhi);
        assert(dz >= box.zlo && dx < box.zhi);
        
        // on paper, the formula turns out to be: F = K*\vec{r} * (r_0/r - 1)
        // FLOPS: 14 / MEM TRANSFER: 2 Scalars
        Scalar rsq = dx*dx+dy*dy+dz*dz;
        Scalar r = sqrt(rsq);
        Scalar forcemag_divr = m_K[bond.type] * (m_r_0[bond.type] / r - Scalar(1.0));
        Scalar bond_eng = Scalar(0.5) * Scalar(0.5) * m_K[bond.type] * (m_r_0[bond.type] - r) * (m_r_0[bond.type] - r);
        
        // calculate the virial (FLOPS: 2)
        Scalar bond_virial = Scalar(1.0/6.0) * rsq * forcemag_divr;
        
        // add the force to the particles (FLOPS: 16 / MEM TRANSFER: 20 Scalars)
        m_fx[idx_b] += forcemag_divr * dx;
        m_fy[idx_b] += forcemag_divr * dy;
        m_fz[idx_b] += forcemag_divr * dz;
        m_pe[idx_b] += bond_eng;
        m_virial[idx_b] += bond_virial;
        
        m_fx[idx_a] -= forcemag_divr * dx;
        m_fy[idx_a] -= forcemag_divr * dy;
        m_fz[idx_a] -= forcemag_divr * dz;
        m_pe[idx_a] += bond_eng;
        m_virial[idx_a] += bond_virial;
        }
        
    m_pdata->release();
    
#ifdef ENABLE_CUDA
    // the data is now only up to date on the CPU
    m_data_location = cpu;
#endif
    
    int64_t flops = size*(3 + 9 + 14 + 2 + 16);
    int64_t mem_transfer = m_pdata->getN() * 5 * sizeof(Scalar) + size * ( (4)*sizeof(unsigned int) + (6+2+20)*sizeof(Scalar) );
    if (m_prof) m_prof->pop(flops, mem_transfer);
    }

void export_HarmonicBondForceCompute()
    {
    class_<HarmonicBondForceCompute, boost::shared_ptr<HarmonicBondForceCompute>, bases<ForceCompute>, boost::noncopyable >
    ("HarmonicBondForceCompute", init< boost::shared_ptr<SystemDefinition>, const std::string& >())
    .def("setParams", &HarmonicBondForceCompute::setParams)
    ;
    }

#ifdef WIN32
#pragma warning( pop )
#endif


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

/*! \file lj_force_test.cc
    \brief Implements unit tests for PotentialPairLJ and PotentialPairLJGPU and descendants
    \ingroup unit_tests
*/

//! Name the unit test module
#define BOOST_TEST_MODULE PotentialPairLJTests
#include "boost_utf_configure.h"

//! Typedef'd LJForceCompute factory
typedef boost::function<shared_ptr<PotentialPairLJ> (shared_ptr<SystemDefinition> sysdef,
                                                     shared_ptr<NeighborList> nlist)> ljforce_creator;

//! Test the ability of the lj force compute to actually calucate forces
void lj_force_particle_test(ljforce_creator lj_creator, boost::shared_ptr<ExecutionConfiguration> exec_conf)
    {
    // this 3-particle test subtly checks several conditions
    // the particles are arranged on the x axis,  1   2   3
    // such that 2 is inside the cuttoff radius of 1 and 3, but 1 and 3 are outside the cuttoff
    // of course, the buffer will be set on the neighborlist so that 3 is included in it
    // thus, this case tests the ability of the force summer to sum more than one force on
    // a particle and ignore a particle outside the radius
    
    // periodic boundary conditions will be handeled in another test
    shared_ptr<SystemDefinition> sysdef_3(new SystemDefinition(3, BoxDim(1000.0), 1, 0, 0, 0, 0, exec_conf));
    shared_ptr<ParticleData> pdata_3 = sysdef_3->getParticleData();
    
    ParticleDataArrays arrays = pdata_3->acquireReadWrite();
    arrays.x[0] = arrays.y[0] = arrays.z[0] = 0.0;
    arrays.x[1] = Scalar(pow(2.0,1.0/6.0)); arrays.y[1] = arrays.z[1] = 0.0;
    arrays.x[2] = Scalar(2.0*pow(2.0,1.0/6.0)); arrays.y[2] = arrays.z[2] = 0.0;
    pdata_3->release();
    shared_ptr<NeighborList> nlist_3(new NeighborList(sysdef_3, Scalar(1.3), Scalar(3.0)));
    shared_ptr<PotentialPairLJ> fc_3 = lj_creator(sysdef_3, nlist_3);
    fc_3->setRcut(0, 0, Scalar(1.3));
    
    // first test: setup a sigma of 1.0 so that all forces will be 0
    Scalar epsilon = Scalar(1.15);
    Scalar sigma = Scalar(1.0);
    Scalar alpha = Scalar(1.0);
    Scalar lj1 = Scalar(4.0) * epsilon * pow(sigma,Scalar(12.0));
    Scalar lj2 = alpha * Scalar(4.0) * epsilon * pow(sigma,Scalar(6.0));
    fc_3->setParams(0,0,make_scalar2(lj1,lj2));
    
    // compute the forces
    fc_3->compute(0);
    
    GPUArray<Scalar4>& force_array_1 =  fc_3->getForceArray();
    GPUArray<Scalar>& virial_array_1 =  fc_3->getVirialArray();
    ArrayHandle<Scalar4> h_force_1(force_array_1,access_location::host,access_mode::read);
    ArrayHandle<Scalar> h_virial_1(virial_array_1,access_location::host,access_mode::read);
    MY_BOOST_CHECK_SMALL(h_force_1.data[0].x, tol_small);
    MY_BOOST_CHECK_SMALL(h_force_1.data[0].y, tol_small);
    MY_BOOST_CHECK_SMALL(h_force_1.data[0].z, tol_small);
    MY_BOOST_CHECK_CLOSE(h_force_1.data[0].w, -0.575, tol);
    MY_BOOST_CHECK_SMALL(h_virial_1.data[0], tol_small);
    
    MY_BOOST_CHECK_SMALL(h_force_1.data[1].x, tol_small);
    MY_BOOST_CHECK_SMALL(h_force_1.data[1].y, tol_small);
    MY_BOOST_CHECK_SMALL(h_force_1.data[1].z, tol_small);
    MY_BOOST_CHECK_CLOSE(h_force_1.data[1].w, -1.15, tol);
    MY_BOOST_CHECK_SMALL(h_virial_1.data[1], tol_small);
    
    MY_BOOST_CHECK_SMALL(h_force_1.data[2].x, tol_small);
    MY_BOOST_CHECK_SMALL(h_force_1.data[2].y, tol_small);
    MY_BOOST_CHECK_SMALL(h_force_1.data[2].z, tol_small);
    MY_BOOST_CHECK_CLOSE(h_force_1.data[2].w, -0.575, tol);
    MY_BOOST_CHECK_SMALL(h_virial_1.data[2], tol_small);
    
    // now change sigma and alpha so we can check that it is computing the right force
    sigma = Scalar(1.2); // < bigger sigma should push particle 0 left and particle 2 right
    alpha = Scalar(0.45);
    lj1 = Scalar(4.0) * epsilon * pow(sigma,Scalar(12.0));
    lj2 = alpha * Scalar(4.0) * epsilon * pow(sigma,Scalar(6.0));
    fc_3->setParams(0,0,make_scalar2(lj1,lj2));
    fc_3->compute(1);
    
    GPUArray<Scalar4>& force_array_2 =  fc_3->getForceArray();
    GPUArray<Scalar>& virial_array_2 =  fc_3->getVirialArray();
    ArrayHandle<Scalar4> h_force_2(force_array_2,access_location::host,access_mode::read);
    ArrayHandle<Scalar> h_virial_2(virial_array_2,access_location::host,access_mode::read);
    MY_BOOST_CHECK_CLOSE(h_force_2.data[0].x, -93.09822608552962, tol);
    MY_BOOST_CHECK_SMALL(h_force_2.data[0].y, tol_small);
    MY_BOOST_CHECK_SMALL(h_force_2.data[0].z, tol_small);
    MY_BOOST_CHECK_CLOSE(h_force_2.data[0].w, 3.5815110377468, tol);
    MY_BOOST_CHECK_CLOSE(h_virial_2.data[0], 17.416537590989, tol);
    
    // center particle should still be a 0 force by symmetry
    MY_BOOST_CHECK_SMALL(h_force_2.data[1].x, tol_small);
    MY_BOOST_CHECK_SMALL(h_force_2.data[1].y, tol_small);
    MY_BOOST_CHECK_SMALL(h_force_2.data[1].z, tol_small);
    // there is still an energy and virial, though
    MY_BOOST_CHECK_CLOSE(h_force_2.data[1].w, 7.1630220754935, tol);
    MY_BOOST_CHECK_CLOSE(h_virial_2.data[1], 34.833075181975, tol);
    
    MY_BOOST_CHECK_CLOSE(h_force_2.data[2].x, 93.09822608552962, tol);
    MY_BOOST_CHECK_SMALL(h_force_2.data[2].y, tol_small);
    MY_BOOST_CHECK_SMALL(h_force_2.data[2].z, tol_small);
    MY_BOOST_CHECK_CLOSE(h_force_2.data[2].w, 3.581511037746, tol);
    MY_BOOST_CHECK_CLOSE(h_virial_2.data[2], 17.416537590989, tol);
    
    // swap the order of particles 0 ans 2 in memory to check that the force compute handles this properly
    arrays = pdata_3->acquireReadWrite();
    arrays.x[2] = arrays.y[2] = arrays.z[2] = 0.0;
    arrays.x[0] = Scalar(2.0*pow(2.0,1.0/6.0)); arrays.y[0] = arrays.z[0] = 0.0;
    
    arrays.tag[0] = 2;
    arrays.tag[2] = 0;
    arrays.rtag[0] = 2;
    arrays.rtag[2] = 0;
    pdata_3->release();
    
    // notify the particle data that we changed the order
    pdata_3->notifyParticleSort();
    
    // recompute the forces at the same timestep, they should be updated
    fc_3->compute(1);
    GPUArray<Scalar4>& force_array_3 =  fc_3->getForceArray();
    GPUArray<Scalar>& virial_array_3 =  fc_3->getVirialArray();
    ArrayHandle<Scalar4> h_force_3(force_array_3,access_location::host,access_mode::read);
    ArrayHandle<Scalar> h_virial_3(virial_array_3,access_location::host,access_mode::read);
    MY_BOOST_CHECK_CLOSE(h_force_3.data[0].x, 93.09822608552962, tol);
    MY_BOOST_CHECK_CLOSE(h_force_3.data[2].x, -93.09822608552962, tol);
    }

//! Tests the ability of a LJForceCompute to handle periodic boundary conditions
void lj_force_periodic_test(ljforce_creator lj_creator, boost::shared_ptr<ExecutionConfiguration> exec_conf)
    {
    ////////////////////////////////////////////////////////////////////
    // now, lets do a more thorough test and include boundary conditions
    // there are way too many permutations to test here, so I will simply
    // test +x, -x, +y, -y, +z, and -z independantly
    // build a 6 particle system with particles across each boundary
    // also test the ability of the force compute to use different particle types
    
    shared_ptr<SystemDefinition> sysdef_6(new SystemDefinition(6, BoxDim(20.0, 40.0, 60.0), 3, 0, 0, 0, 0, exec_conf));
    shared_ptr<ParticleData> pdata_6 = sysdef_6->getParticleData();
    
    ParticleDataArrays arrays = pdata_6->acquireReadWrite();
    arrays.x[0] = Scalar(-9.6); arrays.y[0] = 0; arrays.z[0] = 0.0;
    arrays.x[1] =  Scalar(9.6); arrays.y[1] = 0; arrays.z[1] = 0.0;
    arrays.x[2] = 0; arrays.y[2] = Scalar(-19.6); arrays.z[2] = 0.0;
    arrays.x[3] = 0; arrays.y[3] = Scalar(19.6); arrays.z[3] = 0.0;
    arrays.x[4] = 0; arrays.y[4] = 0; arrays.z[4] = Scalar(-29.6);
    arrays.x[5] = 0; arrays.y[5] = 0; arrays.z[5] =  Scalar(29.6);
    
    arrays.type[0] = 0;
    arrays.type[1] = 1;
    arrays.type[2] = 2;
    arrays.type[3] = 0;
    arrays.type[4] = 2;
    arrays.type[5] = 1;
    pdata_6->release();
    
    shared_ptr<NeighborList> nlist_6(new NeighborList(sysdef_6, Scalar(1.3), Scalar(3.0)));
    shared_ptr<PotentialPairLJ> fc_6 = lj_creator(sysdef_6, nlist_6);
    fc_6->setRcut(0, 0, Scalar(1.3));
    fc_6->setRcut(0, 1, Scalar(1.3));
    fc_6->setRcut(0, 2, Scalar(1.3));
    fc_6->setRcut(1, 1, Scalar(1.3));
    fc_6->setRcut(1, 2, Scalar(1.3));
    fc_6->setRcut(2, 2, Scalar(1.3));
    
    // choose a small sigma so that all interactions are attractive
    Scalar epsilon = Scalar(1.0);
    Scalar sigma = Scalar(0.5);
    Scalar alpha = Scalar(0.45);
    Scalar lj1 = Scalar(4.0) * epsilon * pow(sigma,Scalar(12.0));
    Scalar lj2 = alpha * Scalar(4.0) * epsilon * pow(sigma,Scalar(6.0));
    
    // make life easy: just change epsilon for the different pairs
    fc_6->setParams(0,0,make_scalar2(lj1,lj2));
    fc_6->setParams(0,1,make_scalar2(Scalar(2.0)*lj1,Scalar(2.0)*lj2));
    fc_6->setParams(0,2,make_scalar2(Scalar(3.0)*lj1,Scalar(3.0)*lj2));
    fc_6->setParams(1,1,make_scalar2(Scalar(4.0)*lj1,Scalar(4.0)*lj2));
    fc_6->setParams(1,2,make_scalar2(Scalar(5.0)*lj1,Scalar(5.0)*lj2));
    fc_6->setParams(2,2,make_scalar2(Scalar(6.0)*lj1,Scalar(6.0)*lj2));
    
    fc_6->compute(0);
    
    GPUArray<Scalar4>& force_array_4 =  fc_6->getForceArray();
    GPUArray<Scalar>& virial_array_4 =  fc_6->getVirialArray();
    ArrayHandle<Scalar4> h_force_4(force_array_4,access_location::host,access_mode::read);
    ArrayHandle<Scalar> h_virial_4(virial_array_4,access_location::host,access_mode::read);
    // particle 0 should be pulled left
    MY_BOOST_CHECK_CLOSE(h_force_4.data[0].x, -1.18299976747949, tol);
    MY_BOOST_CHECK_SMALL(h_force_4.data[0].y, tol_small);
    MY_BOOST_CHECK_SMALL(h_force_4.data[0].z, tol_small);
    MY_BOOST_CHECK_CLOSE(h_virial_4.data[0], -0.15773330233059, tol);
    
    // particle 1 should be pulled right
    MY_BOOST_CHECK_CLOSE(h_force_4.data[1].x, 1.18299976747949, tol);
    MY_BOOST_CHECK_SMALL(h_force_4.data[1].y, tol_small);
    MY_BOOST_CHECK_SMALL(h_force_4.data[1].z, tol_small);
    MY_BOOST_CHECK_CLOSE(h_virial_4.data[1], -0.15773330233059, tol);
    
    // particle 2 should be pulled down
    MY_BOOST_CHECK_CLOSE(h_force_4.data[2].y, -1.77449965121923, tol);
    MY_BOOST_CHECK_SMALL(h_force_4.data[2].x, tol_small);
    MY_BOOST_CHECK_SMALL(h_force_4.data[2].z, tol_small);
    MY_BOOST_CHECK_CLOSE(h_virial_4.data[2], -0.23659995349591, tol);
    
    // particle 3 should be pulled up
    MY_BOOST_CHECK_CLOSE(h_force_4.data[3].y, 1.77449965121923, tol);
    MY_BOOST_CHECK_SMALL(h_force_4.data[3].x, tol_small);
    MY_BOOST_CHECK_SMALL(h_force_4.data[3].z, tol_small);
    MY_BOOST_CHECK_CLOSE(h_virial_4.data[3], -0.23659995349591, tol);
    
    // particle 4 should be pulled back
    MY_BOOST_CHECK_CLOSE(h_force_4.data[4].z, -2.95749941869871, tol);
    MY_BOOST_CHECK_SMALL(h_force_4.data[4].x, tol_small);
    MY_BOOST_CHECK_SMALL(h_force_4.data[4].y, tol_small);
    MY_BOOST_CHECK_CLOSE(h_virial_4.data[4], -0.39433325582651, tol);
    
    // particle 3 should be pulled forward
    MY_BOOST_CHECK_CLOSE(h_force_4.data[5].z, 2.95749941869871, tol);
    MY_BOOST_CHECK_SMALL(h_force_4.data[5].x, tol_small);
    MY_BOOST_CHECK_SMALL(h_force_4.data[5].y, tol_small);
    MY_BOOST_CHECK_CLOSE(h_virial_4.data[5], -0.39433325582651, tol);
    }

//! Unit test a comparison between 2 LJForceComputes on a "real" system
void lj_force_comparison_test(ljforce_creator lj_creator1, ljforce_creator lj_creator2, boost::shared_ptr<ExecutionConfiguration> exec_conf)
    {
    const unsigned int N = 5000;
    
    // create a random particle system to sum forces on
    RandomInitializer rand_init(N, Scalar(0.2), Scalar(0.9), "A");
    shared_ptr<SystemDefinition> sysdef(new SystemDefinition(rand_init, exec_conf));
    shared_ptr<ParticleData> pdata = sysdef->getParticleData();
    
    shared_ptr<NeighborListBinned> nlist(new NeighborListBinned(sysdef, Scalar(3.0), Scalar(0.8)));
    
    shared_ptr<PotentialPairLJ> fc1 = lj_creator1(sysdef, nlist);
    shared_ptr<PotentialPairLJ> fc2 = lj_creator2(sysdef, nlist);
    fc1->setRcut(0, 0, Scalar(3.0));
    fc2->setRcut(0, 0, Scalar(3.0));
    
    // setup some values for alpha and sigma
    Scalar epsilon = Scalar(1.0);
    Scalar sigma = Scalar(1.2);
    Scalar alpha = Scalar(0.45);
    Scalar lj1 = Scalar(4.0) * epsilon * pow(sigma,Scalar(12.0));
    Scalar lj2 = alpha * Scalar(4.0) * epsilon * pow(sigma,Scalar(6.0));
    
    // specify the force parameters
    fc1->setParams(0,0,make_scalar2(lj1,lj2));
    fc2->setParams(0,0,make_scalar2(lj1,lj2));
    
    // compute the forces
    fc1->compute(0);
    fc2->compute(0);
    
    // verify that the forces are identical (within roundoff errors)
    ForceDataArrays arrays1 = fc1->acquire();
    ForceDataArrays arrays2 = fc2->acquire();
    
    // compare average deviation between the two computes
    double deltaf2 = 0.0;
    double deltape2 = 0.0;
    double deltav2 = 0.0;
        
    for (unsigned int i = 0; i < N; i++)
        {
        deltaf2 += double(arrays1.fx[i] - arrays2.fx[i]) * double(arrays1.fx[i] - arrays2.fx[i]);
        deltaf2 += double(arrays1.fy[i] - arrays2.fy[i]) * double(arrays1.fy[i] - arrays2.fy[i]);
        deltaf2 += double(arrays1.fz[i] - arrays2.fz[i]) * double(arrays1.fz[i] - arrays2.fz[i]);
        deltape2 += double(arrays1.pe[i] - arrays2.pe[i]) * double(arrays1.pe[i] - arrays2.pe[i]);
        deltav2 += double(arrays1.virial[i] - arrays2.virial[i]) * double(arrays1.virial[i] - arrays2.virial[i]);

        // also check that each individual calculation is somewhat close
        BOOST_CHECK_CLOSE(arrays1.fx[i], arrays2.fx[i], loose_tol);
        BOOST_CHECK_CLOSE(arrays1.fy[i], arrays2.fy[i], loose_tol);
        BOOST_CHECK_CLOSE(arrays1.fz[i], arrays2.fz[i], loose_tol);
        BOOST_CHECK_CLOSE(arrays1.pe[i], arrays2.pe[i], loose_tol);
        BOOST_CHECK_CLOSE(arrays1.virial[i], arrays2.virial[i], loose_tol);
        }
    deltaf2 /= double(pdata->getN());
    deltape2 /= double(pdata->getN());
    deltav2 /= double(pdata->getN());
    BOOST_CHECK_SMALL(deltaf2, double(tol_small));
    BOOST_CHECK_SMALL(deltape2, double(tol_small));
    BOOST_CHECK_SMALL(deltav2, double(tol_small));
    }

//! Test the ability of the lj force compute to compute forces with different shift modes
void lj_force_shift_test(ljforce_creator lj_creator, boost::shared_ptr<ExecutionConfiguration> exec_conf)
    {
    // this 2-particle test is just to get a plot of the potential and force vs r cut
    shared_ptr<SystemDefinition> sysdef_2(new SystemDefinition(2, BoxDim(1000.0), 1, 0, 0, 0, 0, exec_conf));
    shared_ptr<ParticleData> pdata_2 = sysdef_2->getParticleData();
    
    ParticleDataArrays arrays = pdata_2->acquireReadWrite();
    arrays.x[0] = arrays.y[0] = arrays.z[0] = 0.0;
    arrays.x[1] = Scalar(2.8); arrays.y[1] = arrays.z[1] = 0.0;
    pdata_2->release();
    shared_ptr<NeighborList> nlist_2(new NeighborList(sysdef_2, Scalar(3.0), Scalar(0.8)));
    shared_ptr<PotentialPairLJ> fc_no_shift = lj_creator(sysdef_2, nlist_2);
    fc_no_shift->setRcut(0, 0, Scalar(3.0));
    fc_no_shift->setShiftMode(PotentialPairLJ::no_shift);
    
    shared_ptr<PotentialPairLJ> fc_shift = lj_creator(sysdef_2, nlist_2);
    fc_shift->setRcut(0, 0, Scalar(3.0));
    fc_shift->setShiftMode(PotentialPairLJ::shift);
    
    shared_ptr<PotentialPairLJ> fc_xplor = lj_creator(sysdef_2, nlist_2);
    fc_xplor->setRcut(0, 0, Scalar(3.0));
    fc_xplor->setShiftMode(PotentialPairLJ::xplor);
    fc_xplor->setRon(0, 0, Scalar(2.0));
    
    nlist_2->setStorageMode(NeighborList::full);
    
    // setup a standard epsilon and sigma
    Scalar epsilon = Scalar(1.0);
    Scalar sigma = Scalar(1.0);
    Scalar alpha = Scalar(1.0);
    Scalar lj1 = Scalar(4.0) * epsilon * pow(sigma,Scalar(12.0));
    Scalar lj2 = alpha * Scalar(4.0) * epsilon * pow(sigma,Scalar(6.0));
    fc_no_shift->setParams(0,0,make_scalar2(lj1,lj2));
    fc_shift->setParams(0,0,make_scalar2(lj1,lj2));
    fc_xplor->setParams(0,0,make_scalar2(lj1,lj2));
    
    fc_no_shift->compute(0);
    fc_shift->compute(0);
    fc_xplor->compute(0);
    
    GPUArray<Scalar4>& force_array_5 =  fc_no_shift->getForceArray();
    GPUArray<Scalar>& virial_array_5 =  fc_no_shift->getVirialArray();
    ArrayHandle<Scalar4> h_force_5(force_array_5,access_location::host,access_mode::read);
    ArrayHandle<Scalar> h_virial_5(virial_array_5,access_location::host,access_mode::read);
    
    MY_BOOST_CHECK_CLOSE(h_force_5.data[0].x, 0.017713272731914, tol);
    MY_BOOST_CHECK_CLOSE(h_force_5.data[0].w, -0.0041417095577326, tol);
    MY_BOOST_CHECK_CLOSE(h_force_5.data[1].x, -0.017713272731914, tol);
    MY_BOOST_CHECK_CLOSE(h_force_5.data[1].w, -0.0041417095577326, tol);
    
    // shifted just has pe shifted by a given amount
    GPUArray<Scalar4>& force_array_6 =  fc_shift->getForceArray();
    GPUArray<Scalar>& virial_array_6 =  fc_shift->getVirialArray();
    ArrayHandle<Scalar4> h_force_6(force_array_6,access_location::host,access_mode::read);
    ArrayHandle<Scalar> h_virial_6(virial_array_6,access_location::host,access_mode::read);
    MY_BOOST_CHECK_CLOSE(h_force_6.data[0].x, 0.017713272731914, tol);
    MY_BOOST_CHECK_CLOSE(h_force_6.data[0].w, -0.0014019886856134, tol);
    MY_BOOST_CHECK_CLOSE(h_force_6.data[1].x, -0.017713272731914, tol);
    MY_BOOST_CHECK_CLOSE(h_force_6.data[1].w, -0.0014019886856134, tol);
    
    // xplor has slight tweaks
    GPUArray<Scalar4>& force_array_7 =  fc_xplor->getForceArray();
    GPUArray<Scalar>& virial_array_7 =  fc_xplor->getVirialArray();
    ArrayHandle<Scalar4> h_force_7(force_array_7,access_location::host,access_mode::read);
    ArrayHandle<Scalar> h_virial_7(virial_array_7,access_location::host,access_mode::read);
    MY_BOOST_CHECK_CLOSE(h_force_7.data[0].x, 0.012335911924312, tol);
    MY_BOOST_CHECK_CLOSE(h_force_7.data[0].w, -0.001130667359194/2.0, tol);
    MY_BOOST_CHECK_CLOSE(h_force_7.data[1].x, -0.012335911924312, tol);
    MY_BOOST_CHECK_CLOSE(h_force_7.data[1].w, -0.001130667359194/2.0, tol);
    
    // check again, prior to r_on to make sure xplor isn't doing something weird
    arrays = pdata_2->acquireReadWrite();
    arrays.x[0] = arrays.y[0] = arrays.z[0] = 0.0;
    arrays.x[1] = Scalar(1.5); arrays.y[1] = arrays.z[1] = 0.0;
    pdata_2->release();
    
    fc_no_shift->compute(1);
    fc_shift->compute(1);
    fc_xplor->compute(1);
    
    GPUArray<Scalar4>& force_array_8 =  fc_no_shift->getForceArray();
    GPUArray<Scalar>& virial_array_8 =  fc_no_shift->getVirialArray();
    ArrayHandle<Scalar4> h_force_8(force_array_8,access_location::host,access_mode::read);
    ArrayHandle<Scalar> h_virial_8(virial_array_8,access_location::host,access_mode::read);
    
    MY_BOOST_CHECK_CLOSE(h_force_8.data[0].x, 1.1580288310461, tol);
    MY_BOOST_CHECK_CLOSE(h_force_8.data[0].w, -0.16016829713928, tol);
    MY_BOOST_CHECK_CLOSE(h_force_8.data[1].x, -1.1580288310461, tol);
    MY_BOOST_CHECK_CLOSE(h_force_8.data[1].w, -0.16016829713928, tol);
    
    // shifted just has pe shifted by a given amount
    GPUArray<Scalar4>& force_array_9 =  fc_shift->getForceArray();
    GPUArray<Scalar>& virial_array_9 =  fc_shift->getVirialArray();
    ArrayHandle<Scalar4> h_force_9(force_array_9,access_location::host,access_mode::read);
    ArrayHandle<Scalar> h_virial_9(virial_array_9,access_location::host,access_mode::read);
    MY_BOOST_CHECK_CLOSE(h_force_9.data[0].x, 1.1580288310461, tol);
    MY_BOOST_CHECK_CLOSE(h_force_9.data[0].w, -0.15742857626716, tol);
    MY_BOOST_CHECK_CLOSE(h_force_9.data[1].x, -1.1580288310461, tol);
    MY_BOOST_CHECK_CLOSE(h_force_9.data[1].w, -0.15742857626716, tol);
    
    // xplor has slight tweaks
    GPUArray<Scalar4>& force_array_10 =  fc_xplor->getForceArray();
    GPUArray<Scalar>& virial_array_10 =  fc_xplor->getVirialArray();
    ArrayHandle<Scalar4> h_force_10(force_array_10,access_location::host,access_mode::read);
    ArrayHandle<Scalar> h_virial_10(virial_array_10,access_location::host,access_mode::read);
    MY_BOOST_CHECK_CLOSE(h_force_10.data[0].x, 1.1580288310461, tol);
    MY_BOOST_CHECK_CLOSE(h_force_10.data[0].w, -0.16016829713928, tol);
    MY_BOOST_CHECK_CLOSE(h_force_10.data[1].x, -1.1580288310461, tol);
    MY_BOOST_CHECK_CLOSE(h_force_10.data[1].w, -0.16016829713928, tol);
    
    // check once again to verify that nothing fish happens past r_cut
    arrays = pdata_2->acquireReadWrite();
    arrays.x[0] = arrays.y[0] = arrays.z[0] = 0.0;
    arrays.x[1] = Scalar(3.1); arrays.y[1] = arrays.z[1] = 0.0;
    pdata_2->release();
    
    fc_no_shift->compute(2);
    fc_shift->compute(2);
    fc_xplor->compute(2);
    
    GPUArray<Scalar4>& force_array_11 =  fc_no_shift->getForceArray();
    GPUArray<Scalar>& virial_array_11 =  fc_no_shift->getVirialArray();
    ArrayHandle<Scalar4> h_force_11(force_array_11,access_location::host,access_mode::read);
    ArrayHandle<Scalar> h_virial_11(virial_array_11,access_location::host,access_mode::read);
    
    MY_BOOST_CHECK_SMALL(h_force_11.data[0].x, tol_small);
    MY_BOOST_CHECK_SMALL(h_force_11.data[0].w, tol_small);
    MY_BOOST_CHECK_SMALL(h_force_11.data[1].x, tol_small);
    MY_BOOST_CHECK_SMALL(h_force_11.data[1].w, tol_small);
    
    // shifted just has pe shifted by a given amount
    GPUArray<Scalar4>& force_array_12 =  fc_shift->getForceArray();
    GPUArray<Scalar>& virial_array_12 =  fc_shift->getVirialArray();
    ArrayHandle<Scalar4> h_force_12(force_array_12,access_location::host,access_mode::read);
    ArrayHandle<Scalar> h_virial_12(virial_array_12,access_location::host,access_mode::read);
    MY_BOOST_CHECK_SMALL(h_force_12.data[0].x, tol_small);
    MY_BOOST_CHECK_SMALL(h_force_12.data[0].w, tol_small);
    MY_BOOST_CHECK_SMALL(h_force_12.data[1].x, tol_small);
    MY_BOOST_CHECK_SMALL(h_force_12.data[1].w, tol_small);
    
    // xplor has slight tweaks
    GPUArray<Scalar4>& force_array_13 =  fc_xplor->getForceArray();
    GPUArray<Scalar>& virial_array_13 =  fc_xplor->getVirialArray();
    ArrayHandle<Scalar4> h_force_13(force_array_13,access_location::host,access_mode::read);
    ArrayHandle<Scalar> h_virial_13(virial_array_13,access_location::host,access_mode::read);
    MY_BOOST_CHECK_SMALL(h_force_13.data[0].x, tol_small);
    MY_BOOST_CHECK_SMALL(h_force_13.data[0].w, tol_small);
    MY_BOOST_CHECK_SMALL(h_force_13.data[1].x, tol_small);
    MY_BOOST_CHECK_SMALL(h_force_13.data[1].w, tol_small);
    }

//! LJForceCompute creator for unit tests
shared_ptr<PotentialPairLJ> base_class_lj_creator(shared_ptr<SystemDefinition> sysdef,
                                                  shared_ptr<NeighborList> nlist)
    {
    return shared_ptr<PotentialPairLJ>(new PotentialPairLJ(sysdef, nlist));
    }

#ifdef ENABLE_CUDA
//! LJForceComputeGPU creator for unit tests
shared_ptr<PotentialPairLJGPU> gpu_lj_creator(shared_ptr<SystemDefinition> sysdef,
                                          shared_ptr<NeighborList> nlist)
    {
    nlist->setStorageMode(NeighborList::full);
    shared_ptr<PotentialPairLJGPU> lj(new PotentialPairLJGPU(sysdef, nlist));
    // the default block size kills valgrind :) reduce it
    lj->setBlockSize(64);
    return lj;
    }
#endif

//! boost test case for particle test on CPU
BOOST_AUTO_TEST_CASE( PotentialPairLJ_particle )
    {
    ljforce_creator lj_creator_base = bind(base_class_lj_creator, _1, _2);
    lj_force_particle_test(lj_creator_base, boost::shared_ptr<ExecutionConfiguration>(new ExecutionConfiguration(ExecutionConfiguration::CPU)));
    }

//! boost test case for periodic test on CPU
BOOST_AUTO_TEST_CASE( PotentialPairLJ_periodic )
    {
    ljforce_creator lj_creator_base = bind(base_class_lj_creator, _1, _2);
    lj_force_periodic_test(lj_creator_base, boost::shared_ptr<ExecutionConfiguration>(new ExecutionConfiguration(ExecutionConfiguration::CPU)));
    }

//! boost test case for particle test on CPU
BOOST_AUTO_TEST_CASE( PotentialPairLJ_shift )
    {
    ljforce_creator lj_creator_base = bind(base_class_lj_creator, _1, _2);
    lj_force_shift_test(lj_creator_base, boost::shared_ptr<ExecutionConfiguration>(new ExecutionConfiguration(ExecutionConfiguration::CPU)));
    }

# ifdef ENABLE_CUDA
//! boost test case for particle test on GPU
BOOST_AUTO_TEST_CASE( LJForceGPU_particle )
    {
    ljforce_creator lj_creator_gpu = bind(gpu_lj_creator, _1, _2);
    lj_force_particle_test(lj_creator_gpu, boost::shared_ptr<ExecutionConfiguration>(new ExecutionConfiguration(ExecutionConfiguration::GPU)));
    }

//! boost test case for periodic test on the GPU
BOOST_AUTO_TEST_CASE( LJForceGPU_periodic )
    {
    ljforce_creator lj_creator_gpu = bind(gpu_lj_creator, _1, _2);
    lj_force_periodic_test(lj_creator_gpu, boost::shared_ptr<ExecutionConfiguration>(new ExecutionConfiguration(ExecutionConfiguration::GPU)));
    }

//! boost test case for shift test on GPU
BOOST_AUTO_TEST_CASE( LJForceGPU_shift )
    {
    ljforce_creator lj_creator_gpu = bind(gpu_lj_creator, _1, _2);
    lj_force_shift_test(lj_creator_gpu, boost::shared_ptr<ExecutionConfiguration>(new ExecutionConfiguration(ExecutionConfiguration::GPU)));
    }

//! boost test case for comparing GPU output to base class output
/*BOOST_AUTO_TEST_CASE( LJForceGPU_compare )
    {
    ljforce_creator lj_creator_gpu = bind(gpu_lj_creator, _1, _2);
    ljforce_creator lj_creator_base = bind(base_class_lj_creator, _1, _2);
    lj_force_comparison_test(lj_creator_base, lj_creator_gpu, boost::shared_ptr<ExecutionConfiguration>(new ExecutionConfiguration(ExecutionConfiguration::GPU)));
    }*/

#endif

#ifdef WIN32
#pragma warning( pop )
#endif


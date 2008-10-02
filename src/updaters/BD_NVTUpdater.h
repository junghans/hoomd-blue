/*
Highly Optimized Object-Oriented Molecular Dynamics (HOOMD) Open
Source Software License
Copyright (c) 2008 Ames Laboratory Iowa State University
All rights reserved.

Redistribution and use of HOOMD, in source and binary forms, with or
without modification, are permitted, provided that the following
conditions are met:

* Redistributions of source code must retain the above copyright notice,
this list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in the
documentation and/or other materials provided with the distribution.

* Neither the name of the copyright holder nor the names HOOMD's
contributors may be used to endorse or promote products derived from this
software without specific prior written permission.

Disclaimer

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER AND
CONTRIBUTORS ``AS IS''  AND ANY EXPRESS OR IMPLIED WARRANTIES,
INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. 

IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS  BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
THE POSSIBILITY OF SUCH DAMAGE.
*/

// $Id: BD_NVTUpdater.h 1085 2008-07-30 20:22:24Z joaander $
// $URL: http://svn2.assembla.com/svn/hoomd/trunk/src/updaters/BD_NVTUpdater.h $

/*! \file BD_NVTUpdater.h
	\brief Declares an updater that implements BD_NVT dynamics
*/

#include "Updater.h"
#include "Integrator.h"
#include "StochasticForceCompute.h"
#include <vector>
#include <boost/shared_ptr.hpp>

#ifndef __BD_NVTUPDATER_H__
#define __BD_NVTUPDATER_H__

//! Updates particle positions and velocities
/*! This updater performes constant N, constant volume, constant energy (BD_NVT) dynamics. Particle positions and velocities are 
	updated according to the velocity verlet algorithm. The forces that drive this motion are defined external to this class
	in ForceCompute. Any number of ForceComputes can be given, the resulting forces will be summed to produce a net force on 
	each particle.
	
	\ingroup updaters
*/
class BD_NVTUpdater : public Integrator
	{
	public:
		//! Constructor
		BD_NVTUpdater(boost::shared_ptr<ParticleData> pdata, Scalar deltaT, Scalar Temp);
		
		//! Resets the Stochastic Bath Temperature
		void setT(Scalar Temp); 

		//! Resets the Stochastic Bath Temperature
		void setGamma(unsigned int type, Scalar gamma) {assert(m_bdfc); m_bdfc->setParams(type,gamma);} 
				
		//! Sets the movement limit
		void setLimit(Scalar limit);
		
		//! Removes the limit
		void removeLimit();
		
		//! Take one timestep forward
		virtual void update(unsigned int timestep);
		
		//! Returns a list of log quantities this compute calculates
		virtual std::vector< std::string > getProvidedLogQuantities(); 
		
		//! Calculates the requested log value and returns it
		virtual Scalar getLogValue(const std::string& quantity);
		
	protected:
		bool m_accel_set;	//!< Flag to tell if we have set the accelleration yet
		bool m_limit;		//!< True if we should limit the distance a particle moves in one step
		Scalar m_limit_val;	//!< The maximum distance a particle is to move in one step
		Scalar m_T;			//!< The Temperature of the Stochastic Bath
		
		void computeBDAccelerations(unsigned int timestep, const std::string& profile_name);  
		boost::shared_ptr<StochasticForceCompute> m_bdfc; 
		
		#ifdef USE_CUDA 
		// NOT WRITTEN YET
		//! helper function to compute accelerations on the GPU
		//void computeBDAccelerationsGPU(unsigned int timestep, const std::string& profile_name, bool sum_accel);

		//! Force data pointers on the device
		//vector<float4 **> m_d_force_data_ptrs;

		#endif

	};
	
#ifdef USE_PYTHON
//! Exports the BD_NVTUpdater class to python
void export_BD_NVTUpdater();
#endif
	
#endif
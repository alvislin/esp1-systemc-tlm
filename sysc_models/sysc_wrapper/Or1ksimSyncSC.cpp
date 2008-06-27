// ----------------------------------------------------------------------------

// Example Programs for "Building a Loosely Timed SoC Model with OSCI TLM 2.0"

// Copyright (C) 2008  Embecosm Limited

// This program is free software: you can redistribute it and/or modify it
// under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or (at your
// option) any later version.

// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
// License for more details.

// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

// ----------------------------------------------------------------------------

// Implementation of main SystemC wrapper for the OSCI SystemC wrapper project
// with SystemC time synchronization

// $Id$


#include "Or1ksimSyncSC.h"


//! Custom constructor for the Or1ksimSyncSC SystemC module

//! This calls the constructor of the base class, Or1ksimExtSC::. Then sets an
//! initial time point in the underlying ISS.

//! @param name        SystemC module name
//! @param configFile  Config file for the underlying ISS
//! @param imageFile   Binary image to run on the ISS

Or1ksimSyncSC::Or1ksimSyncSC ( sc_core::sc_module_name  name,
		       const char              *configFile,
		       const char              *imageFile ) :
  Or1ksimExtSC( name, configFile, imageFile )
{
  or1ksim_set_time_point();		// Mark the start time

}	/* Or1ksimSyncSC() */


//! Synchronized TLM transport to the target

//! The module synchronizes with SystemC for the time consumed by the ISS,
//! then calls the blocking transport routine for the initiator socket (@see
//! Or1ksimSC::dataBus). SystemC time is adjusted for any delay due to the
//! external entity.

//! @param trans  The transaction payload

void
Or1ksimSyncSC::doTrans( tlm::tlm_generic_payload &trans )
{
  // Synchronize with SystemC for the amount of time that the ISS has used
  // since the last upcall.
  wait( sc_core::sc_time( or1ksim_get_time_period(), sc_core::SC_SEC ) );

  // Call the transport. Since this is a synchronous model, the target should
  // have synchronized, and no additional delay be added on return.

  sc_core::sc_time  delay = sc_core::SC_ZERO_TIME;
  dataBus->b_transport( trans, delay );
  or1ksim_set_time_point();		// Mark start of new time point in ISS

}	// doTrans()


//! Get the clock rate of the underlying Or1ksim ISS

//! Public utility to allow other modules to identify the ISS clock rate

//! @return  The clock rate in Hz of the underlying Or1ksim ISS

unsigned long int
Or1ksimSyncSC::getClockRate()
{
  return or1ksim_clock_rate();

}	// getClockRate()


// EOF

// ----------------------------------------------------------------------------

//                  CONFIDENTIAL AND PROPRIETARY INFORMATION
//                  ========================================

// Unpublished copyright (c) 2008 Embecosm. All Rights Reserved.

// This file contains confidential and proprietary information of Embecosm and
// is protected by copyright, trade secret and other regional, national and
// international laws, and may be embodied in patents issued or pending.

// Receipt or possession of this file does not convey any rights to use,
// reproduce, disclose its contents, or to manufacture, or sell anything it may
// describe.

// Reproduction, disclosure or use without specific written authorization of
// Embecosm is strictly forbidden.

// Reverse engineering is prohibited.

// ----------------------------------------------------------------------------

// Implementation of 16450 UART Synchronous SystemC module.

// $Id$


#include <iostream>
#include <iomanip>
#include <stdio.h>

#include "UartSyncSC.h"


SC_HAS_PROCESS( UartSyncSC );

//! Custom constructor for the UART module

//! Passes the name to the parent constructor. Sets the external clock rate
//! (UartSyncSC::clockRate) and model endianism (UartSyncSC::isLittleEndian).

//! Sets up threads listening to the bus (UartSyncSC::busThread()) and the Rx pin
//! (UartSyncSC::rxThread()).

//! Registers UartSyncSC::busReadWrite() as the callback for blocking transport on
//! the UartSyncSC::bus socket.

//! Zeros the registers, but leaves the UartSyncSC::divLatch and UartSyncSC::charDelay
//! unset - they are undefined until used.

//! @param name             The SystemC module name, passed to the parent
//!                         constructor
//! @param _clockRate       The external clock rate
//! @param _isLittleEndian  The model endianism

UartSyncSC::UartSyncSC( sc_core::sc_module_name  name,
		unsigned long int        _clockRate,
		bool                     _isLittleEndian ) :
  sc_module( name ),
  clockRate( _clockRate ),
  isLittleEndian( _isLittleEndian )
{
  // Set up the two threads

  SC_THREAD( busThread );
  SC_THREAD( rxThread );

  // Register the blocking transport method

  bus.register_b_transport( this, &UartSyncSC::busReadWrite );

  // Initialize the Uart. Clear regs. Other internal state (divLatch,
  // charDelay) is undefined until set.

  bzero( (void *)&regs, sizeof( regs ));

}	/* UartSyncSC() */


//! SystemC thread listening for transmit traffic on the bus

//! Sits in a loop. Initially sets the line status register to indicate the
//! buffer is empty (reset will clear these bits) and sends an interrupt (if
//! enabled) to indicate the buffer is empty.

//! Then waits for the UartSyncSC::txReceived event to be triggered (this happens
//! when new data is written into the transmit buffer by a bus write).

//! On receipt of a character, waits for UartSyncSC::charDelay to simulate the time
//! taken to put the bits on the wire. Then writes the char onto the Tx FIFO.

void
UartSyncSC::busThread()
{
  // Loop listening for changes on the Tx buffer, waiting for a baud rate
  // delay then sending to the terminal

  while( true ) {

    set( regs.lsr, UART_LSR_THRE | UART_LSR_TEMT );  // Indicate buffer empty

    if( isSet( regs.ier, UART_IER_ETBEI ) ) {	// Send interrupt if enabled
      genInt( UART_IIR_THRE );
    }

    wait( txReceived );				// Wait for a Tx request
    wait( charDelay );				// Wait baud delay

    tx.write( regs.thr );			// Send char to terminal
  }
}	// busThread()


//! SystemC thread listening for data on the Rx fifo

//! Waits for a character to appear on the Rx fifo and copies into the read
//! buffer.

//! Sets the data ready flag of the line status register and sends an
//! interrupt (if enabled) to indicate the data is ready.

//! @note The terminal attached to the FIFO is responsible for modeling any
//!       wire delay on the Rx.

void
UartSyncSC::rxThread()
{
  // Loop woken up when a character is written into the fifo from the terminal.

  while( true ) {
    regs.rbr  = rx.read();			// Blocking read of the data
    set( regs.lsr, UART_LSR_DR );		// Mark data ready

    if( isSet( regs.ier, UART_IER_ERBFI ) ) {	// Send interrupt if enabled
      genInt( UART_IIR_RDI );
    }
  }
}	// rxThread()


//! TLM2.0 blocking transport routine for the UART bus socket

//! Receives transport requests on the target socket.

//! Break out the address, data and byte enable mask. Use the byte enable mask
//! to identify the byte address. Allow for model endianism in calculating
//! this (see UartSyncSC::isLittleEndian).

//! Switches on the command and calls UartSyncSC::busRead() or UartSyncSC::buswrite()
//! routines to do the behavior

//! Increases the delay as appropriate and sets a success response.

//! @param payload    The transaction payload
//! @param delayTime  How far the initiator is beyond baseline SystemC
//!                   time. For use with temporal decoupling.

void
UartSyncSC::busReadWrite( tlm::tlm_generic_payload &payload,
		      sc_core::sc_time         &delay )
{
  // Break out the address, mask and data pointer. This should be only a
  // single byte access.

  sc_dt::uint64      addr    = payload.get_address();
  unsigned char     *maskPtr = payload.get_byte_enable_ptr();
  unsigned char     *dataPtr = payload.get_data_ptr();

  int                offset;		// Data byte offset in word
  unsigned char      uaddr;		// UART address

  // Deduce the byte address, allowing for endianism of the ISS

  switch( *((uint32_t *)maskPtr) ) {
  case 0x000000ff: offset = isLittleEndian ? 0 : 3; break;
  case 0x0000ff00: offset = isLittleEndian ? 1 : 2; break;
  case 0x00ff0000: offset = isLittleEndian ? 2 : 1; break;
  case 0xff000000: offset = isLittleEndian ? 3 : 0; break;

  default:		// Invalid request

    std::cerr << "Uart: multiple byte bus access - ignored\n" << std::endl;
    payload.set_response_status( tlm::TLM_GENERIC_ERROR_RESPONSE );
    return;
  }

  // Mask off the address to its range. This ought to have been done already
  // by an arbiter/decoder.

  uaddr = (unsigned char)((addr + offset) & UART_ADDR_MASK);

  // Which command?

  switch( payload.get_command() ) {

  case tlm::TLM_READ_COMMAND:

    dataPtr[offset] = busRead( uaddr );
    delay += sc_core::sc_time( UART_READ_NS, sc_core::SC_NS );
    break;

  case tlm::TLM_WRITE_COMMAND:

    busWrite( uaddr, dataPtr[offset] );
    delay += sc_core::sc_time( UART_WRITE_NS, sc_core::SC_NS );
    break;

  case tlm::TLM_IGNORE_COMMAND:

    std::cerr << "UartSyncSC: Unexpected TLM_IGNORE_COMMAND" << std::endl;
    payload.set_response_status( tlm::TLM_GENERIC_ERROR_RESPONSE );
    return;
  }

  // Single byte accesses always work

  payload.set_response_status( tlm::TLM_OK_RESPONSE );

}	// busReadWrite()


//! Process a read on the UART bus

//! Switch on the address to determine behavior
//! - UART_BUF
//!   - if UART_LSR_DLAB is set, read the low byte of the clock divisor
//!   - otherwise get the value from the read buffer and
//!     clear the data ready flag in the line status register
//! - UART_IER
//!   - if UART_LSR_DLAB is set, read the high byte of the clock divisor
//!   - otherwise get the instruction enable register
//! - UART_IIR Get the interrupt indicator register and
//!   clear the most important pending interrupt (UartSyncSC::clearInt())
//! - UART_LCR Get the line control register
//! - UART_MCR Ignored - write only register
//! - UART_LSR Get the line status register
//! - UART_MSR Get the modem status registe
//! - UART_SCR Get the scratch register

//! @param uaddr  The address of the register being accessed

//! @return  The value read

unsigned char
UartSyncSC::busRead( unsigned char  uaddr )
{
  unsigned char  res;		// The result to return

  // State machine lookup on the register

  switch( uaddr ) {

  case UART_BUF:

    if( isSet(regs.lcr, UART_LCR_DLAB ) ) {	// DLL byte
      res = (unsigned char)(divLatch & 0x00ff);
    }
    else {
      res = regs.rbr;				// Get the read data
      clr( regs.lsr, UART_LSR_DR );		// Clear the data ready bit
    }

    break;

  case UART_IER:

    if( isSet( regs.lcr, UART_LCR_DLAB ) ) {	// DLH byte
      res = (unsigned char)((divLatch & 0xff00) >> 8);
    }
    else {
      res = regs.ier;
    }

    break;

  case UART_IIR:

    res = regs.iir;
    clearInt();				// Clear highest priority interrupt
    break;

  case UART_LCR: res = regs.lcr; break;
  case UART_MCR: res = 0;        break;	// Write only
  case UART_LSR: res = regs.lsr; break;
  case UART_MSR: res = regs.msr; break;
  case UART_SCR: res = regs.scr; break;
  }

  return res;

}	// busRead()


//! Process a write on the UART bus

//! Switch on the address to determine behavior
//! - UART_BUF
//!   - if UART_LSR_DLAB is set, write the low byte of the clock divisor and
//!     recalculate the character delay (UartSyncSC::resetCharDelay())
//!   - otherwise write the data to the transmit buffer, clear the buffer
//!     empty flags and notify the UartSyncSC::busThread() using the
//!     UartSyncSC::txReceived SystemC event.
//! - UART_IER
//!   - if UART_LSR_DLAB is set, write the high byte of the clock divisor and
//!     recalculate the character delay (UartSyncSC::resetCharDelay())
//!   - otherwise set the instruction enable register
//! - UART_IIR Ignored - read only
//! - UART_LCR Set the line control register and recalculate the character
//!   delay (UartSyncSC::resetCharDelay()), since this may have been affected
//! - UART_MCR Set the modem control regsiter
//! - UART_LSR Ignored - read only
//! - UART_MSR Ignored - read only
//! - UART_SCR Set the scratch register

//! @param uaddr  The address of the register being accessed
//! @param wdata  The value to be written

void
UartSyncSC::busWrite( unsigned char  uaddr,
		  unsigned char  wdata )
{
  // State machine lookup on the register

  switch( uaddr ) {

  case UART_BUF:

    if( isSet( regs.lcr, UART_LCR_DLAB ) ) {	// DLL
      divLatch = (divLatch & 0xff00) | (unsigned short int)wdata;
      resetCharDelay();
    }
    else {
      regs.thr = wdata;

      clr( regs.lsr, UART_LSR_TEMT );		// Tx buffer now full
      clr( regs.lsr, UART_LSR_THRE );

      txReceived.notify();			// Tell the bus thread
    }

    break;

  case UART_IER:

    if( isSet( regs.lcr, UART_LCR_DLAB ) ) {	// DLH
      divLatch = (divLatch & 0x00ff) | ((unsigned short int)wdata << 8);
      resetCharDelay();
    }
    else {
      regs.ier = wdata;
    }

    break;

  case UART_IIR:                   break;	// Read only
  case UART_LCR:

    regs.lcr = wdata;
    resetCharDelay();				// Could change baud delay
    break;

  case UART_MCR: regs.mcr = wdata; break;
  case UART_LSR:                   break;	// Read only
  case UART_MSR:                   break;	// Read only
  case UART_SCR: regs.scr = wdata; break;
  }

}	// busWrite()


//! Generate an interrupt

//! Set the relevant interrupt indicator flag, mark an interrupt as pending
//! and write logic high (bool true) to UartSyncSC::intr

//! @param iflag  The interrupt indicator register flag corresponding to the
//!               interrupt being generated.

void
UartSyncSC::genInt( unsigned char  iflag )
{
  set( regs.iir, iflag );
  set( regs.iir, UART_IIR_IPEND );

  intr.write( true );

}	// genInt()


//! Clear an interrupt

//! Clear the highest priority interrupt (Received Data Ready is higher than
//! Transmitter Holding Register Empty)

//! If no interrupts remain asserted, clear the interrupt pending flag and
//! deassert the interrupt by writing logic low (bool false) to UartSyncSC::intr.

void
UartSyncSC::clearInt()
{
  if( isClr( regs.ier, UART_IER_ERBFI | UART_IER_ETBEI ) ) {
    return;					// No ints enabled => do nothing
  }

  if( isSet( regs.ier, UART_IER_ERBFI ) &&
      isClr( regs.ier, UART_IER_ETBEI ) ) {

    clr( regs.iir, UART_IIR_RDI | UART_IIR_IPEND );	// Just RDI => clear it
    intr.write( false );
    return;
  }

  if( isClr( regs.ier, UART_IER_ERBFI ) &&
      isSet( regs.ier, UART_IER_ETBEI ) ) {

    clr( regs.iir, UART_IIR_THRE | UART_IIR_IPEND );	// Just THRE => clear it
    intr.write( false );
    return;
  }

  // Both enabled. Try highest priority first

  if( isSet( regs.iir, UART_IIR_RDI ) ) {
    clr( regs.iir, UART_IIR_RDI );		// Clear RDI
  }
  else {
    clr( regs.iir, UART_IIR_THRE );		// Clear THRE
  }

  if( isClr( regs.iir, UART_IIR_RDI | UART_IIR_THRE ) ) {
    clr( regs.iir, UART_IIR_IPEND );		// None left => clear pending
    intr.write( false );			// Deassert
  }
}	// clearInt()


//! Recalculate the character delay

//! Recalculate Uart::charDelay after a change to the divisor latch. The
//! divisor latch is for a 16x clock. Allow enough time for start (1), stop
//! (1, 1.5 or 2), data (5-7) and parity (0-1) bits.

void
UartSyncSC::resetCharDelay()
{
  if( clockRate > 0 ) {		// Avoid divide by zero!

    int  dataBits  = regs.lcr & UART_LCR_MASK + 5;
    int  parityBit = isSet( regs.lcr, UART_LCR_PEN ) ? 1 : 0;
    int  stopBits  = isSet( regs.lcr, UART_LCR_STB ) ? 2 : 1;

    // 5 bit data has max 1.5 stop bits

    double totBits = (double)(1 + dataBits + parityBit + stopBits) -
                     ((5 == dataBits) && (1 == stopBits)) ? 0.5 : 0.0;
    double baudRate = (double)divLatch / 16.0 / (double)clockRate;

    charDelay = sc_core::sc_time( totBits * baudRate, sc_core::SC_SEC );
  }
}	// resetCharDelay()


//! Set a bits in a register

//! @param reg    The register concerned
//! @param flags  The bits to set

inline void
UartSyncSC::set( unsigned char &reg,
	     unsigned char  flags )
{
  reg |= flags;

}	// set()


//! Clear a bits in a register

//! @param reg    The register concerned
//! @param flags  The bits to set

inline void
UartSyncSC::clr( unsigned char &reg,
	     unsigned char  flags )
{
  reg &= ~flags;

}	// clr()


//! Report if bits are set in a register

//! @param reg    The register concerned
//! @param flags  The bit to set

//! @return  True if the bit is set

inline bool
UartSyncSC::isSet( unsigned char  reg,
	       unsigned char  flags )
{
  return  flags == (reg & flags);

}	// isSet()


//! Report if bits are clear in a register

//! @param reg    The register concerned
//! @param flags  The bit to set

//! @return  True if the bit is clear

inline bool
UartSyncSC::isClr( unsigned char  reg,
	       unsigned char  flags )
{
  return  flags != (reg & flags);

}	// isClr()


// EOF

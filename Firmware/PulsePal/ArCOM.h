/*
----------------------------------------------------------------------------

This file is part of the Pulse Pal Project
Copyright (C) 2026 Sanworks LLC, Rochester, NY, USA

----------------------------------------------------------------------------

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, version 3.

This program is distributed  WITHOUT ANY WARRANTY and without even the
implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
See the GNU General Public License for more details.

*/

// ArCOM reads and writes the data types of the Pulse Pal USB protocol (see /Firmware/PROTOCOL.md).
// It was derived from the general purpose ArCOM library, and has since been tailored to this firmware.
//
// How it behaves:
//
// - Reads return as soon as the bytes have arrived. If no new byte arrives for READ_TIMEOUT_MS, the read gives up,
//   returns zeros and sets timedOut(). Every read after that returns immediately, so the rest of a truncated command
//   costs nothing. loop() checks timedOut() once per pass, reports the failure to the user and clears it.
//   The timeout measures the gap between bytes, not the length of a transfer, so a large custom pulse train is
//   never cut short by it.
// - Writes are collected in a buffer and sent by flush(), which loop() calls once per pass. One USB packet then
//   carries a whole reply, instead of one packet per value. Nothing is sent until flush() is called.
// - Arrays are read and written as one block. Both supported boards are little-endian, and that is also the wire
//   format, so the bytes of a uint16 or uint32 array need no rearranging.
// - readBlock() reads a fixed number of bytes into a buffer of the caller's. An op that reads a known set of values
//   can use it to take the whole set in one transfer, and unpack the buffer afterwards. Op 92 does this.
// - This class is only used from loop(). Do not call it from handler() or any other interrupt.
// - On Pulse Pal 2, it takes received USB packets off the USB controller itself (receiveUSB()), and reads SerialUSB's
//   receive buffer directly, because the Due core's own receive code loses packets (see below).

#ifndef ArCOM_h
#define ArCOM_h

#include "Arduino.h"

#ifndef HARDWARE_VERSION
  #error Include ArCOM.h after HARDWARE_VERSION has been defined
#endif

#if (HARDWARE_VERSION == 2)
  typedef Serial_ ArCOMPort; // SAM3X USB CDC class, the type of SerialUSB

  // Pulse Pal 2 receives USB data itself, instead of through the Due core's SerialUSB reads.
  //
  // The Due core (arduino:sam 1.6.12, cores/arduino/USB/CDC.cpp) can drop a whole packet sent by the PC. Its
  // Serial_::accept() moves received bytes from the USB controller into SerialUSB's buffer. Once the controller's
  // current bank is empty, it releases that bank again unconditionally, and a packet the controller stored there in
  // the meantime is freed unread. The core runs accept() from its USB interrupt, and SerialUSB.read() runs it from
  // loop(). The timer interrupt preempts both, for up to about 25us (see startHardwareTimer()), so a packet that
  // arrived while the one before it was still being taken was sometimes lost: with all four channels playing, 1.4% of
  // soft triggers (op 77) sent back to back, and op 93s among them, whose replies then never came. A command sent
  // after the reply to the one before was never hit, so the classes' usual traffic was safe, but not triggers or stops.
  //
  // receiveUSB() takes received packets off the controller instead, from loop(), into the same buffer. It releases a
  // bank only while it holds a whole packet (FIFOCON set): the controller never writes into a bank the CPU holds. The
  // core's USB interrupt no longer handles received packets, because receiveUSB() switches off the endpoint's receive
  // interrupt (the core switches it on whenever the PC configures the device). Bytes are read from the buffer here,
  // never with SerialUSB.read(), which would run accept().
  #define ARCOM_CDC_BUFFER_SIZE 512 // CDC_SERIAL_BUFFER_SIZE in the core's CDC.cpp
  struct ring_buffer { // SerialUSB's receive buffer, as the core's CDC.cpp defines it
    uint8_t buffer[ARCOM_CDC_BUFFER_SIZE];
    volatile uint32_t head; // Next byte to store
    volatile uint32_t tail; // Next byte to read. head == tail: empty; head one before tail: full
  };
  extern ring_buffer cdc_rx_buffer;

  // The Due core can also stop answering the PC altogether, after a program polls the device with the port closed.
  // Its USB interrupt reads each setup packet (the start of a control request, such as GET_DESCRIPTOR, or the
  // SET_LINE_CODING a program sends when it opens the port) at an offset into endpoint 0's FIFO window that only
  // grows: 8 bytes per request, reset only after a request that brings data from the PC, like SET_LINE_CODING. Programs
  // that list USB devices read their descriptors over and over (about 4 requests a second on one PC with the Arduino
  // IDE open), and with the port closed nothing resets the offset. After 4096 requests it passes the end of the window,
  // and from then on the core reads every request from the wrong memory and refuses it (STALL): the port could not be
  // opened again ("A device attached to the system is not functioning", on Windows) until the device restarted.
  // usbInterruptHook() runs before the core's USB interrupt, through the core's gpf_isr pointer, and resets the offset
  // with the core's UDD_ClearOUT() whenever a setup packet waits. A setup packet ends any request in progress, so the
  // OUT data that UDD_ClearOUT() also clears belongs to no request any more. setup() calls installUSBInterruptHook().
  extern "C" { extern void (*gpf_isr)(void); }
  static void (*coreUSBInterrupt)(void) = nullptr;

  static void usbInterruptHook(void) {
    if (UOTGHS->UOTGHS_DEVEPTISR[0] & UOTGHS_DEVEPTISR_RXSTPI) {
      UDD_ClearOUT(); // Endpoint 0's read offset back to the start of its FIFO, before the core reads the setup packet
    }
    coreUSBInterrupt();
  }

  static void installUSBInterruptHook() {
    noInterrupts();
    if (gpf_isr != usbInterruptHook) {
      coreUSBInterrupt = gpf_isr;
      gpf_isr = usbInterruptHook;
    }
    interrupts();
  }
#else
  typedef usb_serial_class ArCOMPort; // Teensy USB serial class, the type of Serial
#endif

#define ARCOM_READ_TIMEOUT_MS 100 // Longest gap between bytes of one command before the transfer is abandoned
#define ARCOM_WRITE_BUFFER_SIZE 256 // Must hold the largest reply, which is 178 bytes (ops 85 and 93)

class ArCOM
{
  public:
    ArCOM(ArCOMPort &port) : port(port) {}

    // Number of bytes waiting to be read
    unsigned int available() {
      #if (HARDWARE_VERSION == 2)
        receiveUSB();
      #endif
      return port.available();
    }

    // True if a read gave up waiting for data. Stays true until clearTimedOut() is called.
    bool timedOut() {
      return readTimedOut;
    }

    void clearTimedOut() {
      readTimedOut = false;
    }

    // --- Reads. Each returns 0 (or leaves zeros in the array) if the data does not arrive. ---

    byte readByte() {
      byte value = 0;
      readBlock(&value, 1);
      return value;
    }

    uint16_t readUint16() {
      uint16_t value = 0;
      readBlock(&value, 2);
      return value;
    }

    uint32_t readUint32() {
      uint32_t value = 0;
      readBlock(&value, 4);
      return value;
    }

    void readByteArray(byte *values, size_t nValues) {
      readBlock(values, nValues);
    }

    void readUint16Array(uint16_t *values, size_t nValues) {
      readBlock(values, nValues * 2);
    }

    void readUint32Array(uint32_t *values, size_t nValues) {
      readBlock(values, nValues * 4);
    }

    // Reads nBytes into target. Kept out of line: inlining this loop at every call site cost several KB of flash.
    // Returns false if no new byte arrived for ARCOM_READ_TIMEOUT_MS, in which case the
    // bytes that did not arrive are set to 0, so that a half-read command cannot act on whatever was in memory.
    // Ops that read a fixed set of values can call this once with a buffer, instead of once per value.
    __attribute__((noinline)) bool readBlock(void *target, size_t nBytes) {
      uint8_t *nextByte = (uint8_t*)target;
      if (readTimedOut) { // A previous read of this command already failed
        memset(nextByte, 0, nBytes);
        return false;
      }
      size_t nBytesRead = 0;
      uint32_t lastByteTime = millis();
      while (nBytesRead < nBytes) {
        size_t nAvailable = available();
        if (nAvailable > 0) {
          if (nAvailable > (nBytes - nBytesRead)) {
            nAvailable = nBytes - nBytesRead;
          }
          #if (HARDWARE_VERSION == 2)
            readReceived(nextByte + nBytesRead, nAvailable);
            nBytesRead += nAvailable;
          #else
            nBytesRead += port.readBytes((char*)(nextByte + nBytesRead), nAvailable);
          #endif
          lastByteTime = millis();
        } else if ((millis() - lastByteTime) > ARCOM_READ_TIMEOUT_MS) {
          memset(nextByte + nBytesRead, 0, nBytes - nBytesRead);
          readTimedOut = true;
          return false;
        }
      }
      return true;
    }

    // --- Writes. These fill the write buffer; flush() sends it. ---

    void writeByte(byte value) {
      writeBlock(&value, 1);
    }

    void writeUint16(uint16_t value) {
      writeBlock(&value, 2);
    }

    void writeUint32(uint32_t value) {
      writeBlock(&value, 4);
    }

    void writeByteArray(const byte *values, size_t nValues) {
      writeBlock(values, nValues);
    }

    void writeUint16Array(const uint16_t *values, size_t nValues) {
      writeBlock(values, nValues * 2);
    }

    void writeUint32Array(const uint32_t *values, size_t nValues) {
      writeBlock(values, nValues * 4);
    }

    // Sends everything written since the last call. loop() calls this once per pass.
    void flush() {
      if (nBytesToWrite == 0) {
        return;
      }
      port.write(writeBuffer, nBytesToWrite);
      nBytesToWrite = 0;
      #if (HARDWARE_VERSION > 2)
        port.send_now(); // Send the packet now, instead of waiting for the USB transmit timer
      #endif
    }

  private:
    ArCOMPort &port;
    bool readTimedOut = false;
    size_t nBytesToWrite = 0;
    uint8_t writeBuffer[ARCOM_WRITE_BUFFER_SIZE];

    #if (HARDWARE_VERSION == 2)
      // Moves the packets the PC has sent from the USB controller into SerialUSB's buffer, while it has room. Replaces
      // the core's Serial_::accept(), which loses packets (see the top of this file).
      __attribute__((noinline)) void receiveUSB() {
        NVIC_DisableIRQ(UOTGHS_IRQn); // So that the core's USB interrupt cannot run accept() meanwhile
        if (Is_udd_out_received_interrupt_enabled(CDC_RX)) {
          udd_disable_out_received_interrupt(CDC_RX); // Switched on by the core whenever the PC configures the device
        }
        while (Is_udd_fifocon(CDC_RX)) { // The CPU holds a whole received packet (perhaps of zero length)
          uint32_t nBytesLeft = UDD_FifoByteCount(CDC_RX); // Counts down as the bytes are read
          uint32_t head = cdc_rx_buffer.head;
          while (nBytesLeft > 0) {
            uint32_t nextHead = (head + 1) % ARCOM_CDC_BUFFER_SIZE;
            if (nextHead == cdc_rx_buffer.tail) {
              break; // The buffer is full
            }
            cdc_rx_buffer.buffer[head] = UDD_Recv8(CDC_RX);
            head = nextHead;
            nBytesLeft--;
          }
          cdc_rx_buffer.head = head;
          if (nBytesLeft > 0) {
            break; // The rest of the packet waits in its bank, and the controller holds off the PC's next packets
          }
          UDD_ReleaseRX(CDC_RX); // The packet is read: free its bank for the PC, and go on to the other bank
        }
        NVIC_EnableIRQ(UOTGHS_IRQn);
      }

      // Reads nBytes from SerialUSB's buffer. available() must have reported them.
      void readReceived(uint8_t *target, size_t nBytes) {
        uint32_t tail = cdc_rx_buffer.tail;
        for (size_t i = 0; i < nBytes; i++) {
          target[i] = cdc_rx_buffer.buffer[tail];
          tail = (tail + 1) % ARCOM_CDC_BUFFER_SIZE;
        }
        cdc_rx_buffer.tail = tail;
      }
    #endif

    // Adds bytes to the write buffer, sending it first if it is full
    __attribute__((noinline)) void writeBlock(const void *source, size_t nBytes) {
      const uint8_t *nextByte = (const uint8_t*)source;
      while (nBytes > 0) {
        if (nBytesToWrite == ARCOM_WRITE_BUFFER_SIZE) {
          flush();
        }
        size_t nBytesThisPass = ARCOM_WRITE_BUFFER_SIZE - nBytesToWrite;
        if (nBytesThisPass > nBytes) {
          nBytesThisPass = nBytes;
        }
        memcpy(writeBuffer + nBytesToWrite, nextByte, nBytesThisPass);
        nBytesToWrite += nBytesThisPass;
        nextByte += nBytesThisPass;
        nBytes -= nBytesThisPass;
      }
    }
};

#endif

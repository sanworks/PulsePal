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
// - This class is only used from loop(). Do not call it from handler() or any other interrupt.

#ifndef ArCOM_h
#define ArCOM_h

#include "Arduino.h"

#ifndef HARDWARE_VERSION
  #error Include ArCOM.h after HARDWARE_VERSION has been defined
#endif

#if (HARDWARE_VERSION == 2)
  typedef Serial_ ArCOMPort; // SAM3X USB CDC class, the type of SerialUSB
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

    // Reads nBytes into target. Kept out of line: inlining this loop at every call site cost several KB of flash.
    // Returns false if no new byte arrived for ARCOM_READ_TIMEOUT_MS, in which case the
    // bytes that did not arrive are set to 0, so that a half-read command cannot act on whatever was in memory.
    __attribute__((noinline)) bool readBlock(void *target, size_t nBytes) {
      uint8_t *nextByte = (uint8_t*)target;
      if (readTimedOut) { // A previous read of this command already failed
        memset(nextByte, 0, nBytes);
        return false;
      }
      size_t nBytesRead = 0;
      uint32_t lastByteTime = millis();
      while (nBytesRead < nBytes) {
        size_t nAvailable = port.available();
        if (nAvailable > 0) {
          if (nAvailable > (nBytes - nBytesRead)) {
            nAvailable = nBytes - nBytesRead;
          }
          nBytesRead += port.readBytes((char*)(nextByte + nBytesRead), nAvailable);
          lastByteTime = millis();
        } else if ((millis() - lastByteTime) > ARCOM_READ_TIMEOUT_MS) {
          memset(nextByte + nBytesRead, 0, nBytes - nBytesRead);
          readTimedOut = true;
          return false;
        }
      }
      return true;
    }

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

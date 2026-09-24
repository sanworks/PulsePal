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

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/

// The byte stream between PulsePal and the device.
//
// PulsePal talks to the device only through the SerialPort interface below. USBSerialPort implements it with
// libserialport (https://sigrok.org/wiki/Libserialport), which supports Windows, macOS and Linux. The offline tests
// in tests/test_protocol.cpp implement it with a fake port that records the bytes PulsePal sends.

#ifndef PULSEPAL_SERIALPORT_H
#define PULSEPAL_SERIALPORT_H

#include <cstddef>
#include <cstdint>
#include <string>

struct sp_port; // libserialport's port handle. Declared here so that users of this header do not need libserialport.h

class SerialPort
{
public:
    virtual ~SerialPort() {}

    // Opens a port by name, e.g. "COM3" on Windows, "/dev/cu.usbmodem14101" on macOS or "/dev/ttyACM0" on Linux
    virtual bool open(const std::string& portName) = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;

    // Discards bytes that were received but not yet read
    virtual void discardInput() = 0;

    // Writes all nBytes. Returns false if they could not all be written.
    virtual bool write(const uint8_t* data, size_t nBytes) = 0;

    // Reads nBytes, waiting at most timeoutMs for them to arrive. Returns the number of bytes read, which is less
    // than nBytes if the timeout was reached.
    virtual size_t read(uint8_t* data, size_t nBytes, unsigned int timeoutMs) = 0;

    // Describes why the last open(), write() or read() failed
    virtual std::string lastError() const = 0;
};

class USBSerialPort : public SerialPort
{
public:
    USBSerialPort();
    ~USBSerialPort();

    USBSerialPort(const USBSerialPort&) = delete; // Not copyable: the copy would close the port twice
    USBSerialPort& operator=(const USBSerialPort&) = delete;

    bool open(const std::string& portName) override;
    void close() override;
    bool isOpen() const override;
    void discardInput() override;
    bool write(const uint8_t* data, size_t nBytes) override;
    size_t read(uint8_t* data, size_t nBytes, unsigned int timeoutMs) override;
    std::string lastError() const override;

private:
    void setError(const std::string& context, int result);

    struct sp_port* port;
    bool portOpen;
    std::string errorMessage;
};

#endif // PULSEPAL_SERIALPORT_H

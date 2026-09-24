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

// USB serial port for PulsePal, using libserialport. See SerialPort.h.

#include "SerialPort.h"

#include <libserialport.h>

// Pulse Pal 2 (Arduino Due native USB port) and Pulse Pal 3 (Teensy 4.1) are USB CDC devices, which ignore the baud
// rate. 115200 is used because every operating system accepts it. Never use 1200: opening the Due's native port at
// 1200 baud erases its firmware, so that it can be reprogrammed.
static const int kBaudRate = 115200;

// How long write() waits for the operating system to accept a command. Commands are at most ~60kB (a full custom
// pulse train on Pulse Pal 3), which takes well under a second even over USB full speed.
static const unsigned int kWriteTimeoutMs = 5000;

USBSerialPort::USBSerialPort() : port(NULL), portOpen(false)
{
}

USBSerialPort::~USBSerialPort()
{
    close();
}

bool USBSerialPort::open(const std::string& portName)
{
    close();
    errorMessage.clear();

    int result = sp_get_port_by_name(portName.c_str(), &port);
    if (result != SP_OK) {
        setError("Could not find serial port " + portName, result);
        port = NULL;
        return false;
    }
    result = sp_open(port, SP_MODE_READ_WRITE);
    if (result != SP_OK) {
        setError("Could not open serial port " + portName, result);
        sp_free_port(port);
        port = NULL;
        return false;
    }
    portOpen = true;

    // Raw 8N1 with no flow control. DTR must be on: the Due's native USB port drops everything it is asked to send
    // while DTR is off, so Pulse Pal 2 would never reply. (The MATLAB class sets DTR for the same reason.)
    struct sp_port_config* config = NULL;
    result = sp_new_config(&config);
    if (result == SP_OK) {
        sp_set_config_baudrate(config, kBaudRate);
        sp_set_config_bits(config, 8);
        sp_set_config_parity(config, SP_PARITY_NONE);
        sp_set_config_stopbits(config, 1);
        sp_set_config_rts(config, SP_RTS_ON);
        sp_set_config_cts(config, SP_CTS_IGNORE);
        sp_set_config_dtr(config, SP_DTR_ON);
        sp_set_config_dsr(config, SP_DSR_IGNORE);
        sp_set_config_xon_xoff(config, SP_XONXOFF_DISABLED);
        result = sp_set_config(port, config);
        sp_free_config(config);
    }
    if (result != SP_OK) {
        setError("Could not configure serial port " + portName, result);
        close();
        return false;
    }
    return true;
}

void USBSerialPort::close()
{
    if (port != NULL) {
        if (portOpen) {
            sp_close(port);
        }
        sp_free_port(port);
        port = NULL;
    }
    portOpen = false;
}

bool USBSerialPort::isOpen() const
{
    return portOpen;
}

void USBSerialPort::discardInput()
{
    if (portOpen) {
        sp_flush(port, SP_BUF_INPUT);
    }
}

bool USBSerialPort::write(const uint8_t* data, size_t nBytes)
{
    if (!portOpen) {
        errorMessage = "The serial port is not open";
        return false;
    }
    int result = sp_blocking_write(port, data, nBytes, kWriteTimeoutMs);
    if (result < 0) {
        setError("Could not write to the serial port", result);
        return false;
    }
    if ((size_t)result < nBytes) {
        errorMessage = "Timed out writing to the serial port";
        return false;
    }
    return true;
}

size_t USBSerialPort::read(uint8_t* data, size_t nBytes, unsigned int timeoutMs)
{
    if (!portOpen) {
        errorMessage = "The serial port is not open";
        return 0;
    }
    if (timeoutMs == 0) {
        timeoutMs = 1; // libserialport treats 0 as "wait forever"
    }
    int result = sp_blocking_read(port, data, nBytes, timeoutMs);
    if (result < 0) {
        setError("Could not read from the serial port", result);
        return 0;
    }
    return (size_t)result;
}

std::string USBSerialPort::lastError() const
{
    return errorMessage;
}

void USBSerialPort::setError(const std::string& context, int result)
{
    errorMessage = context;
    switch (result) {
        case SP_ERR_FAIL: { // An operating system error, whose message libserialport keeps
            char* message = sp_last_error_message();
            if (message != NULL) {
                std::string text(message);
                sp_free_error_message(message);
                while (!text.empty() && ((text.back() == '\n') || (text.back() == '\r') || (text.back() == ' ')
                                         || (text.back() == '.'))) {
                    text.pop_back(); // Windows messages end in a full stop and a line break
                }
                errorMessage += ": " + text;
            }
        } break;
        case SP_ERR_ARG: errorMessage += ": invalid argument (check the port name)"; break;
        case SP_ERR_MEM: errorMessage += ": out of memory"; break;
        case SP_ERR_SUPP: errorMessage += ": not supported on this platform"; break;
    }
}

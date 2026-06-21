"""
----------------------------------------------------------------------------

This file is part of the Sanworks PulsePal repository
Copyright (C) Sanworks LLC, Rochester, New York, USA

----------------------------------------------------------------------------

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, version 3.

This program is distributed  WITHOUT ANY WARRANTY and without even the
implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
"""

# ArCOM (Arduino Communication) wraps PySerial and numpy to streamline communication
# of numpy arrays to and from equivalent types on an Arduino-programmable microcontroller.
# A matching class for Arduino is provided in the ArCOM repository: https://github.com/sanworks/ArCOM

import numpy as np
import serial


class ArCom(object):
    def __init__(self, serial_port_name, baud_rate):
        """  Initializes the ArCOM object and opens the USB serial port
             Args:
                 serial_port_name (string) The name of the USB serial port as known to the OS
                                           Examples = 'COM3' (Windows), '/dev/ttyACM0' (Linux)
                 baud_rate (uint32) The speed of the target USB serial device (bits/second)
             Returns:
                 none
        """
        self._typeNames = ('uint8', 'int8', 'char', 'uint16', 'int16', 'uint32', 'int32', 'single', 'double')
        self._typeBytes = (1, 1, 1, 2, 2, 4, 4, 8)
        self.serialObject = serial.Serial(serial_port_name, baud_rate, timeout=10, rtscts=True)

    def close(self):
        """  Closes the USB serial port
             Args:
                 none
             Returns:
                 none
         """
        self.serialObject.close()

    def bytes_available(self):
        """  Returns the number of bytes available to read from the USB serial buffer
             Args:
                 none
             Returns:
                 nBytes (int) the number of bytes available to read
         """
        return self.serialObject.inWaiting()

    def write(self, *arg):
        """  Write bytes to the USB serial buffer
             Args:
                 Arguments containing a message to write. The message format is:
                 # First value:
                     Arg 1. A single value or list of values to write
                     Arg 2. The datatype of the data in Arg 1 (must be supported, see self._typeBytes)
                 # Additional values (optional) given as pairs of arguments
                     Arg N. An additional value or list of values to write
                     Arg N+1. The datatype of arg N
             Returns:
                 none
         """
        n_types = int(len(arg)/2)
        arg_pos = 0
        message_bytes = b''
        for i in range(0, n_types):
            data = arg[arg_pos]
            arg_pos += 1
            datatype = arg[arg_pos]
            arg_pos += 1
            if (datatype in self._typeNames) is False:
                raise ArCOMError('Error: ' + datatype + ' is not a data type supported by ArCOM.')
            datatype_pos = self._typeNames.index(datatype)
            
            if type(data).__module__ == np.__name__:
                npdata = data.astype(datatype)
            else:
                npdata = np.array(data, dtype=datatype)
            message_bytes += npdata.tobytes()
        self.serialObject.write(message_bytes)

    def read(self, *arg):
        """  Read bytes from the USB serial buffer
             Args:
                 Arguments containing a message to read. The message format is:
                 # First value:
                     Arg 1. The number of values to read
                     Arg 2. The datatype of the data in Arg 1 (must be supported, see self._typeBytes)
                 # Additional values (optional) given as pairs of arguments
                     Arg N. An additional value number of values to read
                     Arg N+1. The datatype of arg N
                     Note: If additional args are given, the data will be returned as a list
                           with each requested value in the next sequential list position
             Returns:
                 The data requested, returned as a numpy ndarray
                (or a list of ndarrays if multiple values were requested)
         """
        num_types = int(len(arg)/2)
        arg_pos = 0
        outputs = []
        for i in range(0, num_types):
            num_values = arg[arg_pos]
            arg_pos += 1
            datatype = arg[arg_pos]
            if (datatype in self._typeNames) is False:
                raise ArCOMError('Error: ' + datatype + ' is not a data type supported by ArCOM.')
            arg_pos += 1
            type_index = self._typeNames.index(datatype)
            byte_width = self._typeBytes[type_index]
            n_bytes2read = num_values*byte_width
            message_bytes = self.serialObject.read(n_bytes2read)
            n_bytes_read = len(message_bytes)
            if n_bytes_read < n_bytes2read:
                raise ArCOMError('Error: serial port timed out. ' + str(n_bytes_read) +
                                 ' bytes read. Expected ' + str(n_bytes2read) + ' byte(s).')
            this_output = np.frombuffer(message_bytes, datatype)
            outputs.append(this_output)
        if num_types == 1:
            outputs = this_output
        return outputs

    def __del__(self):
        self.serialObject.close()


class ArCOMError(Exception):
    pass

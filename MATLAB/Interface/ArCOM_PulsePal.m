%{
----------------------------------------------------------------------------

This file is part of the Sanworks ArCOM repository
Copyright (C) 2016 Sanworks LLC, Sound Beach, New York, USA

----------------------------------------------------------------------------

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, version 3.

This program is distributed  WITHOUT ANY WARRANTY and without even the
implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
%}

% ArCOM uses an Arduino library to simplify serial communication of different
% data types between MATLAB/GNU Octave and Arduino. To use the library,
% include the following at the top of your Arduino sketch:
% #include "ArCOM.h". See documentation for more Arduino-side tips.
%
% Initialization syntax:
% serialObj = ArCOM('open', 'COM3')
% where 'COM3' is the name of Arduino's serial port on your system.
% This call both creates and opens the port. It returns a struct containing
% a serial port and properties. If PsychToolbox IOport interface is
% available, this is used by default. To use the java interface on a system
% with PsychToolbox, use ArCOM('open', 'COM3', 'java')
%
% Write: ArCOM('write', serialObj, myData, 'uint8') % where 'uint8' is a
% data type from the following list: 'uint8', 'uint16', 'uint32'. If no
% data type argument is specified, ArCOM assumes uint8.
%
% Read: myData = ArCOM('read' serialObj, nValues, 'uint8') % where nValues is the number
% of values to read, and 'uint8' is a data type from the following list: 'uint8', 'uint16', 'uint32'
% If no data type argument is specified, ArCOM assumes uint8.
%
% End: ArCOM('close', serialObj) % Closes, deletes and clears the serial port
% object in the workspace of the calling function

function varargout = ArCOM_PulsePal(op, varargin)
switch lower(op)
    case 'open'
        arCOMObject = struct;
        arCOMObject.Port = [];
        if (exist('OCTAVE_VERSION'))
            try
                pkg load instrument-control
            catch
                error('Please install the instrument control toolbox first. See http://wiki.octave.org/Instrument_control_package');
            end
            if (exist('serial') ~= 3)
                error('Serial port communication is necessary for Pulse Pal, but is not supported in Octave on your platform.');
            end
            warning('off', 'Octave:num-to-str');
            arCOMObject.UseOctave = 1;
            arCOMObject.Interface = 2; % Octave serial interface
        else
            arCOMObject.UseOctave = 0;
        end
        try
            PsychtoolboxVersion;
            arCOMObject.UsePsychToolbox = 1;
            arCOMObject.Interface = 1; % PsychToolbox serial interface
        catch
            arCOMObject.UsePsychToolbox = 0;
            arCOMObject.Interface = 0; % Java serial interface
        end
        portString = varargin{1};
        if nargin > 2
            baudRate = varargin{2};
            if ischar(baudRate)
                baudRate = str2double(baudRate);
            end
        else
            error('Error: Please add a baudRate argument when calling ArCOM(''open''')
        end
        if ~isnan(baudRate) && baudRate >= 1200
            arCOMObject.baudRate = baudRate;
        else
            error(['Error: ' baudRate ' is an invalid baud rate for ArCOM. Some common baud rates are: 9600, 115200'])
        end
        if nargin > 3
            forceOption = varargin{2};
            switch lower(forceOption)
                case 'java'
                    arCOMObject.UsePsychToolbox = 0;
                    arCOMObject.Interface = 0;
                case 'psychtoolbox'
                    arCOMObject.UsePsychToolbox = 1;
                    arCOMObject.Interface = 1;
                otherwise
                    error('The third argument to ArCOM(''init'' must be either ''java'' or ''psychtoolbox''');
            end
        end
        arCOMObject.validDataTypes = {'char', 'uint8', 'uint16', 'uint32', 'int8', 'int16', 'int32'};
        switch arCOMObject.Interface
            case 0
                arCOMObject.Port = serial(portString, 'BaudRate', 115200, 'Timeout', 1,'OutputBufferSize', 100000, 'InputBufferSize', 100000, 'DataTerminalReady', 'on', 'tag', 'ArCOM');
                fopen(arCOMObject.Port);
                varargout{1} = arCOMObject;
            case 1
                if ispc
                    portString = ['\\.\' portString];
                end
                IOPort('Verbosity', 0);
                arCOMObject.Port = IOPort('OpenSerialPort', portString, 'BaudRate=115200, OutputBufferSize=100000, DTR=1');
                if (arCOMObject.Port < 0)
                    IOPort('Close', arCOMObject.Port);
                    error(['Error: Unable to connect to port ' portString '. The port may be in use by another application.'])
                end
                pause(.1); % Helps on some platforms
                varargout{1} = arCOMObject;
            case 2
                if ispc
                    PortNum = str2double(portString(4:end));
                    if PortNum > 9
                        portString = ['\\\\.\\COM' num2str(PortNum)]; % As of Octave instrument control toolbox v0.2.2, ports higher than COM9 must use this syntax
                    end
                end
                arCOMObject.Port = serial(portString, 115200,  1);
                pause(.2);
                srl_flush(arCOMObject.Port);
                varargout{1} = arCOMObject;
        end
    case 'bytesavailable'
        arCOMObject = varargin{1};
        switch arCOMObject.Interface
            case 0 % MATLAB/Java
                varargout{1} = arCOMObject.Port.BytesAvailable;
            case 1 % MATLAB/PsychToolbox
                varargout{1} = IOPort('BytesAvailable', arCOMObject.Port);
            case 2 % Octave
                error('Reading available bytes from a serial port buffer is not supported in Octave as of instrument control toolbox 0.2.2');
        end
    case 'write'
        arCOMObject = varargin{1};
        if nargin == 3 % Single array with no data type specified (defaults to uint8)
            nArrays = 1;
            data2Send = varargin(2);
            dataTypes = {'uint8'};
        else
            nArrays = (nargin-2)/2;
            data2Send = varargin(2:2:end);
            dataTypes = varargin(3:2:end);
        end
        
        nTotalBytes = 0;
        DataLength = cellfun('length',data2Send);
        for i = 1:nArrays
            switch dataTypes{i}
                case 'char'
                    nTotalBytes = nTotalBytes + DataLength(i);
                case 'uint8'
                    nTotalBytes = nTotalBytes + DataLength(i);
                case 'uint16'
                    DataLength(i) = DataLength(i)*2;
                    nTotalBytes = nTotalBytes + DataLength(i);
                case 'uint32'
                    DataLength(i) = DataLength(i)*4;
                    nTotalBytes = nTotalBytes + DataLength(i);
                case 'int8'
                    nTotalBytes = nTotalBytes + DataLength(i);
                case 'int16'
                    DataLength(i) = DataLength(i)*2;
                    nTotalBytes = nTotalBytes + DataLength(i);
                case 'int32'
                    DataLength(i) = DataLength(i)*4;
                    nTotalBytes = nTotalBytes + DataLength(i);
            end
        end
        ByteStringPos = 1;
        ByteString = uint8(zeros(1,nTotalBytes));
        for i = 1:nArrays
            dataType = dataTypes{i};
            data = data2Send{i};
            switch dataType % Check range and cast to uint8
                case 'char'
                    if sum((data < 0)+(data > 128)) > 0
                        error('Error: a char was out of range: 0 to 128 (limited by Arduino)')
                    end
                    ByteString(ByteStringPos:ByteStringPos+DataLength(i)-1) = char(data);
                    ByteStringPos = ByteStringPos + DataLength(i);
                case 'uint8'
                    if sum((data < 0)+(data > 255)) > 0
                        error('Error: an unsigned 8-bit integer was out of range: 0 to 255')
                    end
                    ByteString(ByteStringPos:ByteStringPos+DataLength(i)-1) = uint8(data);
                    ByteStringPos = ByteStringPos + DataLength(i);
                case 'uint16'
                    if sum((data < 0)+(data > 65535)) > 0
                        error('Error: an unsigned 16-bit integer was out of range: 0 to 65,535')
                    end
                    ByteString(ByteStringPos:ByteStringPos+DataLength(i)-1) = typecast(uint16(data), 'uint8');
                    ByteStringPos = ByteStringPos + DataLength(i);
                case 'uint32'
                    if sum((data < 0)+(data > 4294967295)) > 0
                        error('Error: an unsigned 32-bit integer was out of range: 0 to 4,294,967,295')
                    end
                    ByteString(ByteStringPos:ByteStringPos+DataLength(i)-1) = typecast(uint32(data), 'uint8');
                    ByteStringPos = ByteStringPos + DataLength(i);
                case 'int8'
                    if sum((data < -128)+(data > 127)) > 0
                        error('Error: a signed 8-bit integer was out of range: -128 to 127')
                    end
                    ByteString(ByteStringPos:ByteStringPos+DataLength(i)-1) = typecast(int8(data), 'uint8');
                    ByteStringPos = ByteStringPos + DataLength(i);
                case 'int16'
                    if sum((data < -32768)+(data > 32767)) > 0
                        error('Error: a signed 16-bit integer was out of range: -32,768 to 32,767')
                    end
                    ByteString(ByteStringPos:ByteStringPos+DataLength(i)-1) = typecast(int16(data), 'uint8');
                    ByteStringPos = ByteStringPos + DataLength(i);
                case 'int32'
                    if sum((data < -2147483648)+(data > 2147483647)) > 0
                        error('Error: a signed 32-bit integer was out of range: -2,147,483,648 to 2,147,483,647')
                    end
                    ByteString(ByteStringPos:ByteStringPos+DataLength(i)-1) = typecast(int32(data), 'uint8');
                    ByteStringPos = ByteStringPos + DataLength(i);
                otherwise
                    error(['The datatype ' dataType ' is not currently supported by ArCOM.']);
            end
        end
        switch arCOMObject.Interface
            case 0
                fwrite(arCOMObject.Port, ByteString, 'uint8');
            case 1
                IOPort('Write', arCOMObject.Port, ByteString, 1);
            case 2
                srl_write(arCOMObject.Port, char(ByteString));
        end
        
    case 'read'
        arCOMObject = varargin{1};
        if nargin == 3
            nArrays = 1;
            nValues = varargin(2);
            dataTypes = {'uint8'};
        else
            nArrays = (nargin-2)/2;
            nValues = varargin(2:2:end);
            dataTypes = varargin(3:2:end);
        end
        nValues = double(cell2mat(nValues));
        nTotalBytes = 0;
        for i = 1:nArrays
            switch dataTypes{i}
                case 'char'
                    nTotalBytes = nTotalBytes + nValues(i);
                case 'uint8'
                    nTotalBytes = nTotalBytes + nValues(i);
                case 'uint16'
                    nTotalBytes = nTotalBytes + nValues(i)*2;
                case 'uint32'
                    nTotalBytes = nTotalBytes + nValues(i)*4;
                case 'int8'
                    nTotalBytes = nTotalBytes + nValues(i);
                case 'int16'
                    nTotalBytes = nTotalBytes + nValues(i)*2;
                case 'int32'
                    nTotalBytes = nTotalBytes + nValues(i)*4;
            end
        end
        switch arCOMObject.Interface
            case 0
                ByteString = fread(arCOMObject.Port, nTotalBytes, 'uint8');
            case 1
                ByteString = IOPort('Read', arCOMObject.Port, 1, nTotalBytes);
            case 2
                ByteString = srl_read(arCOMObject.Port, nTotalBytes);
        end
        if isempty(ByteString)
            error('Error: The serial port returned 0 bytes.')
        end
        Pos = 1;
        varargout = cell(1,nArrays);
        for i = 1:nArrays
            switch dataTypes{i}
                case 'char'
                    varargout{i} = char(ByteString(Pos:Pos+nValues(i)-1)); Pos = Pos + nValues(i);
                case 'uint8'
                    varargout{i} = uint8(ByteString(Pos:Pos+nValues(i)-1)); Pos = Pos + nValues(i);
                case 'uint16'
                    varargout{i} = typecast(uint8(ByteString(Pos:Pos+(nValues(i)*2)-1)), 'uint16'); Pos = Pos + nValues(i)*2;
                case 'uint32'
                    varargout{i} = typecast(uint8(ByteString(Pos:Pos+(nValues(i)*4)-1)), 'uint32'); Pos = Pos + nValues(i)*4;
                case 'int8'
                    varargout{i} = typecast(uint8(ByteString(Pos:Pos+(nValues(i))-1)), 'int8'); Pos = Pos + nValues(i);
                case 'int16'
                    varargout{i} = typecast(uint8(ByteString(Pos:Pos+(nValues(i)*2)-1)), 'int16'); Pos = Pos + nValues(i)*2;
                case 'int32'
                    varargout{i} = typecast(uint8(ByteString(Pos:Pos+(nValues(i)*4)-1)), 'int32'); Pos = Pos + nValues(i)*4;
            end
        end
    case 'close'
        arCOMObject = varargin{1};
        switch arCOMObject.Interface
            case 0
                fclose(arCOMObject.Port);
                delete(arCOMObject.Port);
            case 1
                IOPort('Close', arCOMObject.Port);
            case 2
                fclose(arCOMObject.Port);
                arCOMObject.Port = [];
        end
        evalin('caller', ['clear ' inputname(2)])
    otherwise
        error('Error: call to ArCOM with an invalid op argument. Valid arguments are: open, bytesAvailable, read, write, close')
end
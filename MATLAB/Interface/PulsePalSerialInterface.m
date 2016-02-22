%{
----------------------------------------------------------------------------

This file is part of the Sanworks Pulse Pal repository
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
function varargout = PulsePalSerialInterface(op, varargin)
% PulsePalSerialInterface wraps either MATLAB/Java or Psychtoolbox serial
% interface (automatically determined) to communicate with Pulse Pal.
% op = 'init', 'read', 'write', 'bytesAvailable', 'end'
% op = 
% 'init': 2 optional arguments: SerialPort, ForceJava
% SerialPort = a string with the serial port as it is known to the OS
% ForceJava = 1 to use Matlab's native Java serial interface instead of 
%               PsychToolbox (the default when psychtoolbox is installed).
% 
% 'read': 2 required arguments: nIntegers, dataType
% nIntegers = how many numbers to read
% dataType = 'uint8', 'uint16', 'uint32' for 8, 16 or 32 bit unsigned ints respectively
%
% 'write': 2 required arguments: Data, dataType
% data = a string of bytes or integers
% dataType = 'uint8', 'uint16', 'uint32' for 8, 16 or 32 bit unsigned ints respectively
%
% 'bytesAvailable': returns the number of bytes currently in the read buffer,
% available to be read with PulsePalSerialInterface('read'...)
%
% 'end': Disconnects the serial interface

global PulsePalSystem
ForceJava = 0;
switch op
    case 'init'
        disp('Searching for Pulse Pal. Please wait.')
        LastPortPath = fullfile(PulsePalSystem.PulsePalPath, 'LastSerialPortUsed.mat');
        BaudRate = 9600; % Setting this to higher baud rate on mac causes crashes, but on all platforms it is effectively ignored - actual transmission proceeds at ~1MB/s
        if nargin > 1
            Ports = {upper(varargin{1})};
            if nargin > 2
                ForceJava = 1;
            end
        else
            % Make list of all ports
            if ispc
                Ports = FindPulsePalPorts;
            elseif ismac
                [trash, RawSerialPortList] = system('ls /dev/tty.*');
                Ports = ParseCOMString_MAC(RawSerialPortList);
            else
                VerifyMatlabSerialPortAccessForUbuntu;
                [trash, RawSerialPortList] = system('ls /dev/ttyACM*');
                Ports = ParseCOMString_LINUX(RawSerialPortList);
            end
            if isempty(Ports)
                error('Could not connect to Pulse Pal: no available serial ports found.');
            end
            % Make it search on the last successful port first
            if (exist(LastPortPath) == 2)
                load(LastPortPath);
                pos = strmatch(LastComPortUsed, Ports, 'exact'); 
                if ~isempty(pos)
                    Temp = Ports;
                    Ports{1} = LastComPortUsed;
                    Ports(2:length(Temp)) = Temp(1:length(Temp) ~= pos);
                end
            end
        end
        
        if isempty(Ports)
            error('Could not connect to Pulse Pal: no available serial ports found.');
        end
        if isempty(Ports{1})
            error('Could not connect to Pulse Pal: no available serial ports found.');
        end
        % Determine if PsychToolbox is installed. If so, serial communication
        % will proceed through lower latency psychtoolbox IOport serial interface (compiled for each platform).
        % Otherwise, Pulse Pal defaults to MATLAB's Java based serial interface.
        try
            V = PsychtoolboxVersion;
            PulsePalSystem.SerialInterface = 1; % Psych toolbox
        catch
            PulsePalSystem.SerialInterface = 0; % Java/MATLAB
        end
        Found = 0;
        i = 0;
        if ForceJava
            PulsePalSystem.SerialInterface = 0;
        end
        switch PulsePalSystem.SerialInterface
            case 0 % Java serial interface (MATLAB default)
                disp('Connecting with MATLAB/Java serial interface (high latency).')
                while (Found == 0) && (i < length(Ports))
                    i = i + 1;
                    disp(['Trying port ' Ports{i}])
                    TestPort = serial(Ports{i}, 'BaudRate', BaudRate, 'Timeout', 1,'OutputBufferSize', 100000, 'InputBufferSize', 1000, 'DataTerminalReady', 'on', 'tag', 'PulsePal');
                    AvailablePort = 1;
                    try
                        fopen(TestPort);
                    catch
                        AvailablePort = 0;
                    end
                    if AvailablePort == 1
                        pause(.5);
                        fwrite(TestPort, [PulsePalSystem.OpMenuByte 72], 'uint8');
                        tic
                        while TestPort.BytesAvailable == 0
                            fwrite(TestPort, [PulsePalSystem.OpMenuByte 72], 'uint8');
                            if toc > 1
                                break
                            end
                            pause(.1);
                        end
                        g = 0;
                        try
                            g = fread(TestPort, 1);
                        catch
                            % ok
                        end
                        if g == 75
                            Found = i;
                        end
                        fclose(TestPort);
                        delete(TestPort)
                    end
                end
                pause(.1);
                if Found ~= 0
                    % Note: DTR is now to "on" here - was off for earlier versions of Pulse Pal 1, but
                    % this seems to work for both
                    PulsePalSystem.SerialPort = serial(Ports{Found}, 'BaudRate', BaudRate, 'Timeout', 1, 'OutputBufferSize', 100000, 'InputBufferSize', 1000, 'DataTerminalReady', 'on', 'tag', 'PulsePal');
                else
                    error('Error: could not find your Pulse Pal device. Please make sure it is connected and drivers are installed.');
                end
                fopen(PulsePalSystem.SerialPort);
            case 1 % Psych toolbox serial interface
                disp('Connecting with PsychToolbox serial interface (low latency).')
                IOPort('Verbosity', 0);
                 while (Found == 0) && (i < length(Ports)) && ~isempty(Ports{1})
                    i = i + 1;
                    disp(['Trying port ' Ports{i}])
                    try
                        if ispc
                            PortString = ['\\.\' Ports{i}];
                        else
                            PortString = Ports{i};
                        end
                        TestPort = IOPort('OpenSerialPort', PortString, 'BaudRate=115200, OutputBufferSize=100000, DTR=1');
                        pause(.1);
                        IOPort('Write', TestPort, uint8([PulsePalSystem.OpMenuByte 72]), 1);
                        pause(.1);
                        Byte = IOPort('Read', TestPort, 1, 1);
                        if Byte == 75
                            Found = i;
                        end
                        IOPort('Close', TestPort);
                    catch
                    end
                 end
                 if Found ~= 0
                     if ispc
                         PortString = ['\\.\' Ports{Found}];
                     else
                         PortString = Ports{Found};
                     end
                     PulsePalSystem.SerialPort = IOPort('OpenSerialPort', PortString, 'BaudRate=115200, OutputBufferSize=100000, DTR=1');
                 else
                     error('No valid serial port detected.')
                 end
        end
        LastComPortUsed = Ports{Found};
        save(LastPortPath, 'LastComPortUsed');
        pause(.1);
        disp(['Pulse Pal connected on port ' Ports{Found}]);
    case 'read'
        nIntegers = varargin{1};
        Datatype = varargin{2};
        switch PulsePalSystem.SerialInterface
            case 0
                varargout{1} = fread(PulsePalSystem.SerialPort, nIntegers, Datatype);
            case 1
                % Psych toolbox way
                nIntegers = double(nIntegers);
                switch Datatype
                    case 'uint8'
                        varargout{1} = IOPort('Read', PulsePalSystem.SerialPort, 1, nIntegers);
                    case 'uint16'
                        Data = IOPort('Read', PulsePalSystem.SerialPort, 1, nIntegers*2);
                        Data = uint8(Data);
                        varargout{1} = double(typecast(Data, 'uint16'));
                    case 'uint32'
                        Data = IOPort('Read', PulsePalSystem.SerialPort, 1, nIntegers*4);
                        Data = uint8(Data);
                        varargout{1} = double(typecast(Data, 'uint32'));
                end
        end
    case 'write'
        ByteString = varargin{1};
        Datatype = varargin{2};
        switch PulsePalSystem.SerialInterface
            case 0
                fwrite(PulsePalSystem.SerialPort, ByteString, Datatype);
            case 1
                switch Datatype
                    case 'uint8'
                        IOPort('Write', PulsePalSystem.SerialPort, uint8(ByteString), 1);
                    case 'uint16'
                        IOPort('Write', PulsePalSystem.SerialPort, typecast(uint16(ByteString), 'uint8'), 1);
                    case 'uint32'
                        IOPort('Write', PulsePalSystem.SerialPort, typecast(uint32(ByteString), 'uint8'), 1);
                end
        end

    case 'bytesAvailable'
        switch PulsePalSystem.SerialInterface
            case 0
                varargout{1} = PulsePalSystem.SerialPort.BytesAvailable;
            case 1
                varargout{1} = IOPort('BytesAvailable', PulsePalSystem.SerialPort);
        end
    case 'end'
        switch PulsePalSystem.SerialInterface
            case 0
                fclose(PulsePalSystem.SerialPort);
                delete(PulsePalSystem.SerialPort);
            case 1
                IOPort('Close', PulsePalSystem.SerialPort);
        end
        PulsePalSystem.SerialPort = [];
        clear PulsePalSystem
    otherwise
        error('Invalid op. See SerialInterface.m for valid ops.')
end

function VerifyMatlabSerialPortAccessForUbuntu
if exist([matlabroot '/bin/glnxa64/java.opts']) ~= 2
    disp(' ');
    disp('**ALERT**')
    disp('Linux64 detected. A file must be copied to the MATLAB root, to gain access to virtual serial ports.')
    disp('This file only needs to be copied once.')
    input('Pulse Pal will try to copy this file from the repository automatically. Press return... ')
    try
        system(['sudo cp ' PulsePalSystem.PulsePalPath 'java.opts ' matlabroot '/bin/glnxa64']);
        disp(' ');
        disp('**SUCCESS**')
        disp('File copied! Please restart MATLAB and run PulsePal again.')
        return
    catch
        disp('File copy error! MATLAB may not have administrative privileges.')
        disp('Please copy /PulsePal/MATLAB/java.opts to the MATLAB java library path.')
        disp('The path is typically /usr/local/MATLAB/R2014a/bin/glnxa64, where r2014a is your MATLAB release.')
        return
    end
end
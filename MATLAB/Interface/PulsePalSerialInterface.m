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
% PulsePalSerialInterface wraps either MATLAB/Java, Psychtoolbox or Octave serial
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
        if PulsePalSystem.UsingOctave
          try
            pkg load instrument-control
          catch
            error('Please install the instrument control toolbox first. See http://wiki.octave.org/Instrument_control_package');
          end
          if (exist('serial') ~= 3)
              error('Serial port communication is necessary for Pulse Pal, but is not supported in Octave on your platform.');
          end
          warning('off', 'Octave:num-to-str');
        end
        LastPortPath = fullfile(PulsePalSystem.PulsePalPath, 'LastSerialPortUsed.mat');
        BaudRate = 9600; % Setting this to higher baud rate on mac causes crashes, but on all platforms it is effectively ignored - actual transmission proceeds at ~1MB/s
        if nargin > 1
            Ports = varargin(1);
            if nargin > 2
              InterfaceType = varargin{2};
              if strcmp(InterfaceType, 'ForceJava')
                ForceJava = 1;
              end
            end
        else
            % Make list of all ports
            if ispc
                Ports = FindPulsePalPorts;
            elseif ismac
                [trash, TTYSerialPortList] = system('ls /dev/tty.*');
                [trash, CUSerialPortList] = system('ls /dev/cu.*');
                RawSerialPortList = [TTYSerialPortList CUSerialPortList];
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
        if PulsePalSystem.UsingOctave
          PulsePalSystem.SerialInterface = 2; % Octave Instrument Control Toolbox
        else
          try
              V = PsychtoolboxVersion;
              PulsePalSystem.SerialInterface = 1; % Psych toolbox IOPORT / MATLAB
              if ForceJava
                  PulsePalSystem.SerialInterface = 0;
              end
          catch
              PulsePalSystem.SerialInterface = 0; % Java/MATLAB
          end
        end
        Found = 0;
        i = 0;
        while (Found == 0) && (i < length(Ports))
            i = i + 1;
            try
                TestPort = ArCOM_PulsePal('open', Ports{i}, BaudRate);
                pause(.5);
                ArCOM_PulsePal('write', TestPort, [PulsePalSystem.OpMenuByte 72], 'uint8');
                Response = ArCOM_PulsePal('read', TestPort, 1, 'uint8');
                if Response == 75
                    ArCOM_PulsePal('read', TestPort, 1, 'uint32'); % Drop firmware version from buffer
                    Found = i;
                    PulsePalSystem.SerialPort = TestPort;
                    LastComPortUsed = Ports{i};
                    if PulsePalSystem.UsingOctave
                        save('-mat7-binary', LastPortPath, 'LastComPortUsed');
                    else
                        save(LastPortPath, 'LastComPortUsed');
                    end
                    pause(.1);
                    
                    disp(['Pulse Pal connected on port ' Ports{Found}]);
                end
            catch
                
            end
        end
        if (Found == 0)
            error('Error: Could not open any of the available serial ports')
        end
    case 'read'
        nIntegers = varargin{1};
        Datatype = varargin{2};
        varargout{1} = ArCOM_PulsePal('read', PulsePalSystem.SerialPort, nIntegers, Datatype);
    case 'write'
        ByteString = varargin{1};
        Datatype = varargin{2};
        ArCOM_PulsePal('write', PulsePalSystem.SerialPort, ByteString, Datatype);
    case 'bytesAvailable'
        varargout{1} = ArCOM_PulsePal('bytesAvailable', PulsePalSystem.SerialPort);
    case 'end'
        ArCOM_PulsePal('close', PulsePalSystem.SerialPort);
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
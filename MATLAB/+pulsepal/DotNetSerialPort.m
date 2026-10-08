% DotNetSerialPort is a USB serial port for PulsePalDevice, WavePalDevice and SynthPalDevice on Windows, using .NET's
% System.IO.Ports.SerialPort. It has the part of serialport's interface that those classes use: write(), read(),
% flush(), NumBytesAvailable, setDTR(), Timeout and Port.
%
% Why: on Windows, MATLAB's serialport delivers each reply about 15.6 ms after the device sends it, because its
% background reader sleeps for one tick of the Windows timer between polls of the port (R2020b to R2025b). A Pulse Pal
% replies in well under 1 ms, so every command that waits for a confirm byte would take about 16 ms. .NET's SerialPort
% waits on the port itself: a round trip takes about 0.3 ms, as from Python, and an open, idle port costs no CPU.
%
%   port = pulsepal.DotNetSerialPort('COM3');
%   write(port, [213 72], 'uint8');
%   reply = read(port, 1, 'uint8');
%   delete(port); % Or clear the last reference to it. Either closes the port.
%
% Behaves like serialport where PulsePalDevice depends on it:
% - read() waits up to Timeout seconds (10 by default) for the whole reply. If it does not arrive, read() warns and
%   returns the values that did, possibly none, as serialport does.
% - Values are returned as a row of doubles, little-endian, as serialport returns them.
% - Ctrl+C can interrupt a read that is waiting: it waits in 100 ms slices, because MATLAB cannot interrupt a .NET call
%   in progress.

%{
----------------------------------------------------------------------------

This file is part of the Sanworks Pulse Pal repository
Copyright (C) 2026 Sanworks LLC, Rochester, New York, USA

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

classdef DotNetSerialPort < handle
    properties (SetAccess = private)
        Port % Port name, e.g. "COM3"
    end

    properties
        BaudRate % Ignored by USB serial devices, except that a Teensy reboots into its bootloader at 134
        Timeout = 10 % Seconds that read() and write() wait before giving up
    end

    properties (Dependent)
        NumBytesAvailable % Bytes received and not yet read
    end

    properties (Access = private)
        SerialPort % System.IO.Ports.SerialPort
        BaseStream % Its stream, whose finalizer is suppressed while the port is open (see closePort())
    end

    properties (Constant, Access = private)
        ReadSliceMs = 100 % A read waits in slices of this length, so Ctrl+C can interrupt it
    end

    methods (Static)
        function tf = isAvailable()
            % True if .NET's System.IO.Ports.SerialPort can be used: on Windows, with the .NET Framework, which
            % Windows includes. MATLAB can also be set to use .NET (Core) with dotnetenv, which does not include
            % System.IO.Ports.
            tf = false;
            if ~ispc
                return
            end
            try
                NET.addAssembly('System');
                probe = System.IO.Ports.SerialPort(); % Opens nothing
                probe.Dispose();
                tf = true;
            catch
            end
        end
    end

    methods
        function obj = DotNetSerialPort(portName, baudRate, varargin)
            p = inputParser;
            addParameter(p, 'Timeout', 10);
            parse(p, varargin{:});
            if nargin < 2
                baudRate = 9600; % USB serial ignores it
            end
            NET.addAssembly('System');
            obj.Port = string(portName);
            obj.BaudRate = baudRate;
            obj.Timeout = p.Results.Timeout;
            sp = System.IO.Ports.SerialPort(char(portName));
            sp.BaudRate = baudRate;
            sp.DtrEnable = true; % As PulsePalDevice sets it with serialport's setDTR()
            sp.ReadBufferSize = 65536;
            sp.WriteBufferSize = 65536;
            sp.ReadTimeout = obj.ReadSliceMs;
            sp.WriteTimeout = round(obj.Timeout * 1000);
            try
                sp.Open();
            catch err
                sp.Dispose();
                obj.throwPortError(err, "open");
            end
            obj.SerialPort = sp;
            % Some .NET Framework versions throw from this stream's finalizer, on the finalizer thread where nothing can
            % catch it, if a USB serial device is unplugged while its port is open, which can crash MATLAB. closePort()
            % disposes of the stream itself, so the finalizer never needs to run.
            obj.BaseStream = sp.BaseStream;
            System.GC.SuppressFinalize(obj.BaseStream);
        end

        function delete(obj)
            obj.closePort();
        end

        function n = get.NumBytesAvailable(obj)
            obj.checkOpen();
            try
                n = double(obj.SerialPort.BytesToRead);
            catch err
                obj.throwPortError(err, "check");
            end
        end

        function set.BaudRate(obj, baudRate)
            obj.BaudRate = baudRate;
            if ~isempty(obj.SerialPort) %#ok<MCSUP>
                obj.SerialPort.BaudRate = baudRate; %#ok<MCSUP>
            end
        end

        function set.Timeout(obj, seconds)
            validateattributes(seconds, {'numeric'}, {'scalar', 'positive', 'finite'});
            obj.Timeout = seconds;
            if ~isempty(obj.SerialPort) %#ok<MCSUP> Keeps the write timeout in step
                obj.SerialPort.WriteTimeout = round(seconds * 1000); %#ok<MCSUP>
            end
        end

        function write(obj, data, datatype)
            % Writes data, converted to datatype (e.g. 'uint8'), little-endian
            obj.checkOpen();
            bytes = pulsepal.DotNetSerialPort.toBytes(data, datatype);
            try
                obj.SerialPort.Write(NET.convertArray(bytes, 'System.Byte'), 0, numel(bytes));
            catch err
                obj.throwPortError(err, "write to");
            end
        end

        function data = read(obj, count, datatype)
            % Reads count values of datatype. Returns them as a row of doubles (char for 'char'), or fewer, with a
            % warning, if they do not arrive within Timeout seconds.
            obj.checkOpen();
            valueBytes = pulsepal.DotNetSerialPort.bytesPerValue(datatype);
            nBytes = count * valueBytes;
            buffer = NET.createArray('System.Byte', max(nBytes, 1));
            received = 0;
            startTime = tic;
            while received < nBytes
                try
                    % Returns as soon as any bytes have arrived, or throws after ReadSliceMs
                    received = received + double(obj.SerialPort.Read(buffer, received, nBytes - received));
                catch err
                    if ~pulsepal.DotNetSerialPort.isTimeout(err)
                        obj.throwPortError(err, "read from");
                    end
                    if toc(startTime) >= obj.Timeout
                        warning('pulsepal:DotNetSerialPort:ReadTimeout', ...
                            '%s: %d of %d bytes arrived within the %g s timeout.', obj.Port, received, nBytes, ...
                            obj.Timeout);
                        break
                    end
                end
            end
            bytes = uint8(buffer);
            bytes = bytes(1:floor(received / valueBytes) * valueBytes);
            if strcmp(datatype, 'char')
                data = char(bytes(:)');
            elseif strcmp(datatype, 'uint8')
                data = double(bytes(:)');
            else
                data = double(typecast(bytes(:)', datatype));
            end
        end

        function setDTR(obj, state)
            obj.checkOpen();
            obj.SerialPort.DtrEnable = logical(state);
        end

        function flush(obj)
            % Discards bytes received and not yet read, and bytes not yet sent
            obj.checkOpen();
            try
                obj.SerialPort.DiscardInBuffer();
                obj.SerialPort.DiscardOutBuffer();
            catch err
                obj.throwPortError(err, "flush");
            end
        end
    end

    methods (Access = private)
        function closePort(obj)
            % Closes the port and releases it, even if the device is gone
            if isempty(obj.SerialPort)
                return
            end
            sp = obj.SerialPort;
            obj.SerialPort = [];
            try
                sp.Close(); % Throws if the device was unplugged
            catch
            end
            try
                obj.BaseStream.Dispose();
            catch
            end
            try
                sp.Dispose();
            catch
            end
            obj.BaseStream = [];
        end

        function checkOpen(obj)
            if isempty(obj.SerialPort)
                error('pulsepal:DotNetSerialPort:Closed', 'The serial port %s is closed.', obj.Port);
            end
        end

        function throwPortError(obj, err, action)
            % Rethrows a .NET exception as a MATLAB error that says what happened
            detail = err.message;
            type = "";
            if isa(err, 'NET.NetException')
                type = string(err.ExceptionObject.GetType().Name);
                detail = char(err.ExceptionObject.Message);
            end
            switch type
                case "UnauthorizedAccessException"
                    reason = 'It is in use by another program, or the device was unplugged.';
                case "IOException"
                    reason = 'The device may have been unplugged, or the port does not exist.';
                case "TimeoutException"
                    reason = sprintf('The device did not accept the data within the %g s timeout.', obj.Timeout);
                case "InvalidOperationException"
                    reason = 'The port is closed.';
                otherwise
                    reason = '';
            end
            error('pulsepal:DotNetSerialPort:PortError', 'Could not %s %s. %s (%s)', action, obj.Port, reason, ...
                detail);
        end
    end

    methods (Static, Access = private)
        function bytes = toBytes(data, datatype)
            if strcmp(datatype, 'uint8') || strcmp(datatype, 'char')
                bytes = uint8(data);
            else
                bytes = typecast(cast(data, datatype), 'uint8');
            end
            bytes = bytes(:)';
        end

        function n = bytesPerValue(datatype)
            switch datatype
                case {'uint8', 'int8', 'char'}
                    n = 1;
                case {'uint16', 'int16'}
                    n = 2;
                case {'uint32', 'int32', 'single'}
                    n = 4;
                case {'uint64', 'int64', 'double'}
                    n = 8;
                otherwise
                    error('pulsepal:DotNetSerialPort:DataType', 'Unsupported data type: %s', datatype);
            end
        end

        function tf = isTimeout(err)
            tf = isa(err, 'NET.NetException') && ...
                strcmp(char(err.ExceptionObject.GetType().Name), 'TimeoutException');
        end
    end
end

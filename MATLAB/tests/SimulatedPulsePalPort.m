% SimulatedPulsePalPort stands in for a Pulse Pal's serial port in /MATLAB/tests/testPulsePalDeviceOffline.m. It records
% every write, and replies as Pulse Pal firmware does (/Firmware/PROTOCOL.md): the handshake, the hardware info, a
% confirm byte to each command that sends one, and the parameters of the last op 92 to op 93. It has the methods of
% pulsepal.DotNetSerialPort that PulsePalDevice uses, so P = PulsePalDevice(SimulatedPulsePalPort()) connects to it.

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

classdef SimulatedPulsePalPort < handle
    properties
        Port = 'SIMULATED' % The port name PulsePalDevice reports
        Timeout = 10 % Unused: replies are ready at once
        writes = {} % Each write, as a row of uint8
        ack = 1 % The confirm byte: 1, or 0 to refuse every command
        loadReply = 1 % The confirm byte of a settings file load (op 90, operation 2)
        lastParams = uint8([]) % The parameters of the last op 92, which op 93 returns
    end

    properties (SetAccess = private)
        hardwareVersion
        firmwareVersion
        replies = uint8([]) % Bytes waiting to be read
    end

    properties (Dependent)
        NumBytesAvailable
    end

    methods
        function obj = SimulatedPulsePalPort(hardwareVersion, firmwareVersion)
            % SimulatedPulsePalPort() is a Pulse Pal 3 with firmware v22. SimulatedPulsePalPort(2, 21) is a Pulse Pal 2
            % with firmware v21.
            if nargin < 1
                hardwareVersion = 3;
            end
            if nargin < 2
                firmwareVersion = 22;
            end
            obj.hardwareVersion = hardwareVersion;
            obj.firmwareVersion = firmwareVersion;
        end

        function n = get.NumBytesAvailable(obj)
            n = numel(obj.replies);
        end

        function write(obj, data, ~)
            bytes = uint8(double(data(:)'));
            obj.writes{end+1} = bytes;
            obj.respond(bytes);
        end

        function data = read(obj, count, datatype)
            % Reads count values of datatype, as a row of doubles, or fewer, with a warning, if fewer are waiting
            switch datatype
                case {'uint8', 'char'}
                    valueBytes = 1;
                case {'uint16', 'int16'}
                    valueBytes = 2;
                otherwise
                    valueBytes = 4;
            end
            nBytes = min(count*valueBytes, floor(numel(obj.replies)/valueBytes)*valueBytes);
            if nBytes < count*valueBytes
                warning('SimulatedPulsePalPort:ReadTimeout', '%d of %d bytes were waiting.', nBytes, count*valueBytes);
            end
            bytes = obj.replies(1:nBytes);
            obj.replies(1:nBytes) = [];
            if any(strcmp(datatype, {'uint8', 'char'}))
                data = double(bytes);
            else
                data = double(typecast(bytes, datatype));
            end
        end

        function setDTR(~, ~)
        end

        function flush(obj)
            obj.replies = uint8([]);
        end

        function clearWrites(obj)
            obj.writes = {};
        end
    end

    methods (Access = private)
        function respond(obj, bytes)
            % Queues the firmware's reply to one command
            if numel(bytes) < 2 || bytes(1) ~= 213
                return
            end
            switch bytes(2)
                case 72 % Handshake: 'K', then the firmware version
                    obj.reply([75 typecast(uint32(obj.firmwareVersion), 'uint8')]);
                case 94 % Hardware info: version, timer period (us), custom trains, pulses per train
                    nTrains = 2 + 2*(obj.hardwareVersion > 2);
                    maxPulses = 5000 + 5000*(obj.hardwareVersion > 2);
                    obj.reply([obj.hardwareVersion typecast(uint32(50), 'uint8') nTrains ...
                               typecast(uint32(maxPulses), 'uint8')]);
                case 92 % All parameters
                    obj.lastParams = bytes(3:end);
                    obj.reply(obj.ack);
                case 93 % Read all parameters: op 92's layout, without the 4 continuous loop bytes
                    p = obj.lastParams;
                    obj.reply([p(1:168) p(173:end)]);
                case 90 % Settings file: operation, name length, name
                    if bytes(3) == 2
                        obj.reply(obj.loadReply);
                    else
                        obj.reply(obj.ack);
                    end
                case 97 % Format the microSD card: status text, then the confirm byte
                    obj.reply([uint8(['SUCCESS: Card format complete!' 13 10]) obj.ack]);
                case {73, 74, 75, 76, 79, 91, 95, 96, 99} % Commands that send a confirm byte
                    obj.reply(obj.ack);
            end % Ops 77, 80, 81, 89 and 98 send nothing
        end

        function reply(obj, bytes)
            obj.replies = [obj.replies uint8(bytes)];
        end
    end
end

% SimulatedPulsePalPort stands in for a Pulse Pal's serial port in /MATLAB/tests/testPulsePalDeviceOffline.m. It records
% every write, and replies as Pulse Pal firmware does (/Firmware/PROTOCOL.md): the handshakes (ops 103 and 72), the hardware info, the
% circuit board revision, a confirm byte to each command that sends one, and the parameters of the last op 92 to op 93. It has the methods of
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
        lateReply = uint8([]) % Sent before the reply to the next command, as the reply to a command an earlier session
                              % left queued on the device
        circuitBoardRevision % The circuit board revision op 100 reports: 5 on Pulse Pal 3, 0 on Pulse Pal 2
        silent = false % true: reply to nothing, as a device showing COMM. FAILURE! until its joystick is clicked
        answersOp103 = true % false: ignore op 103, as firmware from before October 2026 does (and Wave Pal, Synth Pal)
        cyclePeriod = 50 % The timer cycle, in us. At 10 (Pulse Pal 3), an op 72 session's times are in 50 us cycles
        status = uint8([5 typecast(uint32([3 0 2 0 1 0 1]), 'uint8')]) % The reply to op 101: channels 1 and 3 playing,
                                                                       % then the counters
    end

    properties (SetAccess = private)
        hardwareVersion
        firmwareVersion
        replies = uint8([]) % Bytes waiting to be read
        sessionCycles = 1 % The device cycles in each time unit of the session: 50 us cycles after op 72
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
            obj.circuitBoardRevision = 5*(hardwareVersion > 2); % Pulse Pal 2 boards encode no revision
        end

        function n = get.NumBytesAvailable(obj)
            n = numel(obj.replies);
        end

        function write(obj, data, ~)
            bytes = uint8(double(data(:)'));
            obj.writes{end+1} = bytes;
            obj.reply(obj.lateReply);
            obj.lateReply = uint8([]);
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

        function leaveUnread(obj, bytes)
            % Leaves bytes waiting to be read, as the end of a reply that an earlier session did not read
            obj.reply(bytes);
        end
    end

    methods (Access = private)
        function respond(obj, bytes)
            % Queues the firmware's reply to one command
            if numel(bytes) < 2 || bytes(1) ~= 213 || obj.silent
                return
            end
            switch bytes(2)
                case 72 % Handshake: 'K', then the firmware version. Times in 50 us cycles
                    obj.sessionCycles = 50/obj.cyclePeriod;
                    obj.reply([75 typecast(uint32(obj.firmwareVersion), 'uint8')]);
                case 103 % As op 72, and times in the device's own cycles
                    if obj.answersOp103
                        obj.sessionCycles = 1;
                        obj.reply([75 typecast(uint32(obj.firmwareVersion), 'uint8')]);
                    end
                case 94 % Hardware info: version, the session's time unit (us), custom trains, pulses per train
                    nTrains = 2 + 2*(obj.hardwareVersion > 2);
                    maxPulses = 5000 + 5000*(obj.hardwareVersion > 2);
                    period = obj.cyclePeriod*obj.sessionCycles;
                    obj.reply([obj.hardwareVersion typecast(uint32(period), 'uint8') nTrains ...
                               typecast(uint32(maxPulses), 'uint8')]);
                case 100 % Circuit board revision
                    obj.reply(obj.circuitBoardRevision);
                case 101 % Status
                    obj.reply(obj.status);
                case 102 % USB serial number: its length, then its characters. Pulse Pal 2 has none
                    if obj.hardwareVersion > 2
                        obj.reply([8 uint8('20345420')]);
                    else
                        obj.reply(0);
                    end
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
                case 97 % Format the microSD card, if the key follows: status text, then the confirm byte
                    if isequal(bytes(3:end), uint8('WIPE'))
                        obj.reply([uint8(['SUCCESS: Card format complete!' 13 10]) obj.ack]);
                    end
                case {73, 74, 75, 76, 79, 91, 95, 96, 99} % Commands that send a confirm byte
                    obj.reply(obj.ack);
            end % Ops 77, 80, 81, 89 and 98 send nothing
        end

        function reply(obj, bytes)
            obj.replies = [obj.replies uint8(bytes)];
        end
    end
end

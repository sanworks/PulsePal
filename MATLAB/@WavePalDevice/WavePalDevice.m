% WavePalDevice controls a Pulse Pal 3 running Wave Pal firmware (/Firmware/WavePal), which makes it a four
% channel waveform player. Each output channel plays one waveform of up to 1 million samples, stored on the
% device's microSD card, when it is triggered: by a TTL pulse on a trigger channel, from MATLAB with play(),
% or from the thumb joystick. Waveforms play at up to 100 kHz.
%
% Example:
%   W = WavePalDevice('COM3');           % Replace COM3 with the device's port. serialportlist lists them.
%   W.samplingRate = 50000;              % Hz, all channels
%   W.outputRange = '-5V:5V';            % All channels
%   t = (0:49999)/50000;                 % 1 second
%   W.loadWaveform(1, 4*sin(2*pi*10*t)); % A 10 Hz sine wave, +/-4 V, on output channel 1
%   W.play(1);
%   W.loopMode(1) = true;                % Loop channel 1's waveform...
%   W.loopDuration(1) = 3;               % ...for 3 seconds after each trigger
%   W.triggerMode{2} = 'Toggle';
%   clear W                              % Releases the port. The device keeps playing, and TTL triggers still work.
%
% Settings are properties, and assigning one programs the device at once. Settings of the output channels are
% 1x4 arrays with one element per channel, so W.loopMode(2) is output channel 2's loop mode. A single value
% sets all four channels, e.g. W.triggerMode = 'Gated'. Voltages are in volts, within outputRange. Times are
% in seconds.
%
% Triggers. A trigger is a rising edge on a trigger channel linked to the output channel (linkTriggerChannel1,
% linkTriggerChannel2), or a call to play(). What it does depends on the channel's triggerMode:
%   'Normal'  Starts the waveform. Triggers during playback are ignored.
%   'Master'  Starts the waveform, or restarts it from the first sample if it is playing.
%   'Toggle'  Starts the waveform, or stops it if it is playing.
%   'Gated'   Starts the waveform, and a falling edge on the trigger channel stops it, unless the other trigger
%             channel is also linked and still high. With loop mode on and a loop duration of 0, the waveform
%             plays for exactly as long as the TTL is high.
% When no channel is playing, a trigger starts the waveform within microseconds. A channel triggered while
% another plays starts on the next sample of the shared sample clock.
%
% Wave Pal's USB protocol is documented in /Firmware/WavePal/PROTOCOL.md. The Python class,
% /Python/PulsePal/WavePal.py, has the same features.

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

classdef WavePalDevice < handle
    % The class help is at the top of this file, above the license: MATLAB's help shows the first comment block in
    % a file, and a license block there would hide it.

    properties
        Port % The serialport object connected to the device
        samplingRate = 10000 % Sampling rate of all output channels, in Hz: a whole number from 1 to info.maxSamplingRate.
                             % It can change during playback. The rate played can differ slightly: see actualSamplingRate.
        outputRange = '' % Voltage range of all output channels: '0V:5V', '0V:10V', '-5V:5V' or '-10V:10V'. The smallest
                         % range that fits the waveforms gives the finest voltage steps. Changing it stops playback,
                         % sets the outputs to 0 V and loads the waveforms again, encoded for the new range.
        loopMode = false(1,4) % 1x4. true loops the channel's waveform until loopDuration has elapsed, or until stopped
                              % if loopDuration is 0. false plays it once. A change applies to playback in progress.
        loopDuration = zeros(1,4) % 1x4, in seconds. In loop mode, how long the channel plays after each trigger. It stops
                                  % part way through the waveform if need be. 0 loops until the channel is stopped.
        triggerMode = {'Normal', 'Normal', 'Normal', 'Normal'} % 1x4 cell array: 'Normal', 'Master', 'Toggle' or 'Gated'.
                                                               % See "Triggers" above. Not case sensitive.
        linkTriggerChannel1 = true(1,4) % 1x4. true if a rising edge on trigger channel 1 triggers the output channel
        linkTriggerChannel2 = false(1,4) % 1x4. true if a rising edge on trigger channel 2 triggers the output channel
    end

    properties (SetAccess = private)
        info % Properties of the connected device
        waveforms = cell(1,4) % 1x4 cell array of the waveforms loaded by loadWaveform(), in volts. [] if none.
                              % status() shows what the device itself holds, which can include waveforms loaded
                              % by an earlier connection.
    end

    properties (Dependent, SetAccess = private)
        actualSamplingRate % The sampling rate the device plays, in Hz. The device divides info.sampleClockHz (24 MHz)
                           % by a whole number, so e.g. 44100 Hz plays at 44117.6 Hz. Rates that divide 24 MHz,
                           % such as 10, 25 or 100 kHz, play exactly.
    end

    properties (Access = private)
        initialized = false % Assigning a setting programs the device only once the constructor has connected
    end

    properties (Constant, Access = private)
        CurrentFirmwareVersion = 1 % Most recent Wave Pal firmware version
        OpMenuByte = 213 % The first byte of every command
        OpHandshake = 72
        OpDisconnect = 81 % Shows the device's own name on its screen again
        OpSetClientName = 89 % Followed by 6 characters, shown as "NAME Connected"
        OpHardwareInfo = 'N'
        OpSetSamplingRate = 'S'
        OpSetOutputRange = 'R'
        OpLoadWaveform = 'L'
        OpPlay = 'P'
        OpStop = 'X'
        OpSetFixedVoltage = '!'
        OpSetLoopMode = 'O'
        OpSetLoopDuration = 'D'
        OpSetTriggerMode = 'T'
        OpSetTriggerLinks = 'I'
        OpGetStatus = 'G'
        OpGetPlaybackChecksums = 'Z'
        WavePalHandshakeReply = 87 % 'W'
        PulsePalHandshakeReply = 75 % 'K': the device runs Pulse Pal firmware
        OutputRangeNames = {'0V:5V', '0V:10V', '-5V:5V', '-10V:10V'} % In order of their index on the device
        OutputRangeLimits = [0 5; 0 10; -5 5; -10 10] % Volts, one row per range
        TriggerModeNames = {'Normal', 'Master', 'Toggle', 'Gated'} % In order of their code on the device
        DACBitMax = 65535
    end

    methods
        function obj = WavePalDevice(portString)
            % Opens the serial port, checks that the device runs Wave Pal firmware, reads its properties into
            % info, shows "MATLAB Connected" on the device's screen, stops any playback and programs the default
            % settings (see setDefaults).

            % Check for minimum MATLAB version. verLessThan works in releases older than the minimum, where
            % isMATLABReleaseOlderThan (introduced in R2020b) does not exist.
            MinVer = '9.9';
            MinVerName = 'R2020b';
            if verLessThan('matlab', MinVer) %#ok<VERLESSMATLAB>
                error(['WavePalDevice requires MATLAB ' MinVerName ' or newer.'])
            end

            if nargin < 1
                portList = serialportlist('available');
                if ~isempty(portList)
                    error(['You must call WavePalDevice with a serial port string argument, e.g. W = WavePalDevice(''COM3'')'...
                           newline 'Detected serial ports are: ' strjoin(cellstr(portList), ', ')])
                else
                    error('You must call WavePalDevice with a serial port string argument.')
                end
            end

            defaultBaudRate = 12000000; % USB serial ignores the baud rate
            if isunix
                defaultBaudRate = 4000000;
            end
            obj.Port = serialport(portString, defaultBaudRate);
            try
                setDTR(obj.Port, true);
                flush(obj.Port); % Discard anything left in the buffers by an earlier session
                obj.handshake(portString);
                obj.readHardwareInfo();
                obj.writeCommand(obj.OpSetClientName, 'MATLAB'); % Shown on the device's screen as "MATLAB Connected"
                obj.initialized = true;
                obj.stop();
                obj.setDefaults();
            catch err
                % Op 81 puts the device's own name back on its screen. It means something else to other devices,
                % so it is sent only once the device has identified itself as a Wave Pal.
                if isstruct(obj.info)
                    try
                        obj.writeCommand(obj.OpDisconnect, []);
                    catch
                        % The port may already be gone, e.g. the cable was unplugged
                    end
                end
                obj.Port = []; % Release the port, so that the next attempt can open it
                rethrow(err)
            end
        end

        function setDefaults(obj)
            % Programs the default settings on the device: a 10 kHz sampling rate, the -10 V to 10 V output range,
            % loop mode off with loop durations of 0, 'Normal' trigger mode, and all output channels linked to
            % trigger channel 1 and not to trigger channel 2. They match the settings the device starts with.
            % Loaded waveforms are kept, and loaded again if the output range changes. Raises an error if a loaded
            % waveform does not fit the default range.
            obj.samplingRate = 10000;
            obj.outputRange = '-10V:10V';
            obj.loopMode = false;
            obj.loopDuration = 0;
            obj.triggerMode = 'Normal';
            obj.linkTriggerChannel1 = true;
            obj.linkTriggerChannel2 = false;
        end

        function loadWaveform(obj, channel, waveform)
            % Loads a waveform onto an output channel.
            % channel: output channel number, 1-4
            % waveform: voltages of the samples, played at samplingRate: 1 to info.maxSamples values, all within
            %           outputRange.
            % The samples are written to the device's microSD card, and the first info.bufferSamples of them are
            % also kept in its RAM. Loading stops the channel if it is playing; other channels keep playing.
            % Example: W.loadWaveform(2, 3*sin(2*pi*100*(0:9999)/W.samplingRate));
            channel = obj.checkChannel(channel);
            if ~isnumeric(waveform) && ~islogical(waveform)
                error('The waveform must be a vector of voltages.')
            end
            samples = double(waveform(:)');
            nSamples = numel(samples);
            if nSamples < 1 || nSamples > obj.info.maxSamples
                error(['A waveform must have 1 to ' num2str(obj.info.maxSamples) ' samples. Received '...
                       num2str(nSamples) '.'])
            end
            codes = obj.volts2Codes(samples);
            obj.waveforms{channel} = []; % The device unloads the channel before it reads the new samples
            obj.writeCommand(obj.OpLoadWaveform, [uint8(channel) typecast(uint32(nSamples), 'uint8')...
                typecast(codes, 'uint8')]);
            obj.confirmWrite('loadWaveform()');
            obj.waveforms{channel} = samples;
        end

        function play(obj, channels)
            % Triggers output channels in software, e.g. W.play(1) or W.play([2 4]).
            % Each channel responds according to its triggerMode, as if a linked trigger channel had gone high.
            % Channels start on the same sample. Channels without a waveform are ignored.
            obj.writeCommand(obj.OpPlay, obj.channelBits(channels));
        end

        function stop(obj, channels)
            % Stops playback, e.g. W.stop() for all channels, or W.stop([1 3]). The stopped channels output 0 V.
            if nargin < 2
                bits = uint8(15);
            else
                bits = obj.channelBits(channels);
            end
            obj.writeCommand(obj.OpStop, bits);
        end

        function setFixedVoltage(obj, channels, voltage)
            % Sets output channels to a fixed voltage, e.g. W.setFixedVoltage([1 3], 2.5).
            % Stops playback on those channels. The voltage holds until the channel is triggered or stopped.
            bits = obj.channelBits(channels);
            if ~isscalar(voltage)
                error('setFixedVoltage() takes one voltage, which is set on every channel given.')
            end
            code = obj.volts2Codes(double(voltage));
            obj.writeCommand(obj.OpSetFixedVoltage, [bits typecast(code, 'uint8')]);
            obj.confirmWrite('setFixedVoltage()');
        end

        function deviceStatus = status(obj)
            % Returns the device's playback state, as a struct with fields:
            % playing: numbers of the output channels playing a waveform, e.g. [1 3]
            % samplesLoaded: 1x4, samples in each channel's waveform on the device, 0 if it has none
            % underruns: 1x4, blocks of samples that were not read from the microSD card in time, since the
            %            device started. The output holds its last value until the block arrives.
            % longestInterrupt_us: longest run of the device's playback interrupt since the previous call to
            %                      status(), in microseconds. It must stay below the sample period.
            obj.writeCommand(obj.OpGetStatus, []);
            reply = obj.readBytes(37, 'status()');
            values = double(typecast(uint8(reply(2:end)), 'uint32'));
            deviceStatus = struct;
            deviceStatus.playing = find(bitget(reply(1), 1:4));
            deviceStatus.samplesLoaded = values(1:4);
            deviceStatus.underruns = values(5:8);
            deviceStatus.longestInterrupt_us = values(9)/1000;
        end

        function rate = get.actualSamplingRate(obj)
            clockHz = obj.info.sampleClockHz;
            rate = clockHz/round(clockHz/obj.samplingRate);
        end

        function set.samplingRate(obj, rate)
            if ~isnumeric(rate) || ~isscalar(rate) || ~isreal(rate) || rate ~= round(rate) || rate < 1
                error('samplingRate must be a whole number of Hz.')
            end
            rate = double(rate);
            if obj.initialized %#ok<MCSUP> The device must be connected
                if rate > obj.info.maxSamplingRate %#ok<MCSUP>
                    error(['samplingRate must be a whole number of Hz from 1 to ' num2str(obj.info.maxSamplingRate) '.']) %#ok<MCSUP>
                end
                % Loop durations are sent in samples, so check that they still fit before anything is changed
                loopSamples = obj.durations2Samples(obj.loopDuration, rate); %#ok<MCSUP>
                obj.writeCommand(obj.OpSetSamplingRate, typecast(uint32(rate), 'uint8'));
                obj.confirmWrite('setting samplingRate');
                if any(loopSamples > 0)
                    obj.writeCommand(obj.OpSetLoopDuration, typecast(uint32(loopSamples), 'uint8'));
                    obj.confirmWrite('setting loopDuration');
                end
            end
            obj.samplingRate = rate;
        end

        function set.outputRange(obj, rangeName)
            [rangeName, rangeIndex] = obj.findRange(rangeName);
            rangeChanged = ~strcmp(rangeName, obj.outputRange);
            if obj.initialized %#ok<MCSUP>
                limits = obj.OutputRangeLimits(rangeIndex, :);
                for i = 1:4
                    wave = obj.waveforms{i}; %#ok<MCSUP>
                    if ~isempty(wave) && (min(wave) < limits(1) || max(wave) > limits(2))
                        error(['The waveform on channel ' num2str(i) ' spans ' num2str(min(wave)) ' V to '...
                               num2str(max(wave)) ' V, which does not fit the ' rangeName ' range. Load a new '...
                               'waveform first, or choose a wider range.'])
                    end
                end
                obj.writeCommand(obj.OpSetOutputRange, uint8(rangeIndex-1));
                obj.confirmWrite('setting outputRange');
            end
            obj.outputRange = rangeName;
            if obj.initialized && rangeChanged %#ok<MCSUP>
                % The device unloads the waveforms when the range changes, because their samples encode voltages
                % in the old range. Load them again, encoded for the new one.
                oldWaveforms = obj.waveforms; %#ok<MCSUP>
                obj.waveforms = cell(1,4); %#ok<MCSUP>
                for i = 1:4
                    if ~isempty(oldWaveforms{i})
                        obj.loadWaveform(i, oldWaveforms{i});
                    end
                end
            end
        end

        function set.loopMode(obj, modes)
            modes = obj.checkLogical(modes, 'loopMode');
            if obj.initialized %#ok<MCSUP>
                obj.writeCommand(obj.OpSetLoopMode, uint8(modes));
                obj.confirmWrite('setting loopMode');
            end
            obj.loopMode = modes;
        end

        function set.loopDuration(obj, durations)
            durations = obj.expandToChannels(durations, 'loopDuration');
            if ~isnumeric(durations) || ~isreal(durations) || any(~isfinite(durations)) || any(durations < 0)
                error('loopDuration must be 0 (loop until stopped) or a positive number of seconds.')
            end
            durations = double(durations);
            if obj.initialized %#ok<MCSUP>
                loopSamples = obj.durations2Samples(durations, obj.samplingRate); %#ok<MCSUP>
                obj.writeCommand(obj.OpSetLoopDuration, typecast(uint32(loopSamples), 'uint8'));
                obj.confirmWrite('setting loopDuration');
            end
            obj.loopDuration = durations;
        end

        function set.triggerMode(obj, modes)
            if ischar(modes) || (isstring(modes) && isscalar(modes))
                modes = repmat(cellstr(modes), 1, 4);
            elseif isstring(modes)
                modes = cellstr(modes);
            end
            if ~iscell(modes) || numel(modes) ~= 4
                error(['triggerMode needs one mode for all channels, or a 1x4 cell array with one mode per output '...
                       'channel, e.g. {''Normal'', ''Normal'', ''Toggle'', ''Normal''}.'])
            end
            modeCodes = zeros(1,4);
            for i = 1:4
                match = [];
                if ischar(modes{i}) || (isstring(modes{i}) && isscalar(modes{i}))
                    match = find(strcmpi(modes{i}, obj.TriggerModeNames));
                end
                if isempty(match)
                    error(['Unknown trigger mode for channel ' num2str(i) '. Valid modes are: '...
                           strjoin(obj.TriggerModeNames, ', ') '.'])
                end
                modeCodes(i) = match - 1;
            end
            if obj.initialized %#ok<MCSUP>
                obj.writeCommand(obj.OpSetTriggerMode, uint8(modeCodes));
                obj.confirmWrite('setting triggerMode');
            end
            obj.triggerMode = obj.TriggerModeNames(modeCodes+1);
        end

        function set.linkTriggerChannel1(obj, links)
            links = obj.checkLogical(links, 'linkTriggerChannel1');
            if obj.initialized %#ok<MCSUP>
                obj.sendTriggerLinks(links, obj.linkTriggerChannel2); %#ok<MCSUP>
            end
            obj.linkTriggerChannel1 = links;
        end

        function set.linkTriggerChannel2(obj, links)
            links = obj.checkLogical(links, 'linkTriggerChannel2');
            if obj.initialized %#ok<MCSUP>
                obj.sendTriggerLinks(obj.linkTriggerChannel1, links); %#ok<MCSUP>
            end
            obj.linkTriggerChannel2 = links;
        end

        function delete(obj)
            % Releases the serial port, and shows the device's own name on its screen again in place of
            % "MATLAB Connected". The device keeps its settings and waveforms, and playback in progress continues,
            % so TTL triggers keep playing the loaded waveforms.
            if obj.initialized
                try
                    obj.writeCommand(obj.OpDisconnect, []);
                catch
                    % The port may already be gone, e.g. the cable was unplugged
                end
            end
            obj.Port = [];
        end
    end

    methods (Hidden)
        function [samplesPlayed, sums] = playbackChecksums(obj)
            % For testing: what each channel played since it last started. Returns two 1x4 arrays: the number
            % of samples played, and the sum of their DAC codes modulo 2^32. /MATLAB/tests/testWavePalDevice.m
            % compares them with the waveforms it loaded, to check every sample the device played.
            obj.writeCommand(obj.OpGetPlaybackChecksums, []);
            values = double(typecast(uint8(obj.readBytes(32, 'playbackChecksums()')), 'uint32'));
            samplesPlayed = values(1:4);
            sums = values(5:8);
        end
    end

    methods (Access = private)
        function handshake(obj, portString)
            % Checks that the device runs a supported Wave Pal firmware
            obj.Port.Timeout = 2; % A Wave Pal replies at once, so do not wait long for another kind of device
            obj.writeCommand(obj.OpHandshake, []);
            reply = read(obj.Port, 1, 'uint8');
            obj.Port.Timeout = 10;
            if isempty(reply)
                error(['No reply from the device on ' char(portString) '. Is it a Pulse Pal 3 running Wave Pal firmware?'])
            end
            if reply == obj.PulsePalHandshakeReply
                version = typecast(uint8(obj.readBytes(4, 'the handshake')), 'uint32');
                error(['The device on ' char(portString) ' runs Pulse Pal firmware (v' num2str(version) ').'...
                       newline 'Load Wave Pal firmware onto it (/Firmware/WavePal), or connect with PulsePalDevice.'])
            end
            if reply ~= obj.WavePalHandshakeReply
                error(['The device on ' char(portString) ' returned an unexpected handshake signature.'])
            end
            firmwareVersion = double(typecast(uint8(obj.readBytes(4, 'the handshake')), 'uint32'));
            if firmwareVersion > obj.CurrentFirmwareVersion
                error(['Error: Wave Pal with future firmware detected (v' num2str(firmwareVersion) ').'...
                       newline 'Please update your MATLAB software or load Wave Pal firmware v'...
                       num2str(obj.CurrentFirmwareVersion) '.'])
            end
            obj.info = struct;
            obj.info.firmwareVersion = firmwareVersion;
        end

        function readHardwareInfo(obj)
            obj.writeCommand(obj.OpHardwareInfo, []);
            reply = obj.readBytes(18, 'reading the hardware info');
            values = double(typecast(uint8(reply(3:end)), 'uint32'));
            obj.info.hardwareVersion = reply(1);
            obj.info.nChannels = reply(2);
            obj.info.maxSamples = values(1); % Samples per waveform
            obj.info.maxSamplingRate = values(2); % Hz
            obj.info.bufferSamples = values(3); % Samples of each waveform kept in the device's RAM
            obj.info.sampleClockHz = values(4); % Clock that the sample rate is divided from
            obj.info.outputRanges = obj.OutputRangeNames;
            obj.info.triggerModes = obj.TriggerModeNames;
        end

        function sendTriggerLinks(obj, links1, links2)
            % One command programs the links of both trigger channels
            obj.writeCommand(obj.OpSetTriggerLinks, uint8([links1 links2]));
            obj.confirmWrite('setting the trigger channel links');
        end

        function samples = durations2Samples(obj, durations, rate)
            % Converts loop durations in seconds to samples at a sampling rate
            clockHz = obj.info.sampleClockHz;
            actualRate = clockHz/round(clockHz/rate);
            samples = round(durations*actualRate);
            samples(durations > 0 & samples == 0) = 1; % 0 samples would mean "loop until stopped"
            if any(samples > double(intmax('uint32')))
                error(['A loopDuration is too long at ' num2str(rate) ' Hz. The longest is '...
                       num2str(floor(double(intmax('uint32'))/actualRate)) ' s.'])
            end
        end

        function codes = volts2Codes(obj, volts)
            % Converts voltages to DAC codes in the current output range
            limits = obj.OutputRangeLimits(strcmp(obj.outputRange, obj.OutputRangeNames), :);
            if any(~isfinite(volts))
                error('Voltages must be finite numbers.')
            end
            if min(volts) < limits(1) || max(volts) > limits(2)
                error(['Voltages must be within the output range, ' num2str(limits(1)) ' V to ' num2str(limits(2))...
                       ' V. Received ' num2str(min(volts)) ' V to ' num2str(max(volts)) ' V. Change outputRange'...
                       ' to use a wider range.'])
            end
            codes = uint16(round((volts - limits(1))/(limits(2) - limits(1))*obj.DACBitMax));
        end

        function [rangeName, rangeIndex] = findRange(obj, rangeName)
            % Returns the canonical name of an output range, and its index in OutputRangeNames
            rangeIndex = [];
            if ischar(rangeName) || (isstring(rangeName) && isscalar(rangeName))
                rangeIndex = find(strcmpi(strrep(char(rangeName), ' ', ''), obj.OutputRangeNames));
            end
            if isempty(rangeIndex)
                error(['Unknown output range. Valid ranges are: ' strjoin(obj.OutputRangeNames, ', ') '.'])
            end
            rangeName = obj.OutputRangeNames{rangeIndex};
        end

        function values = expandToChannels(~, values, name)
            % Returns one value per output channel, as a 1x4 row, from a single value or four values
            if isscalar(values)
                values = repmat(values, 1, 4);
            elseif numel(values) == 4
                values = reshape(values, 1, 4);
            else
                error([name ' needs one value for all channels, or one value per output channel (1x4).'])
            end
        end

        function values = checkLogical(obj, values, name)
            values = obj.expandToChannels(values, name);
            if ~(islogical(values) || isnumeric(values)) || any(values ~= 0 & values ~= 1)
                error([name ' values must be true or false (1 or 0).'])
            end
            values = logical(values);
        end

        function channel = checkChannel(~, channel)
            if ~isnumeric(channel) || ~isscalar(channel) || ~ismember(channel, 1:4)
                error('Output channels are numbered 1-4.')
            end
            channel = double(channel);
        end

        function bits = channelBits(~, channels)
            % Converts a list of output channel numbers to one bit per channel (bit 0 = channel 1)
            if ~isnumeric(channels) || isempty(channels) || ~all(ismember(channels(:), 1:4))
                error('Output channels are numbered 1-4, e.g. 1 or [2 4].')
            end
            bits = uint8(sum(bitshift(1, unique(channels(:))' - 1)));
        end

        function writeCommand(obj, opCode, data)
            % Sends one command, with its framing byte, in a single write
            write(obj.Port, [uint8([obj.OpMenuByte double(opCode)]) uint8(data)], 'uint8');
        end

        function data = readBytes(obj, nBytes, context)
            % Reads exactly nBytes from the device, as a row of doubles
            data = read(obj.Port, nBytes, 'uint8');
            if numel(data) < nBytes
                error(['Wave Pal did not reply in time to ' context '. ' num2str(numel(data)) ' of '...
                       num2str(nBytes) ' byte(s) arrived.'])
            end
        end

        function confirmWrite(obj, context)
            % Reads the device's one byte confirmation: 1 if it executed the command, 0 if it rejected it
            reply = read(obj.Port, 1, 'uint8');
            if isempty(reply)
                error(['Wave Pal did not confirm ' context '.'])
            end
            if reply ~= 1
                error(['Wave Pal rejected ' context '. A value was out of range, or a waveform could not be '...
                       'written to the microSD card.'])
            end
        end
    end
end

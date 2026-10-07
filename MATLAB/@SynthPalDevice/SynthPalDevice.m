% SynthPalDevice controls a Pulse Pal 3 running Synth Pal firmware (/Firmware/SynthPal), which makes it a four
% channel waveform synthesizer. Each output channel plays a sine, triangle, square or sawtooth wave when it is
% triggered: by a TTL pulse on a trigger channel, from MATLAB with play(), or from the thumb joystick. Each channel
% has its own waveform, amplitude, resting voltage and play duration, and one frequency, 1 Hz to 20 kHz in steps of
% 0.01 Hz, applies to all four.
%
% Example:
%   S = SynthPalDevice('COM3');        % Replace COM3 with the device's port. serialportlist lists them.
%   S.frequency = 440;                 % Hz, all channels
%   S.waveform{1} = 'Triangle';
%   S.amplitude(1) = 4;                % Volts peak to peak
%   S.restingVoltage(1) = 1;           % The waveform swings 2 V above and below 1 V
%   S.playDuration(1) = 0.5;           % Seconds. 0 plays until stopped.
%   S.play(1);
%   S.triggerMode{2} = 'Gated';        % Trigger channel 2
%   clear S                            % Releases the port. The device keeps playing, and TTL triggers still work.
%
% Settings are properties, and assigning one programs the device at once. Settings of the output channels are
% 1x4 arrays with one element per channel, so S.amplitude(2) is output channel 2's amplitude; triggerMode is 1x2,
% with one element per trigger channel. A single value sets all channels, e.g. S.waveform = 'Square'. Voltages are in
% volts, times in seconds, and the frequency in Hz.
%
% Levels. A channel's waveform swings amplitude/2 above and below its restingVoltage, which the channel outputs while
% idle, and which is the waveform's mean. It must stay within -10 V to 10 V: abs(restingVoltage) + amplitude/2 <= 10.
% To raise an amplitude beyond what the resting voltage allows, change the resting voltage first. The device picks
% each channel's output range for the finest voltage steps: see status().
%
% Waveforms start at the trigger: 'Sine' and 'Triangle' at the resting voltage, rising; 'Square' high for the first
% half of each cycle; 'Sawtooth' rising from its lowest voltage to its highest, and falling back at the cycle's end.
%
% Triggers. triggerMode sets how each trigger channel acts on the output channels linked to it (linkTriggerChannel1,
% linkTriggerChannel2). These are Pulse Pal's trigger modes:
%   'Normal'  A rising edge starts the linked channels. Channels that are playing ignore it.
%   'Toggle'  A rising edge starts the linked channels, or stops those that are playing.
%   'Gated'   A rising edge starts the linked channels, and a falling edge stops them, unless the other trigger
%             channel is also gated, linked to them, and still high. With a playDuration of 0, a channel plays for
%             exactly as long as the TTL is high.
% play() starts idle channels, and channels that are playing ignore it. When no channel is playing, a trigger starts
% the waveform 8 microseconds later. A channel triggered while another plays starts on the next sample of the shared
% sample clock.
%
% Sampling. Each cycle is samplesPerCycle samples: the largest multiple of 4 whose sampling rate (samplingRate, which
% is samplesPerCycle * frequency) is at most 100 kHz, so a sample falls on every edge, peak and trough. The frequency
% played is exact.
%
% Synth Pal's USB protocol is documented in /Firmware/SynthPal/PROTOCOL.md. The Python class,
% /Python/PulsePal/SynthPal.py, has the same features.

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

classdef SynthPalDevice < handle
    % The class help is at the top of this file, above the license: MATLAB's help shows the first comment block in
    % a file, and a license block there would hide it.

    properties
        Port % The serial port connected to the device: a pulsepal.DotNetSerialPort on Windows, otherwise a serialport
        frequency = 100 % Frequency of all output channels, in Hz: 1 to 20000, rounded to 0.01 Hz. It can change during
                        % playback: playing channels carry on from the same point in their cycle, and keep the time
                        % they have left to play.
        waveform = {'Sine', 'Sine', 'Sine', 'Sine'} % 1x4 cell array: 'Sine', 'Triangle', 'Square' or 'Sawtooth'. Not
                                                    % case sensitive. A change applies to playback in progress.
        amplitude = [5 5 5 5] % 1x4, peak to peak, in volts: 0 to 20. See "Levels" above.
        restingVoltage = [0 0 0 0] % 1x4, in volts: -10 to 10. Output while the channel is idle, and the waveform's
                                   % mean. See "Levels" above.
        playDuration = [1 1 1 1] % 1x4, in seconds: how long the channel plays after a trigger, up to
                                 % info.maxPlayDuration. 0 plays until stopped. Counted in samples.
        triggerMode = {'Normal', 'Normal'} % 1x2 cell array, one per trigger channel: 'Normal', 'Toggle' or 'Gated'.
                                           % See "Triggers" above. Not case sensitive.
        linkTriggerChannel1 = true(1,4) % 1x4. true if trigger channel 1 triggers the output channel
        linkTriggerChannel2 = false(1,4) % 1x4. true if trigger channel 2 triggers the output channel
    end

    properties (SetAccess = private)
        info % Properties of the connected device
        samplesPerCycle = 1000 % Samples in one cycle of the waveform at the current frequency. See "Sampling" above.
    end

    properties (Dependent, SetAccess = private)
        samplingRate % The rate at which the device plays samples, in Hz: samplesPerCycle * frequency. 100 kHz or
                     % just below it, and at least 50 kHz.
    end

    properties (Access = private)
        initialized = false % Assigning a setting programs the device only once the constructor has connected
    end

    properties (Constant, Access = private)
        CurrentFirmwareVersion = 1 % Most recent Synth Pal firmware version
        OpMenuByte = 213 % The first byte of every command
        OpHandshake = 72
        OpDisconnect = 81 % Shows the device's own name on its screen again
        OpSetClientName = 89 % Followed by 6 characters, shown as "NAME Connected"
        OpSetScreenSaver = 99
        OpHardwareInfo = 'N'
        OpSetFrequency = 'F'
        OpSetWaveform = 'W'
        OpSetAmplitude = 'A'
        OpSetRestingVoltage = 'V'
        OpSetPlayDuration = 'D'
        OpSetTriggerLinks = 'I'
        OpSetTriggerMode = 'T'
        OpPlay = 'P'
        OpStop = 'X'
        OpGetStatus = 'G'
        OpGetPlaybackChecksums = 'Z'
        SynthPalHandshakeReply = 83 % 'S'
        PulsePalHandshakeReply = 75 % 'K': the device runs Pulse Pal firmware
        WavePalHandshakeReply = 87 % 'W': the device runs Wave Pal firmware
        WaveformNames = {'Sine', 'Triangle', 'Square', 'Sawtooth'} % In order of their code on the device
        TriggerModeNames = {'Normal', 'Toggle', 'Gated'} % In order of their code on the device
        OutputRangeNames = {'0V:5V', '0V:10V', '-5V:5V', '-10V:10V'} % In order of their index on the device
        MaxVoltage_uV = 10000000 % Every output voltage stays within +/-10 V
    end

    methods
        function obj = SynthPalDevice(portString)
            % Opens the serial port, checks that the device runs Synth Pal firmware, reads its properties into
            % info, shows "MATLAB Connected" on the device's screen, stops any playback and programs the default
            % settings (see setDefaults).

            % Check for minimum MATLAB version. verLessThan works in releases older than the minimum, where
            % isMATLABReleaseOlderThan (introduced in R2020b) does not exist.
            MinVer = '9.9';
            MinVerName = 'R2020b';
            if verLessThan('matlab', MinVer) %#ok<VERLESSMATLAB>
                error(['SynthPalDevice requires MATLAB ' MinVerName ' or newer.'])
            end

            if nargin < 1
                portList = serialportlist('available');
                if ~isempty(portList)
                    error(['You must call SynthPalDevice with a serial port string argument, e.g. S = SynthPalDevice(''COM3'')'...
                           newline 'Detected serial ports are: ' strjoin(cellstr(portList), ', ')])
                else
                    error('You must call SynthPalDevice with a serial port string argument.')
                end
            end

            defaultBaudRate = 12000000; % USB serial ignores the baud rate
            if isunix
                defaultBaudRate = 4000000;
            end
            % On Windows, serialport delivers each reply about 16 ms after it arrives, so every command that waits for a
            % confirm byte took 16 ms. .NET's SerialPort takes about 0.3 ms (see pulsepal.DotNetSerialPort). It is not
            % available if MATLAB has been set to use .NET (Core) with dotnetenv, and serialport is used then.
            if pulsepal.DotNetSerialPort.isAvailable()
                obj.Port = pulsepal.DotNetSerialPort(portString, defaultBaudRate);
            else
                obj.Port = serialport(portString, defaultBaudRate);
            end
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
                % so it is sent only once the device has identified itself as a Synth Pal.
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
            % Programs the default settings on the device: a frequency of 100 Hz, and on every output channel a
            % sine wave of 5 V peak to peak around a resting voltage of 0 V, played for 1 second. Both trigger
            % channels in 'Normal' mode, and all output channels linked to trigger channel 1 and not to trigger
            % channel 2. They match the settings the device starts with.
            obj.frequency = 100;
            obj.waveform = 'Sine';
            obj.restingVoltage = 0; % First: 0 V is valid with any amplitude the device may hold
            obj.amplitude = 5;
            obj.playDuration = 1;
            obj.triggerMode = 'Normal';
            obj.linkTriggerChannel1 = true;
            obj.linkTriggerChannel2 = false;
        end

        function play(obj, channels)
            % Triggers output channels in software, e.g. S.play(1) or S.play([2 4]). Idle channels start from the
            % beginning of their waveform's cycle, together. Channels that are playing ignore it.
            obj.writeCommand(obj.OpPlay, obj.channelBits(channels));
        end

        function stop(obj, channels)
            % Stops playback, e.g. S.stop() for all channels, or S.stop([1 3]). The stopped channels return to their
            % resting voltage.
            if nargin < 2
                bits = uint8(15);
            else
                bits = obj.channelBits(channels);
            end
            obj.writeCommand(obj.OpStop, bits);
        end

        function deviceStatus = status(obj)
            % Returns the device's playback state, as a struct with fields:
            % playing: numbers of the output channels that are playing, e.g. [1 3]
            % samplesPerCycle: samples in one cycle of the waveform
            % outputRanges: 1x4 cell array, the output range the device chose for each channel: the one with the
            %               finest steps that holds its waveform: '0V:5V' (76 uV steps), then '0V:10V' or '-5V:5V'
            %               (153 uV), then '-10V:10V' (305 uV)
            % longestInterrupt_us: longest run of the device's sample clock interrupt since the previous call to
            %                      status(), in microseconds. It must stay below the sample period.
            % lateUpdates: output updates since the previous call to status() that may have come later than their
            %              fixed time after a sample clock tick. Normally 0; a frequency change during playback can
            %              make one late.
            obj.writeCommand(obj.OpGetStatus, []);
            reply = obj.readBytes(17, 'status()');
            deviceStatus = struct;
            deviceStatus.playing = find(bitget(reply(1), 1:4));
            deviceStatus.samplesPerCycle = double(typecast(uint8(reply(2:5)), 'uint32'));
            deviceStatus.outputRanges = obj.OutputRangeNames(reply(6:9) + 1);
            deviceStatus.longestInterrupt_us = double(typecast(uint8(reply(10:13)), 'uint32'))/1000;
            deviceStatus.lateUpdates = double(typecast(uint8(reply(14:17)), 'uint32'));
        end

        function setScreenSaver(obj, state, timeout)
            % Switches the device's screen saver on (state = 1 or true) or off (state = 0 or false), and sets its timeout
            % in seconds (a whole number, 1-65535; 1800 if left out). With the screen saver on, the device dims its
            % screen once it has been left alone for the timeout: no command from the computer, no rising edge on a
            % trigger channel, and no joystick click or push. The next of these brings the screen back. Both settings
            % are kept in the device's EEPROM, shared with Pulse Pal firmware, and saved once no channel is playing.
            if nargin < 3
                timeout = 1800;
            end
            if ~(isnumeric(state) || islogical(state)) || ~isscalar(state) || ~(state == 0 || state == 1) % Also refuses NaN
                error('state must be 1 (on) or 0 (off)')
            end
            if ~isnumeric(timeout) || ~isscalar(timeout) || ~(timeout >= 1 && timeout <= 65535) || timeout ~= round(timeout)
                error('timeout must be a whole number of seconds from 1 to 65535')
            end
            obj.writeCommand(obj.OpSetScreenSaver, [uint8(state) typecast(uint16(timeout), 'uint8')]);
            obj.confirmWrite('setScreenSaver()');
        end

        function rate = get.samplingRate(obj)
            rate = obj.samplesPerCycle*obj.frequency;
        end

        function set.frequency(obj, value)
            if ~isnumeric(value) || ~isscalar(value) || ~isreal(value) || ~isfinite(value)
                error('frequency must be a number of Hz.')
            end
            centiHz = obj.roundHalfEven(double(value)*100);
            if centiHz < 100 || centiHz > 2000000
                error('frequency must be 1 to 20000 Hz.')
            end
            if obj.initialized %#ok<MCSUP> The device must be connected
                obj.writeCommand(obj.OpSetFrequency, typecast(uint32(centiHz), 'uint8'));
                reply = obj.readBytes(5, 'setting frequency');
                if reply(1) ~= 1
                    error('Synth Pal rejected the frequency.')
                end
                obj.samplesPerCycle = double(typecast(uint8(reply(2:5)), 'uint32')); %#ok<MCSUP>
            end
            obj.frequency = centiHz/100;
        end

        function set.waveform(obj, names)
            codes = obj.namesToCodes(names, obj.WaveformNames, 4, 'waveform', 'output channel');
            if obj.initialized %#ok<MCSUP>
                obj.writeCommand(obj.OpSetWaveform, uint8(codes));
                obj.confirmWrite('setting waveform');
            end
            obj.waveform = obj.WaveformNames(codes+1);
        end

        function set.amplitude(obj, volts)
            volts = obj.checkVolts(volts, 'amplitude', 0, 20);
            microvolts = obj.roundHalfEven(volts*1e6);
            obj.checkLevels(microvolts, obj.roundHalfEven(obj.restingVoltage*1e6), 'amplitude'); %#ok<MCSUP>
            if obj.initialized %#ok<MCSUP>
                obj.writeCommand(obj.OpSetAmplitude, typecast(uint32(microvolts), 'uint8'));
                obj.confirmWrite('setting amplitude');
            end
            obj.amplitude = volts;
        end

        function set.restingVoltage(obj, volts)
            volts = obj.checkVolts(volts, 'restingVoltage', -10, 10);
            microvolts = obj.roundHalfEven(volts*1e6);
            obj.checkLevels(obj.roundHalfEven(obj.amplitude*1e6), microvolts, 'restingVoltage'); %#ok<MCSUP>
            if obj.initialized %#ok<MCSUP>
                obj.writeCommand(obj.OpSetRestingVoltage, typecast(int32(microvolts), 'uint8'));
                obj.confirmWrite('setting restingVoltage');
            end
            obj.restingVoltage = volts;
        end

        function set.playDuration(obj, seconds)
            seconds = obj.expandToChannels(seconds, 'playDuration');
            maxDuration = 3600;
            if isstruct(obj.info) %#ok<MCSUP>
                maxDuration = obj.info.maxPlayDuration; %#ok<MCSUP>
            end
            if ~isnumeric(seconds) || ~isreal(seconds) || any(~isfinite(seconds)) || any(seconds < 0) || ...
                    any(seconds > maxDuration)
                error(['playDuration must be 0 (play until stopped) or a positive number of seconds up to '...
                       num2str(maxDuration) '.'])
            end
            seconds = double(seconds);
            if obj.initialized %#ok<MCSUP>
                obj.writeCommand(obj.OpSetPlayDuration, typecast(uint32(obj.roundHalfEven(seconds*1e6)), 'uint8'));
                obj.confirmWrite('setting playDuration');
            end
            obj.playDuration = seconds;
        end

        function set.triggerMode(obj, modes)
            codes = obj.namesToCodes(modes, obj.TriggerModeNames, 2, 'triggerMode', 'trigger channel');
            if obj.initialized %#ok<MCSUP>
                obj.writeCommand(obj.OpSetTriggerMode, uint8(codes));
                obj.confirmWrite('setting triggerMode');
            end
            obj.triggerMode = obj.TriggerModeNames(codes+1);
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
            % "MATLAB Connected". The device keeps its settings, and playback in progress continues, so TTL triggers
            % keep playing the channels.
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
            % of samples played, and the sum of their DAC codes modulo 2^32. /MATLAB/tests/testSynthPalDevice.m
            % uses them to check what the device played.
            obj.writeCommand(obj.OpGetPlaybackChecksums, []);
            values = double(typecast(uint8(obj.readBytes(32, 'playbackChecksums()')), 'uint32'));
            samplesPlayed = values(1:4);
            sums = values(5:8);
        end
    end

    methods (Access = private)
        function handshake(obj, portString)
            % Checks that the device runs a supported Synth Pal firmware
            obj.Port.Timeout = 2; % A Synth Pal replies at once, so do not wait long for another kind of device
            obj.writeCommand(obj.OpHandshake, []);
            reply = read(obj.Port, 1, 'uint8');
            obj.Port.Timeout = 10;
            if isempty(reply)
                error(['No reply from the device on ' char(portString) '. Is it a Pulse Pal 3 running Synth Pal firmware?'])
            end
            if reply == obj.PulsePalHandshakeReply || reply == obj.WavePalHandshakeReply
                if reply == obj.PulsePalHandshakeReply
                    names = {'Pulse Pal', 'PulsePalDevice'};
                else
                    names = {'Wave Pal', 'WavePalDevice'};
                end
                version = typecast(uint8(obj.readBytes(4, 'the handshake')), 'uint32');
                error(['The device on ' char(portString) ' runs ' names{1} ' firmware (v' num2str(version) ').'...
                       newline 'Load Synth Pal firmware onto it (/Firmware/SynthPal), or connect with ' names{2} '.'])
            end
            if reply ~= obj.SynthPalHandshakeReply
                error(['The device on ' char(portString) ' returned an unexpected handshake signature.'])
            end
            firmwareVersion = double(typecast(uint8(obj.readBytes(4, 'the handshake')), 'uint32'));
            if firmwareVersion > obj.CurrentFirmwareVersion
                error(['Error: Synth Pal with future firmware detected (v' num2str(firmwareVersion) ').'...
                       newline 'Please update your MATLAB software or load Synth Pal firmware v'...
                       num2str(obj.CurrentFirmwareVersion) '.'])
            end
            obj.info = struct;
            obj.info.firmwareVersion = firmwareVersion;
        end

        function readHardwareInfo(obj)
            obj.writeCommand(obj.OpHardwareInfo, []);
            reply = obj.readBytes(22, 'reading the hardware info');
            values = double(typecast(uint8(reply(3:end)), 'uint32'));
            obj.info.hardwareVersion = reply(1);
            obj.info.nChannels = reply(2);
            obj.info.minFrequency = values(1)/100; % Hz
            obj.info.maxFrequency = values(2)/100; % Hz
            obj.info.maxSamplingRate = values(3); % Hz
            obj.info.timerClockHz = values(4); % Clock that the sample clock counts
            obj.info.maxPlayDuration = values(5)/1e6; % Seconds
            obj.info.waveforms = obj.WaveformNames;
            obj.info.triggerModes = obj.TriggerModeNames;
        end

        function sendTriggerLinks(obj, links1, links2)
            % One command programs the links of both trigger channels
            obj.writeCommand(obj.OpSetTriggerLinks, uint8([links1 links2]));
            obj.confirmWrite('setting the trigger channel links');
        end

        function checkLevels(obj, amplitudes_uV, restingVoltages_uV, setting)
            % Checks that each channel's waveform stays within -10 V to 10 V
            for i = 1:4
                if 2*abs(restingVoltages_uV(i)) + amplitudes_uV(i) > 2*obj.MaxVoltage_uV
                    if strcmp(setting, 'amplitude')
                        other = 'restingVoltage';
                    else
                        other = 'amplitude';
                    end
                    error(['On channel ' num2str(i) ', a resting voltage of ' num2str(restingVoltages_uV(i)/1e6)...
                           ' V and an amplitude of ' num2str(amplitudes_uV(i)/1e6) ' V peak to peak would reach '...
                           num2str((abs(restingVoltages_uV(i)) + amplitudes_uV(i)/2)/1e6) ' V. The waveform must '...
                           'stay within -10 V to 10 V. Change ' other ' first, or choose a smaller ' setting '.'])
                end
            end
        end

        function volts = checkVolts(obj, volts, name, low, high)
            volts = obj.expandToChannels(volts, name);
            if ~isnumeric(volts) || ~isreal(volts) || any(~isfinite(volts)) || any(volts < low) || any(volts > high)
                error([name ' values must be numbers of volts from ' num2str(low) ' to ' num2str(high) '.'])
            end
            volts = double(volts);
        end

        function codes = namesToCodes(~, names, validNames, nChannels, settingName, channelType)
            % Converts a name, or one name per channel, to codes: indices into validNames, from 0
            if ischar(names) || (isstring(names) && isscalar(names))
                names = repmat(cellstr(names), 1, nChannels);
            elseif isstring(names)
                names = cellstr(names);
            end
            if ~iscell(names) || numel(names) ~= nChannels
                error([settingName ' needs one name for all channels, or a 1x' num2str(nChannels) ' cell array with '...
                       'one name per ' channelType '.'])
            end
            codes = zeros(1, nChannels);
            for i = 1:nChannels
                match = [];
                if ischar(names{i}) || (isstring(names{i}) && isscalar(names{i}))
                    match = find(strcmpi(names{i}, validNames));
                end
                if isempty(match)
                    error(['Unknown ' settingName ' for ' channelType ' ' num2str(i) '. Valid names are: '...
                           strjoin(validNames, ', ') '.'])
                end
                codes(i) = match - 1;
            end
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

        function bits = channelBits(~, channels)
            % Converts a list of output channel numbers to one bit per channel (bit 0 = channel 1)
            if ~isnumeric(channels) || isempty(channels) || ~all(ismember(channels(:), 1:4))
                error('Output channels are numbered 1-4, e.g. 1 or [2 4].')
            end
            bits = uint8(sum(bitshift(1, unique(channels(:))' - 1)));
        end

        function rounded = roundHalfEven(~, value)
            % Round to the nearest integer, and a value exactly halfway between two to the even one, as the Python
            % class does, so that both send the same microvolts and microseconds. MATLAB's round() takes halves
            % away from zero.
            rounded = round(value);
            halfway = abs(value - fix(value)) == 0.5;
            rounded(halfway) = 2*round(value(halfway)/2);
        end

        function writeCommand(obj, opCode, data)
            % Sends one command, with its framing byte, in a single write
            write(obj.Port, [uint8([obj.OpMenuByte double(opCode)]) uint8(data)], 'uint8');
        end

        function data = readBytes(obj, nBytes, context)
            % Reads exactly nBytes from the device, as a row of doubles
            data = read(obj.Port, nBytes, 'uint8');
            if numel(data) < nBytes
                error(['Synth Pal did not reply in time to ' context '. ' num2str(numel(data)) ' of '...
                       num2str(nBytes) ' byte(s) arrived.'])
            end
        end

        function confirmWrite(obj, context)
            % Reads the device's one byte confirmation: 1 if it executed the command, 0 if it rejected it
            reply = read(obj.Port, 1, 'uint8');
            if isempty(reply)
                error(['Synth Pal did not confirm ' context '.'])
            end
            if reply ~= 1
                error(['Synth Pal rejected ' context '. A value was out of range.'])
            end
        end
    end
end

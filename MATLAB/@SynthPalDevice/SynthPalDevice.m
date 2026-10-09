% SynthPalDevice controls a Pulse Pal 3 running Synth Pal firmware (/Firmware/SynthPal), which makes it a four
% channel waveform synthesizer. Each output channel plays a sine, triangle, square or sawtooth wave, or steps to a
% fixed voltage, when it is triggered: by a TTL pulse on a trigger channel, from MATLAB with trigger(), or from the
% thumb joystick. Each channel has its own waveform, peak to peak voltage, mean voltage, resting voltage, play
% duration, and on and off ramps, and one frequency, 1 Hz to 20 kHz in steps of 0.01 Hz, applies to all four.
%
% Example:
%   S = SynthPalDevice('COM3');        % Replace COM3 with the device's port. serialportlist lists them.
%   S.frequency = 440;                 % Hz, all channels
%   S.waveform{1} = 'Triangle';
%   S.peakToPeak(1) = 4;               % Volts peak to peak
%   S.meanVoltage(1) = 1;              % The waveform swings 2 V above and below 1 V
%   S.restingVoltage(1) = -1;          % The output between playbacks
%   S.playDuration(1) = 0.5;           % Seconds. 0 plays until stopped.
%   S.onRampDuration(1) = 0.01;        % Seconds to fade in from the resting voltage. 0 for no ramp.
%   S.offRampDuration(1) = 0.02;       % Seconds to fade back to it
%   S.trigger(1);                      % One channel, or several as an array, e.g. S.trigger([1 3])
%   S.triggerMode{2} = 'Gated';        % Trigger channel 2
%   clear S                            % Releases the port and stops playback. TTL triggers still play the channels.
%
% Settings are properties, and assigning one programs the device at once. Settings of the output channels are
% 1x4 arrays with one element per channel, so S.peakToPeak(2) is output channel 2's peak to peak voltage; triggerMode
% is a 1x2 cell array, with one element per trigger channel. Assigning a whole setting takes one value per channel:
% S.waveform = 'Square' raises an error, because a single value does not say which channels it is meant for.
% S.waveform(:) = {'Square'} sets all four. configure() sets several of the channels' settings at once:
%   S.configure(2, 'waveform', 'Triangle', 'peakToPeak', 20, 'meanVoltage', 0);
% Voltages are in volts, times in seconds, and the frequency in Hz.
%
% Levels. A channel's periodic waveform swings peakToPeak/2 above and below its meanVoltage. It must stay within
% -10 V to 10 V: abs(meanVoltage) + peakToPeak/2 <= 10. To raise peakToPeak beyond what the mean voltage allows, change
% the mean voltage first, or set both with configure(). A sine wave of 4 V peak to peak around 0 V swings from -2 V to
% 2 V: it is 2*sin(2*pi*f*t). A 'Fixed Voltage' channel steps to its fixedVoltage instead, -10 V to 10 V, and the mean
% voltage does not apply to it. Between playbacks, a channel outputs its restingVoltage, which may be anywhere within
% -10 V to 10 V. The device picks each channel's output range for the finest voltage steps: see status().
%
% Ramps. After each trigger, a channel's on ramp (onRampDuration) fades its waveform in from the resting voltage: its
% peak to peak voltage rises linearly from 0 to its full value, and its mean from the resting voltage to the mean
% voltage (a fixed voltage ramps from the resting voltage to its voltage). The play duration follows at full amplitude,
% then the off ramp (offRampDuration) fades back to the resting voltage. The off ramp also follows a stop: stop(), a
% toggle or gated trigger, or the joystick. So the ramps lengthen playback: from a trigger to rest takes
% onRampDuration + playDuration + offRampDuration. During its off ramp a channel counts as stopping: a trigger fades it
% in again from where it is, without restarting its waveform's cycle, and plays its play duration again. A channel
% stopped during its on ramp fades out from where it is, at the off ramp's rate. The output never jumps.
%
% Waveforms start at the trigger: 'Sine' and 'Triangle' at the mean voltage, rising; 'Square' high for the first half
% of each cycle; 'Sawtooth' rising from its lowest voltage to its highest, and falling back at the cycle's end.
% 'Fixed Voltage' steps to fixedVoltage for the play duration, then returns to the resting voltage. A channel keeps
% both its peak to peak voltage and its fixed voltage, and plays the one its waveform uses.
%   S.waveform{2} = 'Fixed Voltage';
%   S.fixedVoltage(2) = -2.5;          % Steps to -2.5 V when triggered
%
% Triggers. triggerMode sets how each trigger channel acts on the output channels linked to it (linkTriggerChannel1,
% linkTriggerChannel2). These are Pulse Pal's trigger modes:
%   'Normal'  A rising edge starts the linked channels. Channels that are playing ignore it.
%   'Toggle'  A rising edge starts the linked channels, or stops those that are playing.
%   'Gated'   A rising edge starts the linked channels, and a falling edge stops them, unless the other trigger
%             channel is also gated, linked to them, and still high. With a playDuration of 0, a channel plays for
%             exactly as long as the TTL is high.
%   'Param Sync' A rising edge starts and stops nothing (the channel's links are ignored). It loads the settings most
%             recently sent by syncToDevice(), if any: this is how the next trial's settings are sent during the
%             current trial and applied the instant it starts. At the edge, the frequency and both trigger modes
%             change at once: they are shared by all channels. An output channel at its resting voltage takes its new
%             settings at once. One that is playing (off ramp included) finishes on the settings it started with, at
%             the new frequency, and takes the new ones the moment it reaches its resting voltage, so the next
%             trigger plays them. A trigger mode sent by syncToDevice() applies from the next edge. To start channels
%             on the same edge, wire the TTL to the other trigger channel too: the settings load first.
%
% Param sync and autoSync. With autoSync on (the default), assigning a setting programs the device at once, also
% while a trigger channel is in param sync mode; so leaving param sync mode means assigning triggerMode with autoSync
% on. With autoSync off, assignments change only this object's copy of the settings, checking each value, and
% syncToDevice() sends all of them in one command. While a trigger channel is in param sync mode the device stores
% that set, replacing any stored before, for the channel's next rising edge; otherwise it applies it at once. When no
% trigger channel is left in the mode, the device discards a stored set. setDefaultParams(), which the constructor
% calls, programs the device at once and takes both trigger channels out of param sync mode.
%   S.triggerMode{2} = 'Param Sync';  % Sent at once
%   S.autoSync = false;
%   S.frequency = 880;
%   S.peakToPeak(1) = 2;
%   S.syncToDevice();                  % Stored for trigger channel 2's next rising edge
%   S.autoSync = true;
% trigger() starts idle channels, and channels that are playing ignore it. When no channel is playing, a trigger
% starts the waveform 8 microseconds later. A channel triggered while another plays starts on the next sample of the
% shared sample clock.
%
% Sampling. Each cycle is samplesPerCycle samples: the largest multiple of 4 whose sampling rate (samplingRate, which
% is samplesPerCycle * frequency) is at most 100 kHz, so a sample falls on every edge, peak and trough. The frequency
% played is exact.
%
% Synth Pal's USB protocol is documented in /Firmware/SynthPal/PROTOCOL.md. The Python class,
% pulsepal.SynthPalDevice (/Python/PulsePal/pulsepal/synth_pal.py), has the same features.

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
        frequency = 100 % Frequency of all output channels, in Hz: 1 to 20000, rounded to 0.01 Hz. It can change during
                        % playback: playing channels carry on from the same point in their cycle, and keep the time
                        % they have left to play.
        waveform = {'Sine', 'Sine', 'Sine', 'Sine'} % 1x4 cell array: 'Sine', 'Triangle', 'Square', 'Sawtooth' or
                                                    % 'Fixed Voltage'. Not case sensitive. A change applies to playback
                                                    % in progress.
        peakToPeak = [5 5 5 5] % 1x4, in volts, 0 to 20: the periodic waveform's peak to peak voltage. See "Levels" above.
        fixedVoltage = [5 5 5 5] % 1x4, in volts, -10 to 10: what a 'Fixed Voltage' channel steps to. See "Levels" above.
        meanVoltage = [0 0 0 0] % 1x4, in volts: -10 to 10. A periodic waveform's mean. See "Levels" above.
        restingVoltage = [0 0 0 0] % 1x4, in volts: -10 to 10. Output while the channel is idle. See "Levels" above.
        playDuration = [1 1 1 1] % 1x4, in seconds: how long the channel plays at full amplitude after its on ramp, up
                                 % to info.maxPlayDuration. 0 plays until stopped. Counted in samples.
        onRampDuration = [0 0 0 0] % 1x4, in seconds: how long the channel fades in after a trigger, up to
                                   % info.maxPlayDuration. 0 for no ramp. See "Ramps" above.
        offRampDuration = [0 0 0 0] % 1x4, in seconds: how long the channel fades out when it stops. 0 for no ramp.
        linkTriggerChannel1 = true(1,4) % 1x4. true if trigger channel 1 triggers the output channel
        linkTriggerChannel2 = false(1,4) % 1x4. true if trigger channel 2 triggers the output channel
        triggerMode = {'Normal', 'Normal'} % 1x2 cell array, one per trigger channel: 'Normal', 'Toggle', 'Gated' or
                                           % 'Param Sync'. See "Triggers" above. Not case sensitive.
        autoSync = true % true: assigning a setting programs the device at once. false: assignments change only this
                        % object's copy, and syncToDevice() sends them all. See "Param sync and autoSync" above.
    end

    properties (SetAccess = private)
        port % The serial port connected to the device: a pulsepal.DotNetSerialPort on Windows, otherwise a serialport
        info % Properties of the connected device
        samplesPerCycle = 1000 % Samples in one cycle of the waveform at the current frequency. See "Sampling" above.
    end

    properties (Dependent, SetAccess = private)
        samplingRate % The rate at which the device plays samples, in Hz: samplesPerCycle * frequency. 100 kHz or
                     % just below it, and at least 50 kHz.
    end

    properties (Access = private)
        initialized = false % Assigning a setting programs the device only once the constructor has connected
        configuring = false % True while configure() or setDefaultParams() stores levels it has already sent
        % What the device holds for each output channel's waveform, amplitude (a periodic waveform's peak to peak
        % voltage, or a fixed voltage) and mean voltage, in microvolts, as far as this object has programmed them.
        % sendLevels() changes them in an order the device accepts.
        deviceWaveform = {'Sine', 'Sine', 'Sine', 'Sine'}
        deviceAmplitude_uV = [5 5 5 5]*1e6
        deviceMean_uV = [0 0 0 0]
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
        OpSetAmplitude = 'A' % A periodic waveform's peak to peak voltage, or a fixed voltage
        OpSetRestingVoltage = 'V'
        OpSetMeanVoltage = 'M'
        OpSetPlayDuration = 'D'
        OpSetOnRampDuration = 'B'
        OpSetOffRampDuration = 'E'
        OpSetTriggerLinks = 'I'
        OpSetTriggerMode = 'T'
        OpSetAllSettings = 'U'
        OpGetAllSettings = 'R' % Every setting, in op 'U''s layout
        OpTrigger = 'P'
        OpStop = 'X'
        OpGetStatus = 'G'
        OpGetPlaybackChecksums = 'Z'
        SynthPalHandshakeReply = 83 % 'S'
        PulsePalHandshakeReply = 75 % 'K': the device runs Pulse Pal firmware
        WavePalHandshakeReply = 87 % 'W': the device runs Wave Pal firmware
        WaveformNames = {'Sine', 'Triangle', 'Square', 'Sawtooth', 'Fixed Voltage'} % In order of their code on the
                                                                                    % device
        TriggerModeNames = {'Normal', 'Toggle', 'Gated', 'Param Sync'} % In order of their code on the device
        OutputRangeNames = {'0V:5V', '0V:10V', '-5V:5V', '-10V:10V'} % In order of their index on the device
        MaxVoltage_uV = 10000000 % Every output voltage stays within +/-10 V
    end

    methods
        function obj = SynthPalDevice(portString)
            % S = SynthPalDevice(portName) opens the serial port, checks that the device runs Synth Pal firmware,
            % reads its properties into info, shows "MATLAB Connected" on the device's screen, stops any playback and
            % programs the default settings (see setDefaultParams).

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
            % On Windows, MATLAB's serialport delivers each reply about 16 ms after it arrives, so every command that
            % waits for a confirm byte would take 16 ms. .NET's SerialPort takes about 0.3 ms (see
            % pulsepal.DotNetSerialPort). It is not available if MATLAB has been set to use .NET (Core) with dotnetenv,
            % and serialport is used then.
            if pulsepal.DotNetSerialPort.isAvailable()
                obj.port = pulsepal.DotNetSerialPort(portString, defaultBaudRate);
            else
                obj.port = serialport(portString, defaultBaudRate);
            end
            try
                setDTR(obj.port, true);
                flush(obj.port); % Discard anything left in the buffers by an earlier session
                obj.handshake(portString);
                obj.readHardwareInfo();
                obj.writeCommand(obj.OpSetClientName, 'MATLAB'); % Shown on the device's screen as "MATLAB Connected"
                obj.initialized = true;
                obj.stop();
                obj.setDefaultParams();
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
                obj.port = []; % Release the port, so that the next attempt can open it
                rethrow(err)
            end
        end

        function setDefaultParams(obj)
            % Programs the default settings on the device: a frequency of 100 Hz, and on every output channel a
            % sine wave of 5 V peak to peak around a mean voltage of 0 V, resting at 0 V, played for 1 second with no
            % ramps, and a fixed voltage of 5 V for when the waveform is 'Fixed Voltage'. Both trigger channels in
            % 'Normal' mode, and all output channels linked to trigger channel 1 and not to trigger channel 2. They
            % match the settings the device starts with. They are sent at once, also while autoSync is off.
            autoSyncState = obj.autoSync;
            obj.autoSync = true;
            cleanup = onCleanup(@() obj.restoreAutoSync(autoSyncState));
            obj.frequency = 100;
            % In this order, each is valid whatever the device holds: a mean of 0 V goes with any amplitude, 5 V is
            % then a valid amplitude for any waveform, and a sine wave is then valid
            obj.sendLevelsInOrder({'M', [0 0 0 0]; 'A', [5 5 5 5]*1e6; 'W', repmat({'Sine'}, 1, 4)}, true);
            obj.storeLevels(repmat({'Sine'}, 1, 4), [5 5 5 5], [5 5 5 5], [0 0 0 0]);
            obj.restingVoltage = zeros(1,4);
            obj.playDuration = ones(1,4);
            obj.onRampDuration = zeros(1,4);
            obj.offRampDuration = zeros(1,4);
            obj.triggerMode = {'Normal', 'Normal'};
            obj.linkTriggerChannel1 = true(1,4);
            obj.linkTriggerChannel2 = false(1,4);
            clear cleanup
        end

        function syncToDevice(obj)
            % Sends every setting to the device in one command: frequency, and for each channel waveform, peakToPeak
            % or fixedVoltage, meanVoltage, restingVoltage, playDuration, onRampDuration, offRampDuration,
            % linkTriggerChannel1 and linkTriggerChannel2, and triggerMode. While a trigger channel is in param sync
            % mode, the device stores the set, replacing any set stored before, and that channel's next rising edge
            % loads it. Otherwise the device applies it at once. The properties read as sent, including while a set
            % waits for its edge. See "Param sync and autoSync" above.
            obj.checkLevels(obj.peakToPeak, obj.meanVoltage, 'sync');
            [waveforms, amplitudes, means] = obj.deviceLevels(obj.waveform, obj.peakToPeak, obj.fixedVoltage, ...
                                                               obj.meanVoltage);
            waveformCodes = obj.namesToCodes(waveforms, obj.WaveformNames, 4, 'waveform', 'output channel');
            modeCodes = obj.namesToCodes(obj.triggerMode, obj.TriggerModeNames, 2, 'triggerMode', 'trigger channel');
            data = [typecast(uint32(obj.roundHalfEven(obj.frequency*100)), 'uint8'), uint8(waveformCodes), ...
                    typecast(int32(amplitudes), 'uint8'), typecast(int32(means), 'uint8'), ...
                    typecast(int32(obj.roundHalfEven(obj.restingVoltage*1e6)), 'uint8'), ...
                    typecast(uint32(obj.roundHalfEven(obj.playDuration*1e6)), 'uint8'), ...
                    typecast(uint32(obj.roundHalfEven(obj.onRampDuration*1e6)), 'uint8'), ...
                    typecast(uint32(obj.roundHalfEven(obj.offRampDuration*1e6)), 'uint8'), ...
                    uint8([obj.linkTriggerChannel1 obj.linkTriggerChannel2]), uint8(modeCodes)];
            obj.writeCommand(obj.OpSetAllSettings, data);
            obj.confirmWrite('syncToDevice()');
            % Applied now, or, in param sync mode, at the next edge. Either way these are the levels the next
            % assignments build on; if a stored set has not loaded yet, sendLevels() finds out and recovers.
            obj.deviceWaveform = waveforms;
            obj.deviceAmplitude_uV = amplitudes;
            obj.deviceMean_uV = means;
        end

        function configure(obj, channels, varargin)
            % Sets several of the output channels' settings at once, as name-value pairs, e.g.
            %   S.configure(1, 'waveform', 'Sine', 'peakToPeak', 20, 'meanVoltage', 0);
            %   S.configure([2 3], 'waveform', 'Fixed Voltage', 'fixedVoltage', -2.5, 'playDuration', 0.1);
            % channels: one output channel number, or several as an array. Each value is one value for every channel
            % given, or one per channel given, in the same order (a cell array for waveform names). The names are
            % waveform, peakToPeak, fixedVoltage, meanVoltage, restingVoltage, playDuration, onRampDuration and
            % offRampDuration; settings left out keep their values. The new settings are checked together, so their
            % order does not matter: a channel whose sine wave swings 2 V around a mean voltage of 9 V can go straight
            % to 20 V peak to peak around 0 V. With autoSync on, the device is programmed in an order it accepts at
            % every step. Raises an error, and changes nothing, if a value is out of range or the settings do not go
            % together.
            channelList = obj.channelNumbers(channels);
            if numel(unique(channelList)) ~= numel(channelList)
                error('configure(): each output channel may be given once.')
            end
            names = {'waveform', 'peakToPeak', 'fixedVoltage', 'meanVoltage', 'restingVoltage', 'playDuration', ...
                     'onRampDuration', 'offRampDuration'};
            if mod(numel(varargin), 2) ~= 0
                error('configure() takes name-value pairs after the channels, e.g. S.configure(1, ''peakToPeak'', 4).')
            end
            newValues = struct;
            for i = 1:2:numel(varargin)
                name = varargin{i};
                if ~(ischar(name) || (isstring(name) && isscalar(name))) || ~any(strcmp(name, names))
                    error(['Unknown setting for configure(). Valid names are: ' strjoin(names, ', ') '.'])
                end
                name = char(name);
                value = varargin{i+1};
                if strcmp(name, 'waveform')
                    if ischar(value) || (isstring(value) && isscalar(value))
                        value = cellstr(value);
                    end
                    if ~iscell(value)
                        error('configure(): waveform takes a name, or a cell array with one name per channel.')
                    end
                end
                if isscalar(value)
                    value = repmat(value, 1, numel(channelList));
                elseif numel(value) ~= numel(channelList)
                    error(['configure(): ' name ' has ' num2str(numel(value)) ' values for ' ...
                           num2str(numel(channelList)) ' channels. Give one value, or one value per channel.'])
                end
                settings = obj.(name);
                settings(channelList) = reshape(value, 1, []);
                newValues.(name) = obj.checkSetting(name, settings);
            end
            levels = struct('waveform', {obj.waveform}, 'peakToPeak', obj.peakToPeak, 'fixedVoltage', ...
                            obj.fixedVoltage, 'meanVoltage', obj.meanVoltage);
            levelNames = fieldnames(levels);
            for i = 1:numel(levelNames)
                if isfield(newValues, levelNames{i})
                    levels.(levelNames{i}) = newValues.(levelNames{i});
                end
            end
            obj.checkLevels(levels.peakToPeak, levels.meanVoltage, 'configure');
            if obj.initialized && obj.autoSync
                [waveforms, amplitudes, means] = obj.deviceLevels(levels.waveform, levels.peakToPeak, ...
                                                                   levels.fixedVoltage, levels.meanVoltage);
                obj.sendLevels(waveforms, amplitudes, means);
            end
            obj.storeLevels(levels.waveform, levels.peakToPeak, levels.fixedVoltage, levels.meanVoltage);
            otherNames = {'restingVoltage', 'playDuration', 'onRampDuration', 'offRampDuration'};
            for i = 1:numel(otherNames)
                if isfield(newValues, otherNames{i})
                    obj.(otherNames{i}) = newValues.(otherNames{i}); % Sent at once with autoSync on
                end
            end
        end

        function trigger(obj, channels)
            % Triggers output channels in software: one channel number, e.g. S.trigger(1), or several as an array,
            % e.g. S.trigger([2 4]). Idle channels start from the beginning of their waveform's cycle, together.
            % Channels that are playing ignore it.
            obj.writeCommand(obj.OpTrigger, obj.channelBits(channels));
        end

        function stop(obj, channels)
            % Stops playback, e.g. S.stop() for all channels, or S.stop([1 3]). The stopped channels return to their
            % resting voltage, over their off ramps.
            if nargin < 2
                bits = uint8(15);
            else
                bits = obj.channelBits(channels);
            end
            obj.writeCommand(obj.OpStop, bits);
        end

        function deviceStatus = status(obj)
            % Returns the device's playback state, as a struct with fields:
            % playing: numbers of the output channels that are playing, e.g. [1 3]. A channel in its off ramp counts
            %          as playing until it reaches its resting voltage.
            % samplesPerCycle: samples in one cycle of the waveform
            % outputRanges: 1x4 cell array, the output range the device chose for each channel: the one with the
            %               finest steps that holds its waveform and its resting voltage: '0V:5V' (76 uV steps), then
            %               '0V:10V' or '-5V:5V' (153 uV), then '-10V:10V' (305 uV)
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

        function setScreenSaver(obj, enabled, timeout)
            % Switches the device's screen saver on (enabled = true) or off (enabled = false), and sets its timeout in
            % seconds (a whole number, 1-65535; 1800 if left out). With the screen saver on, the device dims its
            % screen once it has been left alone for the timeout: no command from the computer, no rising edge on a
            % trigger channel, and no joystick click or push. The next of these brings the screen back. Both settings
            % are kept in the device's EEPROM, shared with Pulse Pal and Wave Pal firmware, and saved once no channel is
            % playing. The screen saver can also be switched on and off from the device's joystick menu. A new device
            % has it on, with 1800 s. Example: S.setScreenSaver(true, 300) dims the screen after 5 minutes without
            % activity.
            if nargin < 3
                timeout = 1800;
            end
            if ~(isnumeric(enabled) || islogical(enabled)) || ~isscalar(enabled) || ~(enabled == 0 || enabled == 1)
                error('enabled must be true (on) or false (off)') % NaN too: it fails both comparisons
            end
            if ~isnumeric(timeout) || ~isscalar(timeout) || ~(timeout >= 1 && timeout <= 65535) || ...
                    timeout ~= round(timeout)
                error('timeout must be a whole number of seconds from 1 to 65535')
            end
            obj.writeCommand(obj.OpSetScreenSaver, [uint8(enabled) typecast(uint16(timeout), 'uint8')]);
            obj.confirmWrite('setScreenSaver()');
        end

        function syncFromDevice(obj)
            % Reads every setting from the device into the properties: use it after settings were changed with the
            % device's joystick, for example. The device holds one amplitude per channel: a 'Fixed Voltage' channel's
            % goes to fixedVoltage, and any other channel's to peakToPeak. This object keeps the other one, reduced if
            % need be to suit the mean voltage read back. In param sync mode, it reads the settings the device plays
            % now, not a set waiting for the next edge.
            obj.writeCommand(obj.OpGetAllSettings, []);
            reply = uint8(obj.readBytes(114, 'syncFromDevice()'));
            centiHz = double(typecast(reply(1:4), 'uint32'));
            waveforms = obj.WaveformNames(double(reply(5:8)) + 1);
            amplitudes_uV = double(typecast(reply(9:24), 'int32'));
            means_uV = double(typecast(reply(25:40), 'int32'));
            rests_uV = double(typecast(reply(41:56), 'int32'));
            durations_us = double(typecast(reply(57:104), 'uint32')); % Play, on ramp, off ramp: 4 of each
            links = reply(105:112) > 0;
            modes = obj.TriggerModeNames(double(reply(113:114)) + 1);
            isFixed = strcmp(waveforms, 'Fixed Voltage');
            newPeakToPeak = obj.peakToPeak;
            newFixedVoltage = obj.fixedVoltage;
            newPeakToPeak(~isFixed) = amplitudes_uV(~isFixed)/1e6;
            newFixedVoltage(isFixed) = amplitudes_uV(isFixed)/1e6;
            % This object's own peak to peak voltage must still suit the mean voltage (see checkLevels())
            newPeakToPeak(isFixed) = min(newPeakToPeak(isFixed), (2*obj.MaxVoltage_uV - 2*abs(means_uV(isFixed)))/1e6);
            autoSyncState = obj.autoSync;
            obj.autoSync = false; % Stored, not sent
            cleanup = onCleanup(@() obj.restoreAutoSync(autoSyncState));
            obj.frequency = centiHz/100;
            obj.storeLevels(waveforms, newPeakToPeak, newFixedVoltage, means_uV/1e6);
            obj.restingVoltage = rests_uV/1e6;
            obj.playDuration = durations_us(1:4)/1e6;
            obj.onRampDuration = durations_us(5:8)/1e6;
            obj.offRampDuration = durations_us(9:12)/1e6;
            obj.triggerMode = modes;
            obj.linkTriggerChannel1 = links(1:4);
            obj.linkTriggerChannel2 = links(5:8);
            clear cleanup
            obj.deviceWaveform = waveforms;
            obj.deviceAmplitude_uV = amplitudes_uV;
            obj.deviceMean_uV = means_uV;
        end

        function params = exportParams(obj)
            % Returns every setting as a struct, e.g. to save with your data and record exactly what the device
            % played: frequency, then the channel settings, each with one value per channel. Example:
            %   params = S.exportParams();
            %   save('Trial12.mat', 'params');           % Or jsonencode(params), for a text file
            % The Python class's export_params() returns the same, with snake_case names.
            params = struct;
            names = {'frequency', 'waveform', 'peakToPeak', 'fixedVoltage', 'meanVoltage', 'restingVoltage', ...
                     'playDuration', 'onRampDuration', 'offRampDuration', 'triggerMode', 'linkTriggerChannel1', ...
                     'linkTriggerChannel2'};
            for i = 1:numel(names)
                params.(names{i}) = obj.(names{i});
            end
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
            if obj.initialized && obj.autoSync %#ok<MCSUP> The device must be connected
                obj.writeCommand(obj.OpSetFrequency, typecast(uint32(centiHz), 'uint8'));
                reply = obj.readBytes(5, 'setting frequency');
                if reply(1) ~= 1
                    error('Synth Pal rejected the frequency.')
                end
                obj.samplesPerCycle = double(typecast(uint8(reply(2:5)), 'uint32')); %#ok<MCSUP>
            else
                obj.samplesPerCycle = 4*floor(2500000/centiHz); %#ok<MCSUP> As the device works it out
            end
            obj.frequency = centiHz/100;
        end

        function set.waveform(obj, names)
            names = obj.checkSetting('waveform', names);
            obj.applyLevels('waveform', names);
            obj.waveform = names;
        end

        function set.peakToPeak(obj, volts)
            volts = obj.checkSetting('peakToPeak', volts);
            obj.applyLevels('peakToPeak', volts);
            obj.peakToPeak = volts;
        end

        function set.fixedVoltage(obj, volts)
            volts = obj.checkSetting('fixedVoltage', volts);
            obj.applyLevels('fixedVoltage', volts);
            obj.fixedVoltage = volts;
        end

        function set.meanVoltage(obj, volts)
            volts = obj.checkSetting('meanVoltage', volts);
            obj.applyLevels('meanVoltage', volts);
            obj.meanVoltage = volts;
        end

        function set.restingVoltage(obj, volts)
            volts = obj.checkSetting('restingVoltage', volts); % Any resting voltage goes with any waveform
            if obj.initialized && obj.autoSync %#ok<MCSUP>
                obj.writeCommand(obj.OpSetRestingVoltage, typecast(int32(obj.roundHalfEven(volts*1e6)), 'uint8'));
                obj.confirmWrite('setting restingVoltage');
            end
            obj.restingVoltage = volts;
        end

        function set.playDuration(obj, seconds)
            seconds = obj.checkSetting('playDuration', seconds);
            if obj.initialized && obj.autoSync %#ok<MCSUP>
                obj.writeCommand(obj.OpSetPlayDuration, typecast(uint32(obj.roundHalfEven(seconds*1e6)), 'uint8'));
                obj.confirmWrite('setting playDuration');
            end
            obj.playDuration = seconds;
        end

        function set.onRampDuration(obj, seconds)
            seconds = obj.checkSetting('onRampDuration', seconds);
            if obj.initialized && obj.autoSync %#ok<MCSUP>
                obj.writeCommand(obj.OpSetOnRampDuration, typecast(uint32(obj.roundHalfEven(seconds*1e6)), 'uint8'));
                obj.confirmWrite('setting onRampDuration');
            end
            obj.onRampDuration = seconds;
        end

        function set.offRampDuration(obj, seconds)
            seconds = obj.checkSetting('offRampDuration', seconds);
            if obj.initialized && obj.autoSync %#ok<MCSUP>
                obj.writeCommand(obj.OpSetOffRampDuration, typecast(uint32(obj.roundHalfEven(seconds*1e6)), 'uint8'));
                obj.confirmWrite('setting offRampDuration');
            end
            obj.offRampDuration = seconds;
        end

        function set.triggerMode(obj, modes)
            codes = obj.namesToCodes(modes, obj.TriggerModeNames, 2, 'triggerMode', 'trigger channel');
            if obj.initialized && obj.autoSync %#ok<MCSUP>
                obj.writeCommand(obj.OpSetTriggerMode, uint8(codes));
                obj.confirmWrite('setting triggerMode');
            end
            obj.triggerMode = obj.TriggerModeNames(codes+1);
        end

        function set.linkTriggerChannel1(obj, links)
            links = obj.checkLogical(links, 'linkTriggerChannel1');
            if obj.initialized && obj.autoSync %#ok<MCSUP>
                obj.sendTriggerLinks(links, obj.linkTriggerChannel2); %#ok<MCSUP>
            end
            obj.linkTriggerChannel1 = links;
        end

        function set.linkTriggerChannel2(obj, links)
            links = obj.checkLogical(links, 'linkTriggerChannel2');
            if obj.initialized && obj.autoSync %#ok<MCSUP>
                obj.sendTriggerLinks(obj.linkTriggerChannel1, links); %#ok<MCSUP>
            end
            obj.linkTriggerChannel2 = links;
        end

        function set.autoSync(obj, value)
            if ~(islogical(value) || isnumeric(value)) || ~isscalar(value) || ~(value == 0 || value == 1)
                error('autoSync must be true or false.')
            end
            obj.autoSync = logical(value);
        end

        function delete(obj)
            % Runs on clear S or delete(S). Tells the device that MATLAB is disconnecting, which stops playback (each
            % channel over its off ramp), as on Pulse Pal, and puts the device's own name back on its screen in place
            % of "MATLAB Connected", and releases the serial port. The device keeps its settings, so TTL triggers
            % still play the channels.
            if obj.initialized
                try
                    obj.writeCommand(obj.OpDisconnect, []);
                catch
                    % The port may already be gone, e.g. the cable was unplugged
                end
            end
            obj.port = [];
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
            obj.port.Timeout = 2; % A Synth Pal replies at once, so do not wait long for another kind of device
            obj.writeCommand(obj.OpHandshake, []);
            reply = obj.readHandshakeReply(); % Skips the reply to a command an earlier session left queued
            obj.port.Timeout = 10;
            if numel(reply) < 5
                error(['No reply from the device on ' char(portString) '. Is it a Pulse Pal 3 running Synth Pal firmware?'])
            end
            version = double(typecast(uint8(reply(2:5)), 'uint32'));
            reply = reply(1);
            if reply == obj.PulsePalHandshakeReply || reply == obj.WavePalHandshakeReply
                if reply == obj.PulsePalHandshakeReply
                    names = {'Pulse Pal', 'PulsePalDevice'};
                else
                    names = {'Wave Pal', 'WavePalDevice'};
                end
                error(['The device on ' char(portString) ' runs ' names{1} ' firmware (v' num2str(version) ').'...
                       newline 'Load Synth Pal firmware onto it (/Firmware/SynthPal), or connect with ' names{2} '.'])
            end
            if reply ~= obj.SynthPalHandshakeReply
                error(['The device on ' char(portString) ' returned an unexpected handshake signature.'])
            end
            firmwareVersion = version;
            if firmwareVersion > obj.CurrentFirmwareVersion
                % New firmware only adds commands (see PROTOCOL.md), so this class still works with it
                warning('SynthPalDevice:newerFirmware', ['Synth Pal firmware v' num2str(firmwareVersion) ...
                        ' is newer than this class knows (v' num2str(obj.CurrentFirmwareVersion) '). It works '...
                        'with it, but update the MATLAB software to use what is new.'])
            end
            obj.info = struct;
            obj.info.firmwareVersion = firmwareVersion;
        end

        function reply = readHandshakeReply(obj)
            % The reply to op 72, the handshake, once it has been sent: the firmware's letter, then its version (5
            % bytes), or fewer bytes if the device did not send 5 within the port's Timeout. The reply is the last 5
            % bytes the device sends before it goes quiet. A command that an earlier session sent just before it closed
            % can still be waiting on the device when this session connects, e.g. while the device redraws its screen.
            % The device answers it first: after the constructor discarded the bytes waiting, and before the handshake.
            % Its reply is skipped. Bytes are read until none has arrived for 50 ms, longer than the device takes to
            % redraw its screen between two replies, or than serialport takes to deliver bytes on Windows (15.6 ms),
            % and for at most 1 s, so that a device that never stops sending is refused rather than waited for. The
            % three classes and the Python and C++ clients read the handshake this way.
            reply = obj.port.read(5, 'uint8'); % Waits for a whole reply, up to the port's Timeout
            if numel(reply) < 5
                return
            end
            started = tic;
            lastArrival = tic;
            while toc(lastArrival) < 0.05 && toc(started) < 1
                nWaiting = obj.port.NumBytesAvailable;
                if nWaiting > 0
                    reply = [reply obj.port.read(nWaiting, 'uint8')]; %#ok<AGROW>
                    lastArrival = tic;
                else
                    pause(0.001);
                end
            end
            reply = reply(end-4:end);
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

        function restoreAutoSync(obj, state)
            % For setDefaultParams() and syncFromDevice(): puts autoSync back, also if they failed
            obj.autoSync = state;
        end

        function value = checkSetting(obj, name, value)
            % Checks new values of an output channel setting, for all four channels, and returns them as the property
            % holds them
            switch name
                case 'waveform'
                    codes = obj.namesToCodes(value, obj.WaveformNames, 4, 'waveform', 'output channel');
                    value = obj.WaveformNames(codes+1);
                case 'peakToPeak'
                    value = obj.checkVolts(value, name, 0, 20, ' peak to peak');
                case {'fixedVoltage', 'meanVoltage', 'restingVoltage'}
                    value = obj.checkVolts(value, name, -10, 10);
                case 'playDuration'
                    value = obj.checkDurations(value, name, '0 (play until stopped)');
                otherwise
                    value = obj.checkDurations(value, name, '0 (no ramp)');
            end
        end

        function applyLevels(obj, name, values)
            % For the set methods of waveform, peakToPeak, fixedVoltage and meanVoltage: checks that the channels'
            % levels go together with the new values, and with autoSync on, programs them. configure() and
            % setDefaultParams() have done both already.
            if obj.configuring
                return
            end
            levels = struct('waveform', {obj.waveform}, 'peakToPeak', obj.peakToPeak, 'fixedVoltage', ...
                            obj.fixedVoltage, 'meanVoltage', obj.meanVoltage);
            levels.(name) = values;
            obj.checkLevels(levels.peakToPeak, levels.meanVoltage, name);
            if obj.initialized && obj.autoSync
                [waveforms, amplitudes, means] = obj.deviceLevels(levels.waveform, levels.peakToPeak, ...
                                                                   levels.fixedVoltage, levels.meanVoltage);
                obj.sendLevels(waveforms, amplitudes, means);
            end
        end

        function storeLevels(obj, waveforms, peakToPeak, fixedVoltage, meanVoltage)
            % Stores levels that have been checked and sent, without sending them again
            obj.configuring = true;
            cleanup = onCleanup(@() obj.endConfiguring());
            obj.waveform = waveforms;
            obj.peakToPeak = peakToPeak;
            obj.fixedVoltage = fixedVoltage;
            obj.meanVoltage = meanVoltage;
            clear cleanup
        end

        function endConfiguring(obj)
            obj.configuring = false;
        end

        function checkLevels(obj, peakToPeak, meanVoltage, setting)
            % Checks that each channel's periodic waveform stays within -10 V to 10 V, as the device does. Checked on
            % every channel, also one playing a fixed voltage, so that a change of waveform always suits the levels
            % it finds. setting is the one being changed, for the advice in the message.
            pp_uV = obj.roundHalfEven(peakToPeak*1e6);
            mean_uV = obj.roundHalfEven(meanVoltage*1e6);
            for i = 1:4
                if 2*abs(mean_uV(i)) + pp_uV(i) > 2*obj.MaxVoltage_uV
                    switch setting
                        case 'peakToPeak'
                            advice = ' Change meanVoltage first, set both with configure(), or choose a smaller peakToPeak.';
                        case 'meanVoltage'
                            advice = ' Change peakToPeak first, set both with configure(), or choose a smaller meanVoltage.';
                        otherwise
                            advice = ' Change peakToPeak or meanVoltage.';
                    end
                    error(['On channel ' num2str(i) ', a mean voltage of ' num2str(meanVoltage(i)) ' V and a peak '...
                           'to peak voltage of ' num2str(peakToPeak(i)) ' V would reach '...
                           num2str((abs(mean_uV(i)) + pp_uV(i)/2)/1e6) ' V. The waveform must stay within -10 V to '...
                           '10 V.' advice])
                end
            end
        end

        function [waveforms, amplitudes_uV, means_uV] = deviceLevels(obj, waveforms, peakToPeak, fixedVoltage, ...
                                                                       meanVoltage)
            % What the device holds for these settings: waveforms, amplitudes in microvolts (a fixed voltage, or a
            % periodic waveform's peak to peak voltage) and mean voltages in microvolts
            isFixed = strcmp(waveforms, 'Fixed Voltage');
            amplitudes = peakToPeak;
            amplitudes(isFixed) = fixedVoltage(isFixed);
            amplitudes_uV = obj.roundHalfEven(amplitudes*1e6);
            means_uV = obj.roundHalfEven(meanVoltage*1e6);
        end

        function valid = isValidLevel(obj, waveform, amplitude_uV, mean_uV)
            % Whether the device accepts a channel's waveform, amplitude and mean voltage together, as
            % isValidOutputLevel() in /Firmware/SynthPal/Settings.ino checks them
            if strcmp(waveform, 'Fixed Voltage')
                valid = abs(amplitude_uV) <= obj.MaxVoltage_uV;
            else
                valid = amplitude_uV >= 0 && 2*abs(mean_uV) + amplitude_uV <= 2*obj.MaxVoltage_uV;
            end
        end

        function sendLevels(obj, waveforms, amplitudes_uV, means_uV)
            % Programs the device's waveforms, amplitudes and mean voltages, in an order it accepts at every step.
            % The device checks each op against what it holds for the other two (see isValidLevel()), and one order
            % does not suit every change: a sine wave of 20 V peak to peak cannot become a fixed voltage of -5 V by
            % either op first. So each channel first takes an amplitude that suits both its current and its new
            % waveform and mean voltage: its current amplitude if it can, else its new one, else 0 V, which suits any.
            % Then the waveforms, the mean voltages and the new amplitudes follow. If the device refuses a step, which
            % means this object's record of what it holds is out of date (e.g. a param sync edge loaded a stored
            % set), the change is made from 0 V, which works from any state.
            if isequal(waveforms, obj.deviceWaveform) && isequal(amplitudes_uV, obj.deviceAmplitude_uV) && ...
                    isequal(means_uV, obj.deviceMean_uV)
                return
            end
            firstAmplitudes = zeros(1,4);
            for i = 1:4
                w0 = obj.deviceWaveform{i};
                m0 = obj.deviceMean_uV(i);
                for candidate = [obj.deviceAmplitude_uV(i), amplitudes_uV(i), 0]
                    if obj.isValidLevel(w0, candidate, m0) && obj.isValidLevel(waveforms{i}, candidate, m0) && ...
                            obj.isValidLevel(waveforms{i}, candidate, means_uV(i))
                        firstAmplitudes(i) = candidate;
                        break
                    end
                end
            end
            try
                obj.sendLevelsInOrder({'A', firstAmplitudes; 'W', waveforms; 'M', means_uV; 'A', amplitudes_uV}, false);
            catch err
                if ~strcmp(err.identifier, 'SynthPalDevice:rejected')
                    rethrow(err)
                end
                obj.sendLevelsInOrder({'A', [0 0 0 0]; 'W', waveforms; 'M', means_uV; 'A', amplitudes_uV}, true);
            end
        end

        function sendLevelsInOrder(obj, steps, force)
            % Sends ops 'A' (amplitudes, uV), 'W' (waveform names) and 'M' (mean voltages, uV), one per row of steps,
            % and keeps the record of what the device holds. Unless force, an op that would not change what the record
            % says the device holds is skipped.
            for i = 1:size(steps, 1)
                values = steps{i, 2};
                switch steps{i, 1}
                    case 'W'
                        if isequal(values, obj.deviceWaveform) && ~force
                            continue
                        end
                        codes = obj.namesToCodes(values, obj.WaveformNames, 4, 'waveform', 'output channel');
                        obj.writeCommand(obj.OpSetWaveform, uint8(codes));
                        obj.confirmWrite('setting waveform');
                        obj.deviceWaveform = values;
                    case 'A'
                        if isequal(values, obj.deviceAmplitude_uV) && ~force
                            continue
                        end
                        obj.writeCommand(obj.OpSetAmplitude, typecast(int32(values), 'uint8'));
                        obj.confirmWrite('setting the amplitude');
                        obj.deviceAmplitude_uV = values;
                    case 'M'
                        if isequal(values, obj.deviceMean_uV) && ~force
                            continue
                        end
                        obj.writeCommand(obj.OpSetMeanVoltage, typecast(int32(values), 'uint8'));
                        obj.confirmWrite('setting meanVoltage');
                        obj.deviceMean_uV = values;
                end
            end
        end

        function seconds = checkDurations(obj, seconds, name, zeroMeaning)
            % Checks a duration for each output channel: 0 to info.maxPlayDuration seconds
            seconds = obj.oneValuePerChannel(seconds, 4, name, 'output channel');
            maxDuration = 3600;
            if isstruct(obj.info)
                maxDuration = obj.info.maxPlayDuration;
            end
            if ~isnumeric(seconds) || ~isreal(seconds) || any(~isfinite(seconds)) || any(seconds < 0) || ...
                    any(seconds > maxDuration)
                error([name ' must be ' zeroMeaning ' or a positive number of seconds up to ' num2str(maxDuration) '.'])
            end
            seconds = double(seconds);
        end

        function volts = checkVolts(obj, volts, name, low, high, note)
            if nargin < 6
                note = '';
            end
            volts = obj.oneValuePerChannel(volts, 4, name, 'output channel');
            if ~isnumeric(volts) || ~isreal(volts) || any(~isfinite(volts)) || any(volts < low) || any(volts > high)
                error([name ' values must be numbers of volts from ' num2str(low) ' to ' num2str(high) note '.'])
            end
            volts = double(volts);
        end

        function codes = namesToCodes(obj, names, validNames, nChannels, settingName, channelType)
            % Converts one name per channel to codes: indices into validNames, from 0
            if ischar(names) || (isstring(names) && isscalar(names))
                obj.oneValuePerChannel({char(names)}, nChannels, settingName, channelType); % Raises the error
            elseif isstring(names)
                names = cellstr(names);
            end
            if ~iscell(names) || numel(names) ~= nChannels
                error([settingName ' needs a 1x' num2str(nChannels) ' cell array with one name per ' channelType '.'])
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

        function values = oneValuePerChannel(~, values, nChannels, name, channelType)
            % Returns values as a row, after checking that there is one per channel. A single value is refused rather
            % than copied to every channel: a script that sets one channel should say which, and one that sets them
            % all should list them, so that it reads the same in every class and language.
            if numel(values) == nChannels
                values = reshape(values, 1, nChannels);
                return
            end
            if isscalar(values) && iscell(values)
                example = ['''' char(values{1}) ''''];
                error([name ' holds one name per ' channelType ', so a single name is ambiguous. Set one ' channelType ...
                       ' by its number, e.g. S.' name '{1} = ' example ', or all ' num2str(nChannels) ', e.g. S.' ...
                       name '(:) = {' example '}.'])
            elseif isscalar(values)
                example = num2str(values);
                error([name ' holds one value per ' channelType ', so a single value is ambiguous. Set one ' channelType ...
                       ' by its number, e.g. S.' name '(1) = ' example ', or all ' num2str(nChannels) ', e.g. S.' ...
                       name '(:) = ' example '.'])
            end
            error([name ' needs one value per ' channelType ' (1x' num2str(nChannels) ').'])
        end

        function values = checkLogical(obj, values, name)
            values = obj.oneValuePerChannel(values, 4, name, 'output channel');
            if ~(islogical(values) || isnumeric(values)) || any(values ~= 0 & values ~= 1)
                error([name ' values must be true or false (1 or 0).'])
            end
            values = logical(values);
        end

        function channelList = channelNumbers(~, channels)
            % Checks output channel numbers: one, or several as an array
            if ~isnumeric(channels) || ~isreal(channels) || isempty(channels) || ~all(ismember(channels(:), 1:4))
                error('Output channels are numbered 1-4: give one, or several as an array, e.g. 1 or [2 4].')
            end
            channelList = double(channels(:))';
        end

        function bits = channelBits(obj, channels)
            % Converts output channel numbers to one bit per channel (bit 0 = channel 1)
            bits = uint8(sum(bitshift(1, unique(obj.channelNumbers(channels)) - 1)));
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
            write(obj.port, [uint8([obj.OpMenuByte double(opCode)]) uint8(data)], 'uint8');
        end

        function data = readBytes(obj, nBytes, context)
            % Reads exactly nBytes from the device, as a row of doubles
            data = read(obj.port, nBytes, 'uint8');
            if numel(data) < nBytes
                error(['Synth Pal did not reply in time to ' context '. ' num2str(numel(data)) ' of '...
                       num2str(nBytes) ' byte(s) arrived.'])
            end
        end

        function confirmWrite(obj, context)
            % Reads the device's one byte confirmation: 1 if it executed the command, 0 if it rejected it (and changed
            % nothing). A rejection has its own identifier, which sendLevels() recovers from.
            reply = read(obj.port, 1, 'uint8');
            if isempty(reply)
                error(['Synth Pal did not confirm ' context '.'])
            end
            if reply ~= 1
                error('SynthPalDevice:rejected', 'Synth Pal rejected %s. A value was out of range.', context)
            end
        end
    end
end

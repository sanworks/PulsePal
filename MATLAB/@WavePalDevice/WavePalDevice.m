% WavePalDevice controls a Pulse Pal 3 running Wave Pal firmware (/Firmware/WavePal), which makes it a four
% channel waveform player. The device stores four waveforms of up to 1 million samples each on its microSD card,
% and each output channel plays one of them when it is triggered: by a TTL pulse on a trigger channel, from MATLAB
% with trigger(), or from the thumb joystick. Waveforms play at up to 100 kHz.
%
% Example:
%   W = WavePalDevice('COM3');                  % Replace COM3 with the device's port. serialportlist lists them.
%   W.samplingRate = 50000;                     % Hz, all channels
%   W.outputRange = '-5V:5V';                   % All channels
%   t = (0:49999)/50000;                        % 1 second
%   W.loadWaveform(1, 4*sin(2*pi*10*t), [1 3]); % Waveform 1, a 10 Hz sine wave, +/-4 V, played by channels 1 and 3
%   W.trigger([1 3]);                           % One channel, or several as an array
%   W.loadWaveform(2, linspace(0, 4, 5000));    % Waveform 2, a ramp. No channel plays it yet...
%   W.currentWaveform(3) = 2;                   % ...until now: channel 3 plays it from its next start
%   W.loopMode(1) = true;                       % Loop channel 1's waveform...
%   W.loopDuration(1) = 3;                      % ...for 3 seconds after each trigger
%   W.triggerMode{2} = 'Toggle';                % Trigger channel 2
%   clear W                                     % Releases the port and stops playback. TTL triggers still play.
%
% Settings are properties, and assigning one programs the device at once. Settings of the output channels are
% 1x4 arrays with one element per channel, so W.loopMode(2) is output channel 2's loop mode; triggerMode is a 1x2
% cell array, with one element per trigger channel, as in PulsePalDevice. Assigning a whole setting takes one value
% per channel: W.loopMode = true raises an error, because a single value does not say which channels it is meant
% for. W.loopMode(:) = true sets all four. Voltages are in volts, within outputRange. Times are in seconds.
%
% Waveforms. The device stores four waveforms, numbered 1-4, and each output channel plays one of them: its
% currentWaveform, or none (0, the default). Several channels can play the same waveform, which is loaded only once.
% A channel that is playing finishes its waveform: a new currentWaveform plays from the channel's next start. Outside
% param sync mode, loading a waveform stops the channels playing it.
%
% Triggers. triggerMode sets how each trigger channel acts on the output channels linked to it
% (linkTriggerChannel1, linkTriggerChannel2). The first four are Pulse Pal's trigger modes:
%   'Normal'      A rising edge starts the linked channels' waveforms. Channels that are playing ignore it.
%   'Toggle'      A rising edge starts the linked channels, or stops those that are playing.
%   'Gated'       A rising edge starts the linked channels, and a falling edge stops them, unless the other trigger
%                 channel is also gated, linked to them, and still high. With loop mode on and a loop duration of 0,
%                 a waveform plays for exactly as long as the TTL is high.
%   'Param Sync'  A rising edge starts and stops nothing (the channel's links are ignored). It makes the waveforms
%                 loaded, and the currentWaveform set, since the previous edge current. This is how the next trial's
%                 waveforms are loaded during the current trial, and played from the instant it starts.
%   'Master'      A rising edge starts the linked channels, and restarts those that are playing from their first
%                 sample.
% trigger() starts idle channels, and channels that are playing ignore it, as in Pulse Pal. When no channel is
% playing, a trigger starts the waveform within microseconds. A channel triggered while another plays starts on the
% next sample of the shared sample clock.
%
% Param sync. While a trigger channel is in 'Param Sync' mode, loadWaveform() loads into a second memory block that
% the device keeps for each waveform: channels go on playing the waveform it replaces, and triggers still start it,
% until the next rising edge on that trigger channel makes the new one current. A channel playing at the edge
% finishes what it plays. A later load of the same waveform before the edge replaces the earlier one, and stops any
% channel still playing that waveform from before the previous edge, since the device reuses that memory block.
% Changes to currentWaveform wait for the edge in the same way; other settings apply at once. When neither trigger
% channel is in param sync mode any more, what was waiting becomes current at once, so waveforms and
% currentWaveform describe the device again (Pulse Pal and Synth Pal discard their stored settings then).
% outputRange cannot change in param sync mode. To make the waveforms current on the same edge that starts the
% channels, wire the TTL to the other trigger channel as well: the param sync edge is handled first.
%   W.triggerMode{2} = 'Param Sync';
%   W.loadWaveform(1, nextTrialWave);   % Waits for trigger channel 2's next rising edge
%
% Wave Pal's USB protocol is documented in /Firmware/WavePal/PROTOCOL.md. The Python class,
% pulsepal.WavePalDevice (/Python/PulsePal/pulsepal/wave_pal.py), has the same features.

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
        samplingRate = 10000 % Sampling rate of all output channels, in Hz: a whole number from 1 to info.maxSamplingRate.
                             % It can change during playback. The rate played can differ slightly: see actualSamplingRate.
        outputRange = '' % Voltage range of all output channels: '0V:5V', '0V:10V', '-5V:5V' or '-10V:10V'. The smallest
                         % range that fits the waveforms gives the finest voltage steps. Changing it stops playback,
                         % sets the outputs to 0 V and loads the waveforms again, encoded for the new range. It cannot
                         % change in param sync mode, where the loads would wait for a TTL edge.
        currentWaveform = zeros(1,4) % 1x4. The number of the waveform each output channel plays, 1-4, from those
                                     % loaded by loadWaveform(), or 0 for none: a channel with no waveform ignores
                                     % triggers. A channel that is playing finishes its waveform first. In param sync
                                     % mode, a change waits for the next edge.
        loopMode = false(1,4) % 1x4. true loops the channel's waveform until loopDuration has elapsed, or until stopped
                              % if loopDuration is 0. false plays it once. A change applies to playback in progress.
        loopDuration = zeros(1,4) % 1x4, in seconds. In loop mode, how long the channel plays after each trigger. It stops
                                  % part way through the waveform if need be. 0 loops until the channel is stopped.
        triggerMode = {'Normal', 'Normal'} % 1x2 cell array, one per trigger channel: 'Normal', 'Toggle', 'Gated',
                                           % 'Param Sync' or 'Master'. See "Triggers" above. Not case sensitive.
        linkTriggerChannel1 = false(1,4) % 1x4. true if a rising edge on trigger channel 1 triggers the output channel.
                                         % By default no output channel is linked: a TTL starts nothing until a script
                                         % links it
        linkTriggerChannel2 = false(1,4) % 1x4. true if a rising edge on trigger channel 2 triggers the output channel
    end

    properties (SetAccess = private)
        port % The serial port connected to the device: a pulsepal.DotNetSerialPort on Windows, otherwise a serialport
        info % Properties of the connected device
        waveforms = cell(1,4) % 1x4 cell array of the waveforms loaded by loadWaveform(), in volts, by waveform number.
                              % [] if none. In param sync mode, it holds the waveforms loaded since the last edge
                              % before the device plays them. status() shows what the device itself holds, which can
                              % include waveforms loaded by an earlier connection.
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
        OpSetScreenSaver = 99
        OpHardwareRevision = 100 % The circuit board revision, as Pulse Pal firmware's op 100
        OpSerialNumber = 102 % The USB serial number, as Pulse Pal firmware's op 102
        OpHardwareInfo = 'N'
        OpSetSamplingRate = 'S'
        OpSetOutputRange = 'R'
        OpLoadWaveform = 'L'
        OpSetCurrentWaveforms = 'C'
        OpTrigger = 'P'
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
        SynthPalHandshakeReply = 83 % 'S': the device runs Synth Pal firmware
        OutputRangeNames = {'0V:5V', '0V:10V', '-5V:5V', '-10V:10V'} % In order of their index on the device
        SettingNames = {'samplingRate', 'outputRange', 'currentWaveform', 'loopMode', 'loopDuration', 'triggerMode', ...
                        'linkTriggerChannel1', 'linkTriggerChannel2'} % The settings exportParams() returns, in order
        OutputRangeLimits = [0 5; 0 10; -5 5; -10 10] % Volts, one row per range
        TriggerModeNames = {'Normal', 'Toggle', 'Gated', 'Param Sync', 'Master'}
        TriggerModeCodes = [0 1 2 3 4] % Their codes on the device: Pulse Pal's, and 4
        DACBitMax = 65535
        % Added to the error for a reply that does not arrive. After a comm failure the device reads no command until its
        % joystick is clicked, and then loads its default settings. The same text as the Python classes'.
        NoReplyHint = ['If the device''s screen shows COMM. FAILURE!, a command reached it only in part, and it ' ...
                       'waits: click its joystick, then connect again. Otherwise, check the USB connection.']
    end

    methods
        function obj = WavePalDevice(portString, serialNumber)
            % W = WavePalDevice(portName) opens the serial port, checks that the device runs Wave Pal firmware, reads
            % its properties into info, shows "MATLAB Connected" on the device's screen, stops any playback and
            % programs the default settings (see setDefaultParams).
            % W = WavePalDevice('SerialNumber', serialNumber) connects to the device with that USB serial number
            % (info.serialNumber), wherever it is plugged in: see pulsepal.portForSerialNumber.

            % Check for minimum MATLAB version. verLessThan works in releases older than the minimum, where
            % isMATLABReleaseOlderThan (introduced in R2020b) does not exist.
            MinVer = '9.9';
            MinVerName = 'R2020b';
            if verLessThan('matlab', MinVer) %#ok<VERLESSMATLAB>
                error(['WavePalDevice requires MATLAB ' MinVerName ' or newer.'])
            end

            if nargin == 2
                if ~((ischar(portString) || isstring(portString)) && strcmpi(portString, 'SerialNumber'))
                    error(['WavePalDevice takes a port name, e.g. WavePalDevice(''COM3''), or a serial number, e.g. '...
                           'WavePalDevice(''SerialNumber'', ''20345420'').'])
                end
                portString = pulsepal.portForSerialNumber(serialNumber);
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
                % so it is sent only once the device has identified itself as a Wave Pal.
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
            % Programs the default settings on the device: both trigger channels in 'Normal' mode, a 10 kHz sampling
            % rate, the -10 V to 10 V output range, loop mode off with loop durations of 0, no output channel linked to
            % a trigger channel (a TTL starts nothing until a script links it), and no currentWaveform on any channel. They match the
            % settings the device starts with. The trigger modes are sent first: a trigger channel leaving param sync
            % mode makes what was stored for its next edge current, and the settings after it then apply at once.
            % Loaded waveforms are kept, and loaded again if the output range changes. Raises an error if a loaded
            % waveform does not fit the default range.
            obj.triggerMode = {'Normal', 'Normal'};
            obj.samplingRate = 10000;
            obj.outputRange = '-10V:10V';
            obj.loopMode = false(1,4);
            obj.loopDuration = zeros(1,4);
            obj.linkTriggerChannel1 = false(1,4);
            obj.linkTriggerChannel2 = false(1,4);
            obj.currentWaveform = zeros(1,4);
        end

        function loadWaveform(obj, waveformNumber, waveform, channels)
            % Loads one of the device's waveforms, and optionally chooses the channels that play it.
            % waveformNumber: the waveform to load, 1 to info.nWaveforms (4)
            % waveform: voltages of the samples, played at samplingRate: 1 to info.maxSamples values, all within
            %           outputRange.
            % channels (optional): output channels that play this waveform from their next start, one channel number
            %           or several as an array. It sets their currentWaveform, and leaves the other channels' alone,
            %           so channels already playing this waveform play the new one. Left out, no channel changes.
            % The samples are written to the device's microSD card, and the first info.bufferSamples of them are
            % also kept in its RAM. Outside param sync mode, loading stops the channels playing that waveform, and the
            % new one plays from their next start; other channels keep playing. In param sync mode, the new waveform
            % waits for the next rising edge on the param sync trigger channel, and channels go on playing the old one
            % until then. If the device cannot store the waveform, it is left out of waveforms: outside param sync
            % mode the device has unloaded it, and in param sync mode it plays the version made current at the last
            % edge.
            % Example: W.loadWaveform(2, 3*sin(2*pi*100*(0:9999)/W.samplingRate), [1 3]);
            waveformNumber = obj.checkWaveformNumber(waveformNumber);
            if nargin > 3
                obj.channelBits(channels); % Checks the channel numbers before anything is sent
            end
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
            obj.waveforms{waveformNumber} = []; % Until the device has stored it. Outside param sync mode, it unloads it first.
            obj.writeCommand(obj.OpLoadWaveform, [uint8(waveformNumber) typecast(uint32(nSamples), 'uint8')...
                typecast(codes, 'uint8')]);
            obj.confirmWrite('loadWaveform()');
            obj.waveforms{waveformNumber} = samples;
            if nargin > 3
                newWaveforms = obj.currentWaveform;
                newWaveforms(unique(channels(:))) = waveformNumber;
                obj.currentWaveform = newWaveforms;
            end
        end

        function trigger(obj, channels)
            % Triggers output channels in software: one channel number, e.g. W.trigger(1), or several as an array,
            % e.g. W.trigger([2 4]). Idle channels start their current waveforms from the first sample, on the same
            % sample. Channels that are playing ignore it, as Pulse Pal's do. Raises an error, and sends nothing, if a
            % channel has no waveform to play: its currentWaveform is 0, or a waveform this object has not loaded.
            bits = obj.channelBits(channels);
            for channel = unique(channels(:))'
                waveformNumber = obj.currentWaveform(channel);
                if waveformNumber == 0
                    error(['Channel ' num2str(channel) ' has no waveform to play. Set currentWaveform(' ...
                           num2str(channel) ') to a loaded waveform''s number, or load one with loadWaveform(' ...
                           'number, waveform, ' num2str(channel) ').'])
                end
                if isempty(obj.waveforms{waveformNumber})
                    error(['Channel ' num2str(channel) ' plays waveform ' num2str(waveformNumber) ', which is not ' ...
                           'loaded. Load it with loadWaveform(' num2str(waveformNumber) ', waveform).'])
                end
            end
            obj.writeCommand(obj.OpTrigger, bits);
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
            % samplesLoaded: 1x4, samples in each waveform on the device, by waveform number, 0 if it has none. A
            %                load stored for a param sync edge counts from the edge.
            % underruns: 1x4, blocks of samples that were not read from the microSD card in time, since the
            %            device started. The output holds its last value until the block arrives.
            % longestInterrupt_us: longest run of the device's playback interrupt since the previous call to
            %                      status(), in microseconds. It must stay below the sample period.
            % currentWaveform: 1x4, the waveform each output channel plays from its next start, 0 for none, as the
            %                  device holds it. In param sync mode, currentWaveform set since the last edge is in
            %                  storedCurrentWaveform until the next one.
            % storedWaveforms: numbers of the waveforms loaded in param sync mode since the last edge, which the
            %                  next edge makes current, e.g. 2
            % storedCurrentWaveform: 1x4, the currentWaveform set in param sync mode since the last edge, which the
            %                        next edge applies. [] if none was set.
            obj.writeCommand(obj.OpGetStatus, []);
            reply = obj.readBytes(47, 'status()');
            values = double(typecast(uint8(reply(2:37)), 'uint32'));
            deviceStatus = struct;
            deviceStatus.playing = find(bitget(reply(1), 1:4));
            deviceStatus.samplesLoaded = values(1:4);
            deviceStatus.underruns = values(5:8);
            deviceStatus.longestInterrupt_us = values(9)/1000;
            deviceStatus.currentWaveform = double(reply(38:41));
            deviceStatus.storedWaveforms = find(bitget(reply(42), 1:4));
            deviceStatus.storedCurrentWaveform = [];
            if reply(43)
                deviceStatus.storedCurrentWaveform = double(reply(44:47));
            end
        end

        function setScreenSaver(obj, enabled, timeout)
            % Switches the device's screen saver on (enabled = true) or off (enabled = false), and sets its timeout in
            % seconds (a whole number, 1-65535; 1800 if left out). With the screen saver on, the device dims its
            % screen once it has been left alone for the timeout: no command from the computer, no rising edge on a
            % trigger channel, and no joystick click or push. The next of these brings the screen back. Both settings
            % are kept in the device's EEPROM, shared with Pulse Pal and Synth Pal firmware, and saved once no channel
            % is playing. The screen saver can also be switched on and off from the device's joystick menu. A new
            % device has it on, with 1800 s. Example: W.setScreenSaver(true, 300) dims the screen after 5 minutes
            % without activity.
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

        function params = exportParams(obj)
            % Returns every setting as a struct, e.g. to save with your data: samplingRate and outputRange, then the
            % channel settings, each with one value per channel. The waveforms are not part of it: the waveforms
            % property holds them. Example:
            %   params = W.exportParams();
            %   save('Session3.mat', 'params');          % Or jsonencode(params), for a text file
            % The Python class's export_params() returns the same, with snake_case names.
            params = struct;
            names = obj.SettingNames;
            for i = 1:numel(names)
                params.(names{i}) = obj.(names{i});
            end
        end

        function importParams(obj, params)
            % Programs the device with settings exported by exportParams(): a struct with samplingRate, outputRange and
            % the channel settings, each with one value per channel. All of them are checked first, and nothing is sent
            % if one is invalid. They are then sent in an order that lets each take effect: a trigger channel leaving
            % param sync mode first, then the sampling rate, the output range and the channel settings, and a trigger
            % channel entering param sync mode last. So currentWaveform applies at once, unless a trigger channel was in
            % param sync mode and stays in it: then it waits for the next edge, as an assignment would. Settings missing
            % from params keep their values. Fields that are not settings, e.g. from a newer version's exportParams(),
            % are skipped with a warning. The waveforms are not settings: load them with loadWaveform() first, since a
            % current waveform must be a loaded one. Example:
            %   W.importParams(jsondecode(fileread('Session3.json')));
            % The Python class's import_params() does the same.
            if ~isstruct(params) || ~isscalar(params)
                error('importParams() takes a struct of settings, as exportParams() returns.')
            end
            unknown = setdiff(fieldnames(params), obj.SettingNames, 'stable');
            if ~isempty(unknown)
                warning('WavePalDevice:unknownParams', ['importParams(): skipped ' strjoin(unknown, ', ') ...
                        ', which this version of WavePalDevice does not have.'])
            end
            has = @(name) isfield(params, name);
            % Every setting is checked before any is sent
            rate = obj.samplingRate;
            if has('samplingRate')
                rate = obj.checkSamplingRate(params.samplingRate);
            end
            modes = obj.triggerMode;
            if has('triggerMode')
                modes = obj.TriggerModeNames(obj.matchTriggerModes(params.triggerMode));
            end
            % The trigger modes the other settings are sent in: those of channels leaving param sync mode are sent
            % first, those of channels entering it last
            firstModes = obj.triggerMode;
            leaving = strcmp(firstModes, 'Param Sync') & ~strcmp(modes, 'Param Sync');
            firstModes(leaving) = modes(leaving);
            rangeName = obj.outputRange;
            if has('outputRange')
                rangeName = obj.checkOutputRange(params.outputRange, firstModes);
            end
            durations = obj.loopDuration;
            if has('loopDuration')
                durations = obj.checkLoopDurations(params.loopDuration);
            end
            obj.durations2Samples(durations, rate); % They must fit at the new rate
            if has('loopMode')
                obj.checkLogical(params.loopMode, 'loopMode');
            end
            if has('currentWaveform')
                obj.checkCurrentWaveform(params.currentWaveform);
            end
            for name = {'linkTriggerChannel1', 'linkTriggerChannel2'}
                if has(name{1})
                    obj.checkLogical(params.(name{1}), name{1});
                end
            end
            % Then sent, in that order
            if ~isequal(firstModes, obj.triggerMode)
                obj.triggerMode = firstModes;
            end
            if rate ~= obj.samplingRate
                obj.samplingRate = rate;
            end
            if ~strcmp(rangeName, obj.outputRange)
                obj.outputRange = rangeName;
            end
            for name = {'loopMode', 'loopDuration', 'linkTriggerChannel1', 'linkTriggerChannel2', 'currentWaveform'}
                if has(name{1})
                    obj.(name{1}) = params.(name{1});
                end
            end
            if ~isequal(modes, obj.triggerMode)
                obj.triggerMode = modes;
            end
        end

        function rate = get.actualSamplingRate(obj)
            rate = obj.info.sampleClockHz/obj.periodCycles(obj.samplingRate);
        end

        function set.samplingRate(obj, rate)
            if ~isnumeric(rate) || ~isscalar(rate) || ~isreal(rate) || rate ~= round(rate) || rate < 1
                error('samplingRate must be a whole number of Hz.')
            end
            rate = double(rate);
            if obj.initialized %#ok<MCSUP> The device must be connected
                rate = obj.checkSamplingRate(rate);
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
                obj.checkOutputRange(rangeName, obj.triggerMode); %#ok<MCSUP>
                obj.writeCommand(obj.OpSetOutputRange, uint8(rangeIndex-1));
                obj.confirmWrite('setting outputRange');
            end
            obj.outputRange = rangeName;
            if obj.initialized && rangeChanged %#ok<MCSUP>
                % The device unloads the waveforms when the range changes, because their samples encode voltages
                % in the old range. Load them again, encoded for the new one. The channels keep their current
                % waveforms.
                oldWaveforms = obj.waveforms; %#ok<MCSUP>
                obj.waveforms = cell(size(oldWaveforms)); %#ok<MCSUP>
                for i = 1:numel(oldWaveforms)
                    if ~isempty(oldWaveforms{i})
                        obj.loadWaveform(i, oldWaveforms{i});
                    end
                end
            end
        end

        function set.currentWaveform(obj, waveformNumbers)
            waveformNumbers = obj.oneValuePerChannel(waveformNumbers, 4, 'currentWaveform', 'output channel');
            if obj.initialized %#ok<MCSUP>
                waveformNumbers = obj.checkCurrentWaveform(waveformNumbers);
                obj.writeCommand(obj.OpSetCurrentWaveforms, uint8(waveformNumbers));
                obj.confirmWrite('setting currentWaveform');
            end
            obj.currentWaveform = double(waveformNumbers);
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
            durations = obj.checkLoopDurations(durations);
            if obj.initialized %#ok<MCSUP>
                loopSamples = obj.durations2Samples(durations, obj.samplingRate); %#ok<MCSUP>
                obj.writeCommand(obj.OpSetLoopDuration, typecast(uint32(loopSamples), 'uint8'));
                obj.confirmWrite('setting loopDuration');
            end
            obj.loopDuration = durations;
        end

        function set.triggerMode(obj, modes)
            matches = obj.matchTriggerModes(modes);
            if obj.initialized %#ok<MCSUP>
                obj.writeCommand(obj.OpSetTriggerMode, uint8(obj.TriggerModeCodes(matches)));
                obj.confirmWrite('setting triggerMode');
            end
            obj.triggerMode = obj.TriggerModeNames(matches);
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
            % Runs on clear W or delete(W). Tells the device that MATLAB is disconnecting, which stops playback, as on
            % Pulse Pal, and puts the device's own name back on its screen in place of "MATLAB Connected", and releases
            % the serial port. The device keeps its settings and waveforms, so TTL triggers still play them.
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
            obj.port.Timeout = 2; % A Wave Pal replies at once, so do not wait long for another kind of device
            obj.writeCommand(obj.OpHandshake, []);
            reply = obj.readHandshakeReply(); % Skips the reply to a command an earlier session left queued
            obj.port.Timeout = 10;
            if numel(reply) < 5
                error(['No reply from the device on ' char(portString) '. Is it a Pulse Pal 3 running Wave Pal firmware? ' ...
                       obj.NoReplyHint])
            end
            version = double(typecast(uint8(reply(2:5)), 'uint32'));
            reply = reply(1);
            if reply == obj.PulsePalHandshakeReply
                error(['The device on ' char(portString) ' runs Pulse Pal firmware (v' num2str(version) ').'...
                       newline 'Load Wave Pal firmware onto it (/Firmware/WavePal), or connect with PulsePalDevice.'])
            end
            if reply == obj.SynthPalHandshakeReply
                error(['The device on ' char(portString) ' runs Synth Pal firmware (v' num2str(version) ').'...
                       newline 'Load Wave Pal firmware onto it (/Firmware/WavePal), or connect with SynthPalDevice.'])
            end
            if reply ~= obj.WavePalHandshakeReply
                error(['The device on ' char(portString) ' returned an unexpected handshake signature.'])
            end
            firmwareVersion = version;
            if firmwareVersion > obj.CurrentFirmwareVersion
                % New firmware only adds commands (see PROTOCOL.md), so this class still works with it
                warning('WavePalDevice:newerFirmware', ['Wave Pal firmware v' num2str(firmwareVersion) ...
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
            reply = obj.readBytes(19, 'reading the hardware info');
            obj.writeCommand(obj.OpHardwareRevision, []);
            circuitBoardRevision = obj.readBytes(1, 'reading the circuit board revision');
            obj.writeCommand(obj.OpSerialNumber, []);
            serialNumberLength = obj.readBytes(1, 'reading the USB serial number'); % Then that many characters
            serialNumber = '';
            if serialNumberLength > 0
                serialNumber = char(obj.readBytes(serialNumberLength, 'reading the USB serial number'));
            end
            values = double(typecast(uint8(reply(3:18)), 'uint32'));
            obj.info.hardwareVersion = reply(1);
            % 0-31: the board encodes its revision on five pins, which the firmware reads at startup. 0 if it encodes
            % none. Copy it to your data, with hardwareVersion, to record exactly which hardware ran an experiment.
            obj.info.circuitBoardRevision = circuitBoardRevision;
            % The USB serial number, e.g. '20345420': the Teensy's, unique to each device. Copy it to your data too. The
            % constructor takes it in place of a port name: see pulsepal.portForSerialNumber
            obj.info.serialNumber = serialNumber;
            obj.info.nChannels = reply(2);
            obj.info.nWaveforms = reply(19); % Waveforms the device stores, numbered from 1
            obj.waveforms = cell(1, obj.info.nWaveforms);
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

        function rate = checkSamplingRate(obj, rate)
            % Checks a sampling rate, and returns it as a double
            if ~isnumeric(rate) || ~isscalar(rate) || ~isreal(rate) || rate ~= round(rate) || ...
                    rate < 1 || rate > obj.info.maxSamplingRate
                error(['samplingRate must be a whole number of Hz from 1 to ' num2str(obj.info.maxSamplingRate) '.'])
            end
            rate = double(rate);
        end

        function rangeName = checkOutputRange(obj, rangeName, triggerModes)
            % Checks that the output range can be set, with these trigger modes, and returns its canonical name
            [rangeName, rangeIndex] = obj.findRange(rangeName);
            limits = obj.OutputRangeLimits(rangeIndex, :);
            for i = 1:numel(obj.waveforms)
                wave = obj.waveforms{i};
                if ~isempty(wave) && (min(wave) < limits(1) || max(wave) > limits(2))
                    error(['Waveform ' num2str(i) ' spans ' num2str(min(wave)) ' V to '...
                           num2str(max(wave)) ' V, which does not fit the ' rangeName ' range. Load a new '...
                           'waveform first, or choose a wider range.'])
                end
            end
            if ~strcmp(rangeName, obj.outputRange) && any(strcmp(triggerModes, 'Param Sync'))
                error(['outputRange cannot change while a trigger channel is in param sync mode: the waveforms '...
                       'are loaded again for the new range, and in param sync mode they would wait for a TTL '...
                       'edge. Set triggerMode to another mode first.'])
            end
        end

        function waveformNumbers = checkCurrentWaveform(obj, waveformNumbers)
            % Checks new current waveforms, one per output channel, and returns them as a row
            waveformNumbers = obj.oneValuePerChannel(waveformNumbers, 4, 'currentWaveform', 'output channel');
            nWaveforms = obj.info.nWaveforms;
            if ~isnumeric(waveformNumbers) || ~isreal(waveformNumbers) || ...
                    any(~ismember(waveformNumbers, 0:nWaveforms))
                error(['currentWaveform values are waveform numbers, 1-' num2str(nWaveforms) ', or 0 for none.'])
            end
            % A channel left on a waveform whose load failed is not refused here: trigger() refuses it
            changed = find(waveformNumbers ~= obj.currentWaveform & waveformNumbers > 0);
            for channel = changed
                if isempty(obj.waveforms{waveformNumbers(channel)})
                    error(['Waveform ' num2str(waveformNumbers(channel)) ' is not loaded. Load it with '...
                           'loadWaveform(' num2str(waveformNumbers(channel)) ', waveform) first.'])
                end
            end
        end

        function durations = checkLoopDurations(obj, durations)
            % Checks new loop durations, one per output channel, and returns them as a row of doubles
            durations = obj.oneValuePerChannel(durations, 4, 'loopDuration', 'output channel');
            if ~isnumeric(durations) || ~isreal(durations) || any(~isfinite(durations)) || any(durations < 0)
                error('loopDuration must be 0 (loop until stopped) or a positive number of seconds.')
            end
            durations = double(durations);
        end

        function matches = matchTriggerModes(obj, modes)
            % Returns the indices in TriggerModeNames of new trigger modes, one per trigger channel
            if ischar(modes) || (isstring(modes) && isscalar(modes))
                obj.oneValuePerChannel({char(modes)}, 2, 'triggerMode', 'trigger channel'); % Raises the error
            elseif isstring(modes)
                modes = cellstr(modes);
            end
            if ~iscell(modes) || numel(modes) ~= 2
                error(['triggerMode needs a 1x2 cell array with one mode per trigger channel, e.g. '...
                       '{''Normal'', ''Toggle''}.'])
            end
            matches = zeros(1,2);
            for i = 1:2
                match = [];
                if ischar(modes{i}) || (isstring(modes{i}) && isscalar(modes{i}))
                    match = find(strcmpi(modes{i}, obj.TriggerModeNames));
                end
                if isempty(match)
                    error(['Unknown trigger mode for trigger channel ' num2str(i) '. Valid modes are: '...
                           strjoin(obj.TriggerModeNames, ', ') '.'])
                end
                matches(i) = match;
            end
        end

        function samples = durations2Samples(obj, durations, rate)
            % Converts loop durations in seconds to samples at a sampling rate
            actualRate = obj.info.sampleClockHz/obj.periodCycles(rate);
            samples = obj.roundHalfEven(durations*actualRate);
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
            codes = uint16(obj.roundHalfEven((volts - limits(1))/(limits(2) - limits(1))*obj.DACBitMax));
        end

        function cycles = periodCycles(obj, rate)
            % The sample period the device plays at a sampling rate, in cycles of its sample clock: the nearest whole
            % number, computed as the firmware's setSamplingRate() does, in integers. A rate halfway between two
            % (e.g. 3072 Hz: 7812.5 cycles) rounds up there, not to even, so roundHalfEven() does not apply.
            clockHz = obj.info.sampleClockHz;
            cycles = floor((clockHz + floor(rate/2))/rate);
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
                       ' by its number, e.g. W.' name '{1} = ' example ', or all ' num2str(nChannels) ', e.g. W.' ...
                       name '(:) = {' example '}.'])
            elseif isscalar(values)
                example = num2str(values);
                error([name ' holds one value per ' channelType ', so a single value is ambiguous. Set one ' channelType ...
                       ' by its number, e.g. W.' name '(1) = ' example ', or all ' num2str(nChannels) ', e.g. W.' ...
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

        function waveformNumber = checkWaveformNumber(obj, waveformNumber)
            if ~isnumeric(waveformNumber) || ~isscalar(waveformNumber) || ...
                    ~ismember(waveformNumber, 1:obj.info.nWaveforms)
                error(['Waveforms are numbered 1-' num2str(obj.info.nWaveforms) '.'])
            end
            waveformNumber = double(waveformNumber);
        end

        function bits = channelBits(~, channels)
            % Converts output channel numbers, one or several as an array, to one bit per channel (bit 0 = channel 1)
            if ~isnumeric(channels) || ~isreal(channels) || isempty(channels) || ~all(ismember(channels(:), 1:4))
                error('Output channels are numbered 1-4: give one, or several as an array, e.g. 1 or [2 4].')
            end
            bits = uint8(sum(bitshift(1, unique(channels(:))' - 1)));
        end

        function rounded = roundHalfEven(~, value)
            % Round to the nearest integer, and a value exactly halfway between two to the even one, as the Python
            % class does, so that both send the same DAC codes and loop durations. MATLAB's round() takes halves
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
                error(['Wave Pal did not reply in time to ' context '. ' num2str(numel(data)) ' of '...
                       num2str(nBytes) ' byte(s) arrived. ' obj.NoReplyHint])
            end
        end

        function confirmWrite(obj, context)
            % Reads the device's one byte confirmation: 1 if it executed the command, 0 if it rejected it
            reply = read(obj.port, 1, 'uint8');
            if isempty(reply)
                error(['Wave Pal did not confirm ' context ': no reply arrived within ' num2str(obj.port.Timeout) ...
                       ' s. ' obj.NoReplyHint])
            end
            if reply ~= 1
                error(['Wave Pal rejected ' context '. A value was out of range, or a waveform could not be '...
                       'written to the microSD card.'])
            end
        end
    end
end

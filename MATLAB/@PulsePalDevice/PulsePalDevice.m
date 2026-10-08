% PulsePalDevice controls a Pulse Pal on a USB serial port. Pulse Pal plays precisely timed voltage pulse trains on
% four output channels, started from MATLAB with trigger(), by TTL pulses on its two trigger channels, or from its
% thumb joystick. This class supports Pulse Pal 2 and Pulse Pal 3, with firmware v21 or newer.
%
% Example:
%   P = PulsePalDevice('COM3');          % Replace COM3 with Pulse Pal's port. serialportlist lists them.
%   P.phase1Voltage(1) = 5;              % Volts, on output channel 1
%   P.phase1Duration(1) = 0.001;         % Seconds: 1 ms pulses,
%   P.interPulseInterval(1) = 0.049;     % 49 ms apart (end of one to start of the next), so 20 pulses per second,
%   P.pulseTrainDuration(1) = 2;         % for 2 seconds
%   P.trigger(1);                        % Plays the pulse train on output channel 1
%   P.triggerMode(2) = 1;                % Trigger channel 2 in toggle mode (see "Trigger modes" below)
%   clear P                              % Releases the port. Pulse Pal stops all output channels.
%
% Parameters are properties, with one element per channel: the output channel parameters below are 1x4, so
% P.phase1Voltage(2) belongs to output channel 2, and triggerMode is 1x2, one element per trigger channel. With
% autoSync on (the default), assigning a parameter programs the device at once. With autoSync off, assignments change
% only this object's copy of the parameters, and syncToDevice() sends all of them in one command:
%   P.autoSync = false;
%   P.phase1Voltage = [5 5 2.5 2.5];
%   P.phase1Duration = [0.001 0.001 0.002 0.002];
%   P.syncToDevice();
%   P.autoSync = true;
%
% Units. Voltages are in volts, -10 to 10. Times are in seconds, rounded to the nearest cycle of the device's timer
% (50 us). Phase durations, the inter-pulse interval and the pulse train duration are at least 100 us
% (info.minPulseWidth_us): the shortest pulse that another Pulse Pal's trigger channel detects reliably. A value out
% of range raises an error, and nothing is sent.
%
% Output channel parameters (1x4):
%   isBiphasic           0: monophasic pulses, phase 1 only. 1: biphasic pulses: phase 1, interPhaseInterval, phase 2
%   phase1Voltage        Voltage of the first phase of each pulse
%   phase2Voltage        Voltage of the second phase (biphasic pulses only)
%   restingVoltage       Voltage between pulses, and while the channel is idle
%   phase1Duration       Duration of the first phase
%   interPhaseInterval   Time at the resting voltage between the two phases (biphasic pulses only)
%   phase2Duration       Duration of the second phase (biphasic pulses only)
%   interPulseInterval   Time from the end of one pulse to the start of the next
%   burstDuration        Duration of each burst of pulses. 0 for no bursts: pulses continue for the whole train
%   interBurstInterval   Time at the resting voltage between bursts, when burstDuration is not 0
%   pulseTrainDuration   Duration of the pulse train
%   pulseTrainDelay      Time from the trigger to the start of the pulse train
%   linkTriggerChannel1  1 if a TTL on trigger channel 1 triggers the output channel, 0 if not
%   linkTriggerChannel2  1 if a TTL on trigger channel 2 triggers the output channel, 0 if not
%   customTrainID        0 plays the pulse train defined above. 1 or more plays that custom train instead, loaded
%                        with sendCustomPulseTrain() or sendCustomWaveform(): 1-4 on Pulse Pal 3, 1-2 on Pulse Pal 2
%   customTrainTarget    0: a custom train's times are pulse onsets. 1: they are burst onsets
%   customTrainLoop      1 repeats a custom train until pulseTrainDuration has elapsed. 0 plays it once
%   playbackMode         0 plays the pulse train once per trigger. 1 (continuous loop mode) plays it until the channel
%                        is stopped, ignoring pulseTrainDuration
% A pulse starts only if it fits. In a burst, its first phase, or a biphasic pulse's whole pulse, must end before the
% burst does. A biphasic pulse must also end by the end of the train; the end of the train cuts a monophasic pulse
% short. The parameter guide, https://sites.google.com/site/pulsepalwiki/parameter-guide, has diagrams.
%
% Trigger modes (triggerMode, 1x2, one per trigger channel):
%   0  Normal: a rising edge starts the pulse trains of the linked output channels. Edges during a train are ignored.
%   1  Toggle: as 0, but a rising edge during a train stops it.
%   2  Pulse gated: the trains play only while the TTL is high.
%   3  Param sync (Pulse Pal 3 only): a rising edge starts and stops nothing. It loads the parameters most recently
%      sent by syncToDevice(): this is how the next trial's parameters are sent during the current trial and applied
%      the instant it starts. An idle output channel takes them in the timer cycle the edge is detected. One that is
%      playing a pulse train finishes it on the parameters it started with, and takes the new ones the moment it
%      ends, so the next trigger plays a whole train with the new parameters. A channel in continuous loop mode has
%      no train end, so it keeps its parameters until something stops it.
%      Only syncToDevice() waits for the edge: with autoSync on, assigning a parameter programs the device at once,
%      also in param sync mode. So leaving param sync mode means assigning triggerMode with autoSync on; a trigger
%      mode sent by syncToDevice() takes effect at the next edge. A param sync channel's links to output channels are
%      ignored: to start trains on the same edge, wire the TTL to the other trigger channel too (the parameters load
%      first). Connecting, and setDefaultParams(), take both trigger channels out of param sync mode.
%   P.triggerMode(2) = 3;                % Sent at once
%   P.autoSync = false;
%   P.phase1Voltage = [2 2 2 2];
%   P.syncToDevice();                    % Stored: trigger channel 2's next rising edge applies it
%   P.autoSync = true;
%
% Methods (help PulsePalDevice.trigger, and so on, describes each one):
%   trigger(channels)                    Starts the pulse trains of output channels, e.g. P.trigger([1 3])
%   stop(channels)                       Stops pulse trains: P.stop() stops all of them, P.stop([1 3]) some
%   setVoltage(channel, voltage)         Holds an output channel at a fixed voltage until it is triggered
%   sendCustomPulseTrain(trainID, pulseTimes, voltages)    Loads a custom pulse train
%   sendCustomWaveform(trainID, samplingPeriod, voltages)  Loads a sampled waveform, as a custom pulse train
%   syncToDevice()                       With autoSync off, sends every parameter to the device in one command
%   syncFromDevice()                     Reads every parameter from the device into the properties
%   setDefaultParams()                   Programs the default parameters
%   saveParameters(fileName)             Saves the parameters to a .mat file; loadParameters(fileName) loads them
%   sdSettings(fileName, op)             Saves, loads or deletes a settings file on the device's microSD card
%   gui()                                Opens the parameter editor window
%   setScreenSaver(state, timeout), setCalibration(channel, voltageOffset), formatMicroSD()
% info holds the connected device's hardware and firmware versions, and its limits.
%
% The USB protocol is documented in /Firmware/PROTOCOL.md. The Python class, /Python/PulsePal/PulsePal.py, has the
% same parameters with snake_case names, but there an assignment changes only the local copy until sync_to_device().

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

classdef PulsePalDevice < handle
    % The class help is at the top of this file, above the license: MATLAB's help shows the first comment block in
    % a file, and a license block there would hide it.

    properties
        Port % The serial port connected to the device: a pulsepal.DotNetSerialPort on Windows, otherwise a serialport
        info % Properties of the connected device: hardware and firmware versions, and its limits
        autoSync = true; % true: assigning a parameter programs the device at once. false: syncToDevice() sends them all
        isBiphasic % 1x4. 0 for monophasic pulses, 1 for biphasic. See "Output channel parameters" above
        phase1Voltage % 1x4, volts
        phase2Voltage % 1x4, volts. Biphasic pulses only
        restingVoltage % 1x4, volts. Between pulses, and while the channel is idle
        phase1Duration % 1x4, seconds
        interPhaseInterval % 1x4, seconds. Biphasic pulses only
        phase2Duration % 1x4, seconds. Biphasic pulses only
        interPulseInterval % 1x4, seconds, from the end of one pulse to the start of the next
        burstDuration % 1x4, seconds. 0 for no bursts
        interBurstInterval % 1x4, seconds
        pulseTrainDuration % 1x4, seconds
        pulseTrainDelay % 1x4, seconds from the trigger to the start of the pulse train
        linkTriggerChannel1 % 1x4. 1 if trigger channel 1 triggers the output channel, 0 if not
        linkTriggerChannel2 % 1x4. 1 if trigger channel 2 triggers the output channel, 0 if not
        customTrainID % 1x4. 0 for the pulse train defined by the parameters, or the number of the custom train to play
        customTrainTarget % 1x4. 0: custom train times are pulse onsets. 1: they are burst onsets
        customTrainLoop % 1x4. 1 repeats a custom train until pulseTrainDuration has elapsed, 0 plays it once
        playbackMode % 1x4. 0 plays the pulse train once per trigger, 1 plays it until stopped (continuous loop mode)
        triggerMode % 1x2, one per trigger channel: 0 normal, 1 toggle, 2 pulse gated, 3 param sync. See "Trigger modes"
    end

    properties (Access = private)
        firmwareVersion % Actual firmware version of connected device
        hardwareVersion % With newer firmware, a hardware version is explicitly returned
        cyclePeriod  % Update period of Pulse Pal hardware timer. Units = us
        cycleFrequency  % Update frequency of Pulse Pal hardware timer. Units = Hz
        nCustomPulseTrains % Number of custom pulse trains supported
        maxCustomPulses % Maximum number of custom pulses per pulse train supported
        ui % Struct with user interface handles
    end

    properties (Constant, Access = private)
        CurrentFirmwareVersion = 22; % Most recent firmware version
        OpMenuByte = 213; % Byte code to access op menu
        ParamNames = {... % Names of Pulse Pal's parameters. See: https://sites.google.com/site/pulsepalwiki/parameter-guide
            'isBiphasic' 'phase1Voltage' 'phase2Voltage' 'phase1Duration' 'interPhaseInterval' 'phase2Duration'...
            'interPulseInterval' 'burstDuration' 'interBurstInterval' 'pulseTrainDuration' 'pulseTrainDelay'...
            'linkTriggerChannel1' 'linkTriggerChannel2' 'customTrainID' 'customTrainTarget' 'customTrainLoop'... 
            'restingVoltage' 'playbackMode'};
    end

    methods
        function obj = PulsePalDevice(varargin)
            % P = PulsePalDevice(portName) connects to the Pulse Pal on a USB serial port, e.g. 'COM3' on Windows or
            % '/dev/ttyACM0' on Linux, checks that it runs supported Pulse Pal firmware, reads its properties into
            % info, shows "MATLAB Connected" on its screen, and programs the default parameters (see
            % setDefaultParams).

            % Check for minimum MATLAB version
            MinVer = '9.9';
            MinVerName = 'R2020b';
            if verLessThan('matlab', MinVer)
                error(['PulsePalDevice requires MATLAB ' MinVerName ' or newer.'...
                    char(10) 'If you must use previous MATLAB versions, please consider using the legacy interface.'])
            end

            % Find USB serial ports
            if nargin > 0
                portString = varargin{1};
            else
                PortList = obj.findSerialPorts();
                if ~isempty(PortList)
                    error(['You must call PulsePalDevice with a serial port string argument, e.g. P = PulsePalDevice(''COM3'')'...
                           newline 'Detected serial ports are: ' strjoin(PortList, ', ')])
                else
                    error('You must call PulsePalDevice with a serial port string argument.')
                end
            end

            % Connect to USB serial port and exchange handshake bytes
            % to confirm that the connected device is a Pulse Pal
            defaultBaudRate = 12000000;
            if isunix
                defaultBaudRate = 4000000;
            end
            % On Windows, MATLAB's serialport delivers each reply about 16 ms after it arrives, so every command that
            % waits for a confirm byte would take 16 ms. .NET's SerialPort takes about 0.3 ms (see
            % pulsepal.DotNetSerialPort). It is not available if MATLAB has been set to use .NET (Core) with dotnetenv,
            % and serialport is used then.
            if pulsepal.DotNetSerialPort.isAvailable()
                obj.Port = pulsepal.DotNetSerialPort(portString, defaultBaudRate);
            else
                obj.Port = serialport(portString, defaultBaudRate);
            end
            setDTR(obj.Port, true);
            obj.Port.write([obj.OpMenuByte 72], 'uint8');
            HandShakeOkByte = obj.Port.read(1, 'uint8'); % read() waits for the reply, up to the port's Timeout
            if HandShakeOkByte == 75
                % Check firmware version
                obj.firmwareVersion = obj.Port.read(1, 'uint32');
                if obj.firmwareVersion < 20
                    obj.Port = [];
                    error(['Error: Pulse Pal 1 detected. You must use the legacy interface.'... 
                           newline 'Add /PulsePal/MATLAB to the MATLAB path, and at the command prompt,'... 
                           newline 'type PulsePal(''Port'') where ''Port'' is your serial port string.']);
                else
                    if obj.firmwareVersion < 21
                        obj.Port = [];
                        error(['Error: Pulse Pal with old firmware detected. Please update your firmware.' ...
                               newline 'Update instructions are online at: '...
                               'https://sites.google.com/site/pulsepalwiki/updating-firmware']);
                    end
                    if obj.firmwareVersion > obj.CurrentFirmwareVersion
                        obj.Port = [];
                        error(['Error: Pulse Pal with future firmware detected.'...
                               newline 'Please update your MATLAB software or downgrade the firmware to v'... 
                               num2str(obj.CurrentFirmwareVersion) '.']);
                    end
                end
                % Get hardware info if supported
                if obj.firmwareVersion > 21
                    obj.Port.write([obj.OpMenuByte 94], 'uint8'); % Request hardware info
                    obj.hardwareVersion = obj.Port.read(1, 'uint8');
                    obj.cyclePeriod = obj.Port.read(1, 'uint32');
                    obj.cycleFrequency = 1/(obj.cyclePeriod/1000000);
                    obj.nCustomPulseTrains = obj.Port.read(1, 'uint8');
                    obj.maxCustomPulses = obj.Port.read(1, 'uint32');
                else
                    obj.hardwareVersion = 2;
                    obj.cyclePeriod = 50;
                    obj.cycleFrequency = 20000;
                    obj.nCustomPulseTrains = 2;
                    obj.maxCustomPulses = 5000;
                end

                % Create local copy of hardware description
                obj.info = struct;
                obj.info.hardwareVersion = obj.hardwareVersion;
                obj.info.firmwareVersion = obj.firmwareVersion;
                obj.info.minPulseWidth_us = 2*obj.cyclePeriod;
                obj.info.nCustomPulseTrains = obj.nCustomPulseTrains;
                obj.info.maxPulsesPerCustomTrain = obj.maxCustomPulses;
            elseif HandShakeOkByte == 87 % 'W': the device runs Wave Pal firmware (/Firmware/WavePal)
                wavePalVersion = obj.Port.read(1, 'uint32');
                obj.Port = [];
                error(['The device at port ' portString ' runs Wave Pal firmware (v' num2str(wavePalVersion) ...
                       '), not Pulse Pal firmware.' newline 'To use it as a Pulse Pal, load Pulse Pal firmware '...
                       'onto it with LoadPulsePalFirmware (in /MATLAB/FirmwareLoader).' newline ...
                       'To use it as a Wave Pal, connect with WavePalDevice.'])
            elseif HandShakeOkByte == 83 % 'S': the device runs Synth Pal firmware (/Firmware/SynthPal)
                synthPalVersion = obj.Port.read(1, 'uint32');
                obj.Port = [];
                error(['The device at port ' portString ' runs Synth Pal firmware (v' num2str(synthPalVersion) ...
                       '), not Pulse Pal firmware.' newline 'To use it as a Pulse Pal, load Pulse Pal firmware '...
                       'onto it with LoadPulsePalFirmware (in /MATLAB/FirmwareLoader).' newline ...
                       'To use it as a Synth Pal, connect with SynthPalDevice.'])
            else
                obj.Port = [];
                error(['The device at port ' portString ' returned an unexpected handshake signature.'])
            end

            % Set name of connected software
            obj.Port.write([obj.OpMenuByte 89 'MATLAB'], 'uint8');

            % Set default parameters
            obj.setDefaultParams;
        end

        function trigger(obj, channels, varargin)
            % Starts the pulse trains of output channels, e.g. P.trigger(1) or P.trigger([1 3 4]). The channels start
            % in the same timer cycle. A channel that is already playing a pulse train ignores the trigger.
            % P.trigger(1, 3, 4) works too. A character string is read as a binary number, with channel 1 as its last
            % digit: P.trigger('1101') triggers channels 1, 3 and 4, as in the legacy interface.
            if ischar(channels)
                TriggerAddress = bin2dec(channels);
            else
                if nargin > 1
                    channels = [channels cell2mat(varargin)];
                end
                if ~all(ismember(channels, [1 2 3 4]))
                    error('All channels must be valid Pulse Pal output channel indexes: 1,2,3 or 4')
                end
                ChannelsBinary = zeros(1,4);
                ChannelsBinary(channels) = 1;
                TriggerAddress = sum(ChannelsBinary .* [1 2 4 8]);
            end
            obj.Port.write([obj.OpMenuByte 77 TriggerAddress], 'uint8');
        end

        function stop(obj, varargin)
            % Stops pulse trains: P.stop() stops all output channels, and P.stop([1 3 4]) stops channels 1, 3 and 4
            % (firmware v22 or newer). The stopped channels return to their resting voltage. A soft trigger that has
            % not started its channel yet is cancelled too.
            bitCode = 15; % All channels
            if nargin > 1
                if obj.firmwareVersion < 22
                    error('stop() cannot address individual channels prior to firmware v22')
                end
                channels = varargin{1};
                if ~all(ismember(channels, [1 2 3 4]))
                    error('All channels must be valid Pulse Pal output channel indexes: 1,2,3 or 4')
                end
                bitCode = uint8(sum(bitshift(uint16(1), channels-1)));
            end
            if obj.firmwareVersion < 22
                obj.Port.write([obj.OpMenuByte 80], 'uint8');
            else
                obj.Port.write([obj.OpMenuByte 98 bitCode], 'uint8');
            end
        end

        function confirmed = syncToDevice(obj)
            % Sends every parameter to the device in one command. Use it with autoSync off: with autoSync on, every
            % assignment has already reached the device, and syncToDevice() raises an error.
            % On Pulse Pal 3, if either trigger channel is in param sync mode (triggerMode 3), the device stores
            % the parameters instead of programming them, and loads them on the next rising edge of that channel.
            % It still checks every value at once: an error is raised if one is out of range. See "Trigger modes" in
            % help PulsePalDevice.
            if obj.autoSync
                error('autoSync is set to ''true''. syncToDevice() may be used when autoSync is off.')
            end
            confirmed = obj.syncAllParams;
        end

        function confirmed = syncFromDevice(obj)
            % Reads every parameter from the device into the properties, e.g. after they were changed with the
            % joystick or by loading a settings file. Requires firmware v22 or newer. playbackMode is left as it is:
            % the device does not report it.
            confirmed = obj.importCurrentParamsFromPulsePal;
        end

        function confirmed = setVoltage(obj, channel, voltage)
            % Holds an output channel at a fixed voltage until it is set again or a pulse train is triggered on it,
            % e.g. P.setVoltage(4, 2.5). channel: 1-4. voltage: in volts, -10 to 10.
            obj.checkParamRange(voltage, 'Volts', [-10 10], 17);
            voltageBits = obj.volts2Bits(voltage);
            obj.Port.write([obj.OpMenuByte 79 channel typecast(uint16(voltageBits), 'uint8')], 'uint8');
            confirmed = obj.confirmWrite;
        end

        function setCalibration(obj, channel, voltageOffset)
            % Sets an output channel's zero code calibration: an offset, in volts from -0.1 to 0.1, that corrects the
            % DAC's offset error, e.g. P.setCalibration(2, -0.003). Pulse Pal 3 stores it in its EEPROM and applies
            % it after every power cycle; Pulse Pal 2 keeps it until it is switched off. Requires firmware v22.
            if obj.firmwareVersion > 21
                if ~ismember(channel, [1 2 3 4])
                    error('channel must be 1, 2, 3 or 4')
                end
                if ~isscalar(voltageOffset) || ~(voltageOffset >= -0.1 && voltageOffset <= 0.1) % Also refuses NaN
                    error('voltageOffset for zero code calibration must be in range [-0.1, 0.1]')
                end
                voltageBits = obj.roundHalfEven(voltageOffset*(1/(20/65536)));
                obj.Port.write([obj.OpMenuByte 96 channel-1 typecast(int16(voltageBits), 'uint8')], 'uint8');
                obj.confirmWrite;
                disp(['Zero code calibration set to ' num2str(voltageOffset) ' on channel ' num2str(channel) '.'])
            else
                error(['Zero code calibration requires firmware v22 or newer. Detected firmware is: v' num2str(obj.firmwareVersion)])
            end
        end

        function setScreenSaver(obj, state, timeout)
            % Switches the device's screen saver on (state = 1 or true) or off (state = 0 or false), and sets its timeout
            % in seconds (a whole number, 1-65535). With the screen saver on, the device dims its screen once it has been
            % left alone for the timeout: no command from the computer, no rising edge on a trigger channel, and no
            % joystick click or push. The next of these brings the screen back. Both settings are stored in the device's
            % EEPROM and kept through power cycles; the screen saver can also be switched on and off from the device's
            % joystick menu. A new device has it on, with 1800 s. The timeout is sent with every call, so leaving it out
            % sets 1800 s.
            % The device saves the settings once no channel is playing. Saving pauses its timer, usually for about
            % 20 us but, once in about 2000 changes, for tens of milliseconds, which would delay a trigger. Change the
            % settings before an experiment rather than during one.
            % Requires firmware v22 or newer. Only Pulse Pal 3 has a screen saver: Pulse Pal 2 accepts state = 0 only.
            % Example: setScreenSaver(P, 1, 300) dims the screen after 5 minutes without activity.
            if nargin < 3
                timeout = 1800;
            end
            if obj.firmwareVersion < 22
                error(['setScreenSaver() requires firmware v22 or newer. Detected firmware is: v' num2str(obj.firmwareVersion)])
            end
            if ~(isnumeric(state) || islogical(state)) || ~isscalar(state) || ~(state == 0 || state == 1) % Also refuses NaN
                error('state must be 1 (on) or 0 (off)')
            end
            if ~isnumeric(timeout) || ~isscalar(timeout) || ~(timeout >= 1 && timeout <= 65535) || timeout ~= round(timeout)
                error('timeout must be a whole number of seconds from 1 to 65535')
            end
            if state == 1 && obj.hardwareVersion < 3
                error('The screen saver requires hardware v3 or newer: Pulse Pal 2 accepts state = 0 only.')
            end
            obj.Port.write([obj.OpMenuByte 99 double(state) typecast(uint16(timeout), 'uint8')], 'uint8');
            obj.confirmWrite;
        end

        function sendCustomPulseTrain(obj, trainID, pulseTimes, voltages)
            % Loads a custom pulse train onto the device: a list of pulse onset times and a voltage for each pulse.
            % trainID: 1-4 on Pulse Pal 3, 1-2 on Pulse Pal 2.
            % pulseTimes: in seconds from the start of the train, increasing, in multiples of 100 us.
            % voltages: one per pulse, in volts, -10 to 10.
            % An output channel plays the train when its customTrainID is trainID. Each pulse takes the channel's own
            % phase durations; a biphasic pulse's second phase is its voltage with the sign reversed. Example:
            %   P.sendCustomPulseTrain(1, [0 0.1 0.25 0.5], [5 2.5 -2.5 -5]);
            %   P.customTrainID(2) = 1;
            sendCustomTrain(obj, trainID, pulseTimes, voltages);
        end

        function sendCustomWaveform(obj, trainID, samplingPeriod, voltages)
            % Loads a sampled waveform onto the device, as a custom pulse train of adjoining pulses: one per sample,
            % samplingPeriod seconds apart (a multiple of 100 us). Set phase1Duration to samplingPeriod on the output
            % channel that plays it, so that each sample lasts until the next. trainID: 1-4 on Pulse Pal 3, 1-2 on
            % Pulse Pal 2. voltages: in volts, -10 to 10.
            nVoltages = length(voltages);
            if ~isscalar(samplingPeriod) || ~isfinite(samplingPeriod) || samplingPeriod <= 0
                error('Error: the sampling period must be a positive number of seconds.');
            end
            if rem(round(samplingPeriod*1000000), obj.cyclePeriod*2) > 0
                error(['Error: sampling period must be a multiple of ' num2str(obj.cyclePeriod*2) ' microseconds.']);
            end
            pulseTimes = 0:samplingPeriod:((nVoltages*samplingPeriod)-(1*samplingPeriod));
            sendCustomTrain(obj, trainID, pulseTimes, voltages);
        end

        function setDefaultParams(obj)
            % Programs the default parameters: on all four output channels, monophasic 5 V pulses of 1 ms, 10 ms
            % apart, for 1 second, resting at 0 V and linked to trigger channel 1; both trigger channels in normal
            % mode. The constructor calls it.
            autoSyncState = obj.autoSync;
            if obj.hardwareVersion > 2
                % A device left in param sync mode would store the sync below instead of running it,
                % leaving the device on its old program until a TTL arrived. Assigning triggerMode is
                % not deferred that way, so take both trigger channels out of param sync mode first.
                % See the triggerMode property.
                obj.autoSync = true; % Assigning a parameter reaches the device at once only while autoSync is on
                obj.triggerMode = uint8(zeros(1,2));
            end
            obj.autoSync = false;
            obj.isBiphasic = zeros(1,4);
            obj.phase1Voltage = ones(1,4)*5;
            obj.phase2Voltage = ones(1,4)*-5;
            obj.restingVoltage = zeros(1,4);
            obj.phase1Duration = ones(1,4)*0.001;
            obj.interPhaseInterval = ones(1,4)*0.001;
            obj.phase2Duration = ones(1,4)*0.001;
            obj.interPulseInterval = ones(1,4)*0.01;
            obj.burstDuration = zeros(1,4);
            obj.interBurstInterval = zeros(1,4);
            obj.pulseTrainDuration = ones(1,4);
            obj.pulseTrainDelay = zeros(1,4);
            obj.linkTriggerChannel1 = ones(1,4);
            obj.linkTriggerChannel2 = zeros(1,4);
            obj.customTrainID = uint8(zeros(1,4));
            obj.customTrainTarget = uint8(zeros(1,4));
            obj.customTrainLoop = zeros(1,4);
            obj.playbackMode = zeros(1,4); % 0 = triggered 1 = continuous
            obj.triggerMode = uint8(zeros(1,2));
            obj.syncToDevice;
            obj.autoSync = autoSyncState;
        end

        function confirmed = sdSettings(obj, settingsFileName, op)
            % Saves the parameters to a settings file on the device's microSD card, loads them from one, or deletes
            % one, e.g. P.sdSettings('MyProgram.pps', 'save'). settingsFileName: the file's name with its extension,
            % such as .pps. op: 'save', 'load' or 'delete'. Loading a file also reads the parameters back into the
            % properties (see syncFromDevice). A saved file can also be loaded from the joystick menu.
            if sum(settingsFileName == '.') == 0
                error('Error: The file name must have a valid extension.')
            end
            op = lower(op);
            switch op
                case 'save'
                    OpByte = 1;
                case 'load'
                    OpByte = 2;
                case 'delete'
                    OpByte = 3;
                otherwise
                    error('File op must be: ''save'', ''load'' or ''delete''')
            end
            SettingsNameLength = length(settingsFileName);
            Message = [obj.OpMenuByte 90 OpByte SettingsNameLength settingsFileName];
            obj.Port.write(Message, 'uint8');
            confirmed = 1;
            if obj.firmwareVersion > 21
                confirmed = obj.Port.read(1, 'uint8'); % Sent after the file operation has finished
            elseif OpByte == 2
                pause(.1); % Firmware v21 does not acknowledge, so allow time for the load
            end
            if OpByte == 2
                % Read the parameters back even if the load failed: the device then loads its own default
                % parameters (not the ones setDefaultParams() sets), and the properties must describe them.
                obj.importCurrentParamsFromPulsePal;
            end
            if isempty(confirmed) || confirmed ~= 1
                if OpByte == 2
                    error(['Error: Pulse Pal could not load ' settingsFileName '. It has loaded its default '...
                        'parameters instead, and the properties have been updated to match.']);
                else
                    error('Error: Pulse Pal did not return an expected byte to confirm the operation.');
                end
            end
            confirmed = true;
        end

        function saveParameters(obj, filename)
            % Saves the parameters (this object's properties) to a .mat file on this computer, e.g.
            % P.saveParameters('MyProgram.mat'). loadParameters() programs the device with them again.
            if (~strcmp(filename(end-3:end), '.mat'))
                error('The file to save must be a .mat file')
            end
            params = obj.exportParams;
            save(filename, 'params');
        end

        function loadParameters(obj, filename)
            % Loads parameters from a .mat file saved by saveParameters(), and programs the device with them. autoSync
            % takes the value saved in the file.
            S = load(filename);
            params = S.params;
            obj.importParams(params);
            obj.autoSync = false;
            obj.syncToDevice;
            obj.autoSync = params.autoSync;
        end

        function formatMicroSD(obj)
            % Formats the device's microSD card (Pulse Pal 3), which erases every settings file on it, and programs
            % the default parameters. Asks for confirmation at the command prompt first.
            if obj.hardwareVersion < 3
                error('formatMicroSD() requires hardware v3 or newer.')
            end
            disp('*** Pulse Pal microSD Formatter ***')
            disp('This will format Pulse Pal''s microSD card,')
            disp('erase all settings files on the device')
            disp('and reset all parameters to defaults.')
            reply = input('Do you want to continue (y/n) > ', 's');
            if lower(reply) == 'y'
                obj.Port.write([obj.OpMenuByte 97], 'uint8');
                % The device replies with lines of status text, the last of which contains '!', and
                % then a confirm byte (1 if the card was formatted, 0 if not). The confirm byte is sent
                % after the device has reloaded its default parameters, so it can arrive well after the
                % text. It must be read here: left in the buffer, it would be taken as the reply to the
                % next command (the one sent by setDefaultParams() below), and every reply after that
                % would be read one byte late.
                tic;
                msg = [];
                lineEnd = [];
                replyComplete = false;
                while toc < 30 && ~replyComplete
                    if obj.Port.NumBytesAvailable > 0
                        msg = [msg obj.Port.read(obj.Port.NumBytesAvailable, 'uint8')];
                        flagIndex = find(msg == '!', 1);
                        if ~isempty(flagIndex)
                            lineEnd = flagIndex - 1 + find(msg(flagIndex:end) == 10, 1); % 10 = newline
                        end
                        replyComplete = ~isempty(lineEnd) && (length(msg) > lineEnd);
                    end
                    pause(.01);
                end
                if ~replyComplete
                    error('Pulse Pal did not report the result of formatting its microSD card within 30 seconds.')
                end
                disp(strtrim(char(msg(1:lineEnd))));
                success = (msg(lineEnd+1) == 1);
                obj.setDefaultParams();
                if ~success
                    error('Pulse Pal could not format its microSD card.')
                end
            else
                disp('Choice confirmed - microSD Card NOT formatted.')
            end
        end

        function set.phase1Voltage(obj, val)
            units = 'Volts'; paramCode = 2;
            obj.setOutputParam(paramCode, val, units);
            obj.phase1Voltage = val;
        end

        function set.phase2Voltage(obj, val)
            units = 'Volts'; paramCode = 3;
            obj.setOutputParam(paramCode, val, units);
            obj.phase2Voltage = val;
        end

        function set.restingVoltage(obj, val)
            units = 'Volts'; paramCode = 17;
            obj.setOutputParam(paramCode, val, units);
            obj.restingVoltage = val;
        end

        function set.phase1Duration(obj, val)
            units = 'Time'; paramCode = 4;
            obj.setOutputParam(paramCode, val, units);
            obj.phase1Duration = val;
        end

        function set.interPhaseInterval(obj, val)
            units = 'Time'; paramCode = 5;
            obj.setOutputParam(paramCode, val, units);
            obj.interPhaseInterval = val;
        end

        function set.phase2Duration(obj, val)
            units = 'Time'; paramCode = 6;
            obj.setOutputParam(paramCode, val, units);
            obj.phase2Duration = val;
        end

        function set.interPulseInterval(obj, val)
            units = 'Time'; paramCode = 7;
            obj.setOutputParam(paramCode, val, units);
            obj.interPulseInterval = val;
        end

        function set.burstDuration(obj, val)
            units = 'Time'; paramCode = 8;
            obj.setOutputParam(paramCode, val, units);
            obj.burstDuration = val;
        end

        function set.interBurstInterval(obj, val)
            units = 'Time'; paramCode = 9;
            obj.setOutputParam(paramCode, val, units);
            obj.interBurstInterval = val;
        end

        function set.pulseTrainDuration(obj, val)
            units = 'Time'; paramCode = 10;
            obj.setOutputParam(paramCode, val, units);
            obj.pulseTrainDuration = val;
        end

        function set.pulseTrainDelay(obj, val)
            units = 'Time'; paramCode = 11;
            obj.setOutputParam(paramCode, val, units);
            obj.pulseTrainDelay = val;
        end

        function set.linkTriggerChannel1(obj, val)
            units = 'Byte'; paramCode = 12;
            obj.setOutputParam(paramCode, val, units);
            obj.linkTriggerChannel1 = val;
        end

        function set.linkTriggerChannel2(obj, val)
            units = 'Byte'; paramCode = 13;
            obj.setOutputParam(paramCode, val, units);
            obj.linkTriggerChannel2 = val;
        end

        function set.customTrainID(obj, val)
            units = 'Byte'; paramCode = 14;
            obj.setOutputParam(paramCode, val, units);
            obj.customTrainID = val;
        end

        function set.customTrainTarget(obj, val)
            units = 'Byte'; paramCode = 15;
            obj.setOutputParam(paramCode, val, units);
            obj.customTrainTarget = val;
        end
        function set.customTrainLoop(obj, val)
            units = 'Byte'; paramCode = 16;
            obj.setOutputParam(paramCode, val, units);
            obj.customTrainLoop = val;
        end

        function set.isBiphasic(obj, val)
            units = 'Byte'; paramCode = 1;
            obj.setOutputParam(paramCode, val, units);
            obj.isBiphasic = val;
        end

        function set.triggerMode(obj, val)
            units = 'Byte'; paramCode = 128;
            obj.setOutputParam(paramCode, val, units);
            obj.triggerMode = val;
        end

        function set.playbackMode(obj, val)
            units = 'Byte'; paramCode = 18;
            obj.setOutputParam(paramCode, val, units);
            obj.playbackMode = val;
        end

        function set.autoSync(obj, val)
            if ~islogical(val)
                error('autoSync must be logical, e.g. P.autoSync = true;')
            end
            obj.autoSync = val;
        end

        function delete(obj)
            % Runs on clear P or delete(P). Tells the device that MATLAB is disconnecting, which stops all output
            % channels and puts the device's own name back on its screen, closes the GUI, and releases the serial
            % port. Errors are ignored: the device may already be unplugged.
            try
                obj.Port.write([obj.OpMenuByte 81], 'uint8');
            catch
                % Fail silently
            end
            try
                close(obj.ui.Figure)
            catch
                % Fail silently
            end
            obj.Port = [];
        end
    end

    methods (Access = private)
        function portStrings = findSerialPorts(obj)
            % Return likely serial-port candidates.
            portStrings = {}; % Initialize empty cell array
            portLocations = sort(serialportlist('available'));
            % Filter and add ports to portStrings
            for p = 1:length(portLocations)
                candidatePort = strtrim(portLocations{p}); % Trim whitespace
                if ~isempty(candidatePort) && (~ispc || ~strcmp(candidatePort, 'COM1')) % Exclude 'COM1' on Windows
                    if ~any(strcmp(candidatePort, portStrings))
                        portStrings{end+1} = candidatePort; % Add new port
                    end
                end
            end
        end

        function checkParamRange(obj, param, type, range, varargin)
            %   Validate numeric parameter values against an inclusive range.
            %   Throws an error that names the offending Pulse Pal parameter when any
            %   element falls outside range.
            RangeLow = range(1);
            RangeHigh = range(2);
            if nargin > 4
                paramCode = varargin{1};
                if paramCode < 128
                    paramCodeString = obj.ParamNames{paramCode};
                else
                    paramCodeString = 'triggerMode';
                end
            else
                paramCodeString = 'A parameter';
            end
            % NaN fails every comparison, so the range check below would let it through, and it would reach the device
            % as 0: -10 V, or a phase of 0 cycles
            if (~isnumeric(param) && ~islogical(param)) || ~all(isfinite(double(param(:))))
                error([paramCodeString ' must be a number (NaN and Inf are not allowed).']);
            end
            if strcmp(type, 'Byte') && any(double(param(:)) ~= round(double(param(:))))
                error([paramCodeString ' must be a whole number.']);
            end
            if (sum(param < RangeLow) > 0) || (sum(param > RangeHigh) > 0)
                error([paramCodeString ' was out of range: ' num2str(RangeLow) ' to ' num2str(RangeHigh)]);
            end
        end

        function bits = volts2Bits(obj, voltage)
            % Convert -10 to +10 V values to 16-bit DAC codes.
            bits = uint16(min(max(obj.roundHalfEven(((double(voltage) + 10) ./ 20) .* 65535), 0), 65535));
        end

        function rounded = roundHalfEven(obj, value)
            % Round to the nearest integer, and a value exactly halfway between two to the even one, as the Python
            % and C++ classes do, so that all three send the same DAC codes and cycle counts. MATLAB's round() and
            % uint32() take halves away from zero, which would play 125 us (2.5 cycles) as 150 us from MATLAB and
            % as 100 us from Python.
            rounded = round(value);
            halfway = abs(value - fix(value)) == 0.5;
            rounded(halfway) = 2*round(value(halfway)/2);
        end

        function volts = bytes2Volts(obj, bytes)
            % Convert serialized 16-bit DAC bytes to volt values.
            voltageBits = typecast(uint8(bytes), 'uint16');
            volts = ((double(voltageBits) ./ 65535) .* 20) - 10;
        end

        function seconds = bytes2Seconds(obj, Bytes)
            % Convert serialized hardware timer counts to seconds.
            seconds = double(typecast(uint8(Bytes), 'uint32'))/obj.cycleFrequency;
        end
        function confirmed = confirmWrite(obj)
            % Verify that the device acknowledged a write command.
            confirmed = obj.Port.read(1, 'uint8');
            % read() returns nothing (with a warning) if the byte does not arrive, and "if [] ~= 1" is false, so a
            % missing confirm byte has to be checked for: left unchecked, it arrives late and is read as the next reply
            if isempty(confirmed) || confirmed ~= 1
                error('Error: Pulse Pal did not return an expected byte to confirm the operation.');
            end
        end

        function setOutputParam(obj, paramCode, val, units)
            %   Validate, encode, and optionally transmit one parameter.
            %   paramCode identifies the Pulse Pal parameter. units selects byte, time,
            %   or voltage encoding. When obj.autoSync is true, the encoded command is
            %   written immediately and device acknowledgement is checked.
            if paramCode == 128
                if length(val) ~= 2
                    error('Error: there must be exactly one parameter value for each trigger channel.')
                end
            else
                if length(val) ~= 4
                    error('Error: there must be exactly one parameter value for each output channel.')
                end
            end
            switch units
                case 'Volts'
                    obj.checkParamRange(val, 'Volts', [-10 10], paramCode);
                    value2send = obj.volts2Bits(val);
                case 'Time'
                    switch paramCode
                        case 4
                            range = [0.0001 3600];
                        case 6
                            range = [0.0001 3600];
                        case 7
                            range = [0.0001 3600];
                        case 10
                            range = [0.0001 3600];
                        otherwise
                            range = [0 3600];
                    end
                    % Checked in whole timer cycles, as the device receives it: [100 100 100 100]*1e-6 is
                    % 9.999999999999999e-05 in floating point, under the 0.0001 minimum, but is exactly 2 cycles.
                    obj.checkParamRange(val, 'Time', [-Inf Inf], paramCode); % A number, not NaN or Inf
                    value2send = obj.roundHalfEven(double(val)*obj.cycleFrequency);
                    obj.checkParamRange(value2send/obj.cycleFrequency, 'Time', range, paramCode);
                case 'Byte'
                    switch paramCode
                        case 1
                            range = [0 1];
                        case 12
                            range = [0 1];
                        case 13
                            range = [0 1];
                        case 14
                            range = [0 obj.nCustomPulseTrains];
                        case 15
                            range = [0 1];
                        case 16
                            range = [0 1];
                        case 18
                            range = [0 1];
                        case 128
                            if obj.hardwareVersion > 2
                                range = [0 3]; % Param sync mode (3) is Pulse Pal 3 only
                            else
                                range = [0 2];
                            end
                    end
                    obj.checkParamRange(val, 'Byte', range, paramCode);
                    value2send = val;
            end
            if obj.autoSync
                Msg = [];
                if sum(paramCode == [2 3 17]) > 0
                    if obj.firmwareVersion > 21
                        obj.Port.write([obj.OpMenuByte 91 paramCode typecast(uint16(value2send), 'uint8')], 'uint8');
                    else
                        for i = 1:4
                            Msg = [Msg obj.OpMenuByte 74 paramCode i typecast(uint16(value2send(i)), 'uint8')];
                        end
                        obj.Port.write(Msg, 'uint8');
                    end
                elseif sum(paramCode == [4 5 6 7 8 9 10 11]) > 0
                    if obj.firmwareVersion > 21
                        obj.Port.write([obj.OpMenuByte 91 paramCode typecast(uint32(value2send), 'uint8')], 'uint8');
                    else
                        for i = 1:4
                            Msg = [Msg obj.OpMenuByte 74 paramCode i typecast(uint32(value2send(i)), 'uint8')];
                        end
                        obj.Port.write(Msg, 'uint8');
                    end
                else
                    if obj.firmwareVersion > 21
                        obj.Port.write([obj.OpMenuByte 91 paramCode value2send], 'uint8');
                    else
                        if paramCode < 18 || paramCode == 128 % Firmware v21 did not process code 18
                            for i = 1:4
                                Msg = [Msg obj.OpMenuByte 74 paramCode i value2send(i)];
                            end
                        end
                        obj.Port.write(Msg, 'uint8');
                    end
                end
                obj.confirmWrite;
            end
        end

        function confirmed = syncAllParams(obj)
            %   syncAllParams Encode and transmit the complete parameter set.
            %   Used when autoSync is false to batch all output and trigger parameters
            %   into a single command. This is more efficient than item-wise data transfers.
            if obj.autoSync
                error('The autoSync field is set to ''true''. syncAllParams() may be used when autoSync is false.')
            end
            for i = 1:4
                if obj.customTrainTarget(i) == 1
                    BDuration = obj.burstDuration(i);
                    if BDuration == 0
                        error(['Error in output channel ' num2str(i)... 
                            ': When custom train times target burst onsets, a non-zero burst duration must be defined.'])
                    end
                end
            end
            TimeData = obj.roundHalfEven([obj.phase1Duration; obj.interPhaseInterval; obj.phase2Duration;...
                obj.interPulseInterval; obj.burstDuration; obj.interBurstInterval;...
                obj.pulseTrainDuration; obj.pulseTrainDelay]*obj.cycleFrequency);
            TimeData = TimeData';
            VoltageData = [obj.volts2Bits(obj.phase1Voltage); obj.volts2Bits(obj.phase2Voltage); obj.volts2Bits(obj.restingVoltage)];
            VoltageData = VoltageData';
            playbackModeData = [];
            if obj.firmwareVersion > 21
                playbackModeData = obj.playbackMode;
            end
            SingleByteOutputParams = [obj.isBiphasic; obj.customTrainID; obj.customTrainTarget; obj.customTrainLoop; playbackModeData];
            opCode = 92;
            if obj.firmwareVersion < 22 % Use op 73 for firmware v21
                opCode = 73;
                TimeData = TimeData';
                VoltageData = VoltageData';
            else
                SingleByteOutputParams = SingleByteOutputParams';
            end
            SingleByteParams = [SingleByteOutputParams(1:end) obj.linkTriggerChannel1 obj.linkTriggerChannel2 obj.triggerMode];
            obj.Port.write([obj.OpMenuByte opCode typecast(uint32(TimeData(1:end)), 'uint8') ...
                typecast(uint16(VoltageData(1:end)), 'uint8') SingleByteParams], 'uint8');
            confirmed = obj.confirmWrite;
        end

        function sendCustomTrain(obj, trainID, pulseTimes, voltages)
            %   Validate and transmit a custom train of pulses to Pulse Pal.
            if length(pulseTimes) ~= length(voltages)
                error('There must be one voltage value for every timestamp');
            end
            nPulses = length(pulseTimes);
            if ~all(isfinite(double(pulseTimes(:)))) || ~all(isfinite(double(voltages(:))))
                error('Error: custom pulse times and voltages must be numbers (NaN and Inf are not allowed).');
            end
            if nPulses > obj.maxCustomPulses
                error(['Error: Attempted to send ' num2str(nPulses) ' pulses. Pulse Pal '... 
                    num2str(obj.info.hardwareVersion) ' can only store '... 
                    num2str(obj.maxCustomPulses) ' pulses per custom pulse train.']);
            end
            if sum(sum(rem(round(pulseTimes*1000000), obj.cyclePeriod*2))) > 0
                error(['Non-zero time values for Pulse Pal must be multiples of ' num2str(obj.cyclePeriod*2) ' microseconds.']);
            end
            if (sum(pulseTimes < 0) > 0)
                error('Error: Custom pulse times must be positive');
            end
            CandidateTimes = uint32(obj.roundHalfEven(double(pulseTimes)*obj.cycleFrequency));
            CandidateVoltages = voltages;
            % The device plays each pulse until the next one's time, so a time that is not later than the one
            % before it would freeze the output for the rest of the train. diff() is taken on doubles because
            % uint32 subtraction saturates at 0 in MATLAB, which would hide decreasing times.
            if any(diff(double(CandidateTimes)) <= 0)
                error('Error: Custom pulse times must always increase');
            end
            if (CandidateTimes(end) > (3600*obj.cycleFrequency))
                error('Error: Custom pulse times must be < 3600 s');
            end
            if (sum(abs(CandidateVoltages) > 10) > 0)
                error('Error: Custom voltage range = -10V to +10V');
            end
            if (length(CandidateVoltages) ~= length(CandidateTimes))
                error('Error: There must be a voltage for every timestamp');
            end
            TimeOutput = CandidateTimes;
            VoltageOutput = obj.volts2Bits(voltages);
            if ~ismember(trainID, 1:obj.nCustomPulseTrains)
                error(['The custom pulse train ID must be an integer in range 1:' num2str(obj.nCustomPulseTrains)])
            end
            opCode = 95;
            trainCode = trainID-1;
            if obj.firmwareVersion < 22
                if trainID == 1
                    opCode = 75;
                else
                    opCode = 76;
                end
                trainCode = [];
            end
            obj.Port.write([obj.OpMenuByte opCode trainCode typecast(uint32([nPulses TimeOutput]), 'uint8') ...
                typecast(uint16(VoltageOutput), 'uint8')], 'uint8');
            obj.confirmWrite;
        end
        function confirmed = importCurrentParamsFromPulsePal(obj)
            %   importCurrentParamsFromPulsePal Import all parameters currently stored on the device.
            %   Firmware v22 or newer is required. The method reads the packed parameter
            %   message, decodes times and voltages, and updates object properties while
            %   temporarily disabling autoSync.
            if obj.firmwareVersion < 22
                error(['importCurrentParamsFromPulsePal() requires firmware v22 or newer.'...
                      newline 'Detected firmware is v' num2str(obj.firmwareVersion)])
            end
            confirmed = false;
            obj.Port.write([obj.OpMenuByte 93], 'uint8');
            Msg = obj.Port.read(178, 'uint8');
            if length(Msg) == 178
                confirmed = true;
            end
            autoSyncState = obj.autoSync;
            obj.autoSync = false;
            Pos = 1;
            obj.phase1Duration = obj.bytes2Seconds(Msg(Pos:Pos+15)); Pos = Pos + 16;
            obj.interPhaseInterval = obj.bytes2Seconds(Msg(Pos:Pos+15)); Pos = Pos + 16;
            obj.phase2Duration = obj.bytes2Seconds(Msg(Pos:Pos+15)); Pos = Pos + 16;
            obj.interPulseInterval = obj.bytes2Seconds(Msg(Pos:Pos+15)); Pos = Pos + 16;
            obj.burstDuration = obj.bytes2Seconds(Msg(Pos:Pos+15)); Pos = Pos + 16;
            obj.interBurstInterval = obj.bytes2Seconds(Msg(Pos:Pos+15)); Pos = Pos + 16;
            obj.pulseTrainDuration = obj.bytes2Seconds(Msg(Pos:Pos+15)); Pos = Pos + 16;
            obj.pulseTrainDelay = obj.bytes2Seconds(Msg(Pos:Pos+15)); Pos = Pos + 16;
            obj.phase1Voltage = obj.bytes2Volts(Msg(Pos:Pos+7)); Pos = Pos + 8;
            obj.phase2Voltage = obj.bytes2Volts(Msg(Pos:Pos+7)); Pos = Pos + 8;
            obj.restingVoltage = obj.bytes2Volts(Msg(Pos:Pos+7)); Pos = Pos + 8;
            obj.isBiphasic = Msg(Pos:Pos+3); Pos = Pos + 4;
            obj.customTrainID = Msg(Pos:Pos+3); Pos = Pos + 4;
            obj.customTrainTarget = Msg(Pos:Pos+3); Pos = Pos + 4;
            obj.customTrainLoop = Msg(Pos:Pos+3); Pos = Pos + 4;
            obj.linkTriggerChannel1 = Msg(Pos:Pos+3); Pos = Pos + 4;
            obj.linkTriggerChannel2 = Msg(Pos:Pos+3); Pos = Pos + 4;
            obj.triggerMode = Msg(Pos:Pos+1);
            obj.autoSync = autoSyncState;
        end
        function params = exportParams(obj)
            % Export the current parameters of the PulsePalDevice object to a struct
            params = struct;
            params.autoSync = obj.autoSync;
            params.isBiphasic = obj.isBiphasic;
            params.phase1Voltage = obj.phase1Voltage;
            params.phase2Voltage = obj.phase2Voltage;
            params.restingVoltage = obj.restingVoltage;
            params.phase1Duration = obj.phase1Duration;
            params.interPhaseInterval = obj.interPhaseInterval;
            params.phase2Duration = obj.phase2Duration;
            params.interPulseInterval = obj.interPulseInterval;
            params.burstDuration = obj.burstDuration;
            params.interBurstInterval = obj.interBurstInterval;
            params.pulseTrainDuration = obj.pulseTrainDuration;
            params.pulseTrainDelay = obj.pulseTrainDelay;
            params.linkTriggerChannel1 = obj.linkTriggerChannel1;
            params.linkTriggerChannel2 = obj.linkTriggerChannel2;
            params.customTrainID = obj.customTrainID;
            params.customTrainTarget = obj.customTrainTarget;
            params.customTrainLoop = obj.customTrainLoop;
            params.playbackMode = obj.playbackMode;
            params.triggerMode = obj.triggerMode;
        end

        function importParams(obj, params)
            % Import a struct of parameters to be the current parameters of the PulsePalDevice object
            obj.isBiphasic = params.isBiphasic;
            obj.phase1Voltage = params.phase1Voltage;
            obj.phase2Voltage = params.phase2Voltage;
            obj.restingVoltage = params.restingVoltage;
            obj.phase1Duration = params.phase1Duration;
            obj.interPhaseInterval = params.interPhaseInterval;
            obj.phase2Duration = params.phase2Duration;
            obj.interPulseInterval = params.interPulseInterval;
            obj.burstDuration = params.burstDuration;
            obj.interBurstInterval = params.interBurstInterval;
            obj.pulseTrainDuration = params.pulseTrainDuration;
            obj.pulseTrainDelay = params.pulseTrainDelay;
            obj.linkTriggerChannel1 = params.linkTriggerChannel1;
            obj.linkTriggerChannel2 = params.linkTriggerChannel2;
            obj.customTrainID = params.customTrainID;
            obj.customTrainTarget = params.customTrainTarget;
            obj.customTrainLoop = params.customTrainLoop;
            obj.playbackMode = params.playbackMode;
            obj.triggerMode = params.triggerMode;
        end
        function fh = makeCallback(obj, fun, varargin)
            % makeCallback Create a UI callback that safely dispatches to a local function.
            %   Stores a weak reference to the device object when supported so GUI callbacks
            %   do not keep deleted objects alive.
            cObj = PulsePalDevice.getCallbackObject(obj);
            extraArgs = varargin;
            fh = @(~,~)PulsePalDevice.dispatchCallback( ...
                cObj, fun, extraArgs{:});
        end
    end
    methods (Static, Access = private)
        function params = defaultParams
            % defaultParams returns a struct containing default Pulse Pal parameters.
            params = struct;
            params.isBiphasic = zeros(1,4);
            params.restingVoltage = zeros(1,4);
            params.phase1Voltage = ones(1,4)*5;
            params.phase2Voltage = ones(1,4)*-5;
            params.phase1Duration = ones(1,4)*0.001;
            params.interPhaseInterval = ones(1,4)*0.001;
            params.phase2Duration = ones(1,4)*0.001;
            params.interPulseInterval = ones(1,4)*0.01;
            params.burstDuration = zeros(1,4);
            params.interBurstInterval = zeros(1,4);
            params.pulseTrainDuration = ones(1,4);
            params.pulseTrainDelay = zeros(1,4);
            params.linkTriggerChannel1 = ones(1,4);
            params.linkTriggerChannel2 = zeros(1,4);
            params.customTrainID = uint8(zeros(1,4));
            params.customTrainTarget = uint8(zeros(1,4));
            params.customTrainLoop = zeros(1,4);
            params.playbackMode = zeros(1,4);
            params.triggerMode = uint8(zeros(1,2));
        end

        function cObj = getCallbackObject(obj)
            % Return a weak reference if supported or direct object reference if not
            if PulsePalDevice.supportsWeakReference()
                cObj = matlab.lang.WeakReference(obj);
            else
                cObj = obj;
            end
        end

        function tf = supportsWeakReference()
            % supportsWeakReference True when the MATLAB release supports matlab.lang.WeakReference.
            tf = exist('matlab.lang.WeakReference', 'class') == 8;
        end

        function obj = resolveCallbackObject(cObj)
            if isa(cObj, 'matlab.lang.WeakReference')
                obj = cObj.Handle;
            else
                obj = cObj;
            end

            if isempty(obj) || ~isvalid(obj)
                obj = [];
            end
        end

        function dispatchCallback(cObj, fun, varargin)
            % Invoke a GUI callback only when the device object is still valid.
            obj = PulsePalDevice.resolveCallbackObject(cObj);
            if isempty(obj)
                return
            end
            fun(obj, varargin{:});
        end
    end
end
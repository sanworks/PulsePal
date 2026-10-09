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
%   P.triggerMode{2} = 'Toggle';         % Trigger channel 2 in toggle mode (see "Trigger modes" below)
%   clear P                              % Releases the port. Pulse Pal stops all output channels.
%
% Parameters are properties, with one element per channel: the output channel parameters below are 1x4, so
% P.phase1Voltage(2) belongs to output channel 2, and triggerMode is a 1x2 cell array, one element per trigger
% channel. Assigning a whole parameter takes one value per channel: P.phase1Voltage = 5 raises an error, because a
% single value does not say which channels it is meant for. P.phase1Voltage = [5 5 5 5] or P.phase1Voltage(:) = 5
% sets all four. With autoSync on (the default), assigning a parameter programs the device at once. With autoSync
% off, assignments change only this object's copy of the parameters, and syncToDevice() sends all of them in one
% command:
%   P.autoSync = false;
%   P.phase1Voltage = [5 5 2.5 2.5];
%   P.phase1Duration = [0.001 0.001 0.002 0.002];
%   P.syncToDevice();
%   P.autoSync = true;
%
% Units and values. Voltages are in volts, -10 to 10. Times are in seconds, from 0 to 9999.9999 (info.maxTime, the
% longest time the device's screen shows), rounded to the nearest cycle of the device's timer (50 us); the property
% then holds the time the device plays, so 0.00012 reads back as 0.0001. Phase durations, the inter-pulse interval and
% the pulse train duration are at least 100 us (info.minPulseWidth_us): the shortest pulse that another Pulse Pal's
% trigger channel detects reliably. Custom pulse times are not rounded: each must be a multiple of 100 us. On/off
% parameters are logical (true or false; 1 and 0 work too), and modes are names, such as 'Gated' (not case sensitive;
% the numbers older Pulse Pal software used work too). A value out of range raises an error, and nothing is sent.
%
% Output channel parameters (1x4):
%   isBiphasic           false: monophasic pulses, phase 1 only. true: biphasic pulses: phase 1, interPhaseInterval,
%                        phase 2
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
%   linkTriggerChannel1  true if a TTL on trigger channel 1 triggers the output channel
%   linkTriggerChannel2  true if a TTL on trigger channel 2 triggers the output channel
%   customTrainID        0 plays the pulse train defined above. 1 or more plays that custom train instead, loaded
%                        with sendCustomPulseTrain() or sendCustomWaveform(): 1-4 on Pulse Pal 3, 1-2 on Pulse Pal 2
%   customTrainTarget    1x4 cell array. 'Pulses': a custom train's times are pulse onsets. 'Bursts': they are
%                        burst onsets
%   customTrainLoop      true repeats a custom train until pulseTrainDuration has elapsed. false plays it once
%   continuousLoop       false plays the pulse train once per trigger. true plays it until the channel is stopped,
%                        ignoring pulseTrainDuration (firmware v22 or newer)
% A pulse starts only if it fits. In a burst, its first phase, or a biphasic pulse's whole pulse, must end before the
% burst does. A biphasic pulse must also end by the end of the train; the end of the train cuts a monophasic pulse
% short. The parameter guide, https://sites.google.com/site/pulsepalwiki/parameter-guide, has diagrams.
%
% Trigger modes (triggerMode, a 1x2 cell array, one per trigger channel; info.triggerModes lists them):
%   'Normal'      A rising edge starts the pulse trains of the linked output channels. Edges during a train are
%                 ignored.
%   'Toggle'      As 'Normal', but a rising edge during a train stops it.
%   'Gated'       The trains play only while the TTL is high.
%   'Param Sync'  (Pulse Pal 3 only) A rising edge starts and stops nothing. It loads the parameters most recently sent
%                 by syncToDevice(): this is how the next trial's parameters are sent during the current trial and
%                 applied the instant it starts. An idle output channel takes them in the timer cycle the edge is
%                 detected. One that is playing a pulse train finishes it on the parameters it started with, and takes
%                 the new ones the moment it ends, so the next trigger plays a whole train with the new parameters. A
%                 channel in continuous loop mode has no train end, so it keeps its parameters until something stops
%                 it. Only syncToDevice() waits for the edge: with autoSync on, assigning a parameter programs the
%                 device at once, also in param sync mode. So leaving param sync mode means assigning triggerMode with
%                 autoSync on; a trigger mode sent by syncToDevice() takes effect at the next edge. A param sync
%                 channel's links to output channels are ignored: to start trains on the same edge, wire the TTL to the
%                 other trigger channel too (the parameters load first). Connecting, and setDefaultParams(), take both
%                 trigger channels out of param sync mode.
%   P.triggerMode{2} = 'Param Sync';     % Sent at once
%   P.autoSync = false;
%   P.phase1Voltage(1) = 2;
%   P.syncToDevice();                    % Stored: trigger channel 2's next rising edge applies it
%   P.autoSync = true;
%
% Methods (help PulsePalDevice.trigger, and so on, describes each one):
%   trigger(channels)                    Starts the pulse trains of output channels, e.g. P.trigger([1 3])
%   stop(channels)                       Stops pulse trains: P.stop() stops all of them, P.stop([1 3]) some
%   setFixedVoltage(channels, voltage)   Holds output channels at a fixed voltage until they are triggered
%   sendCustomPulseTrain(customTrainID, pulseTimes, voltages)    Loads a custom pulse train
%   sendCustomWaveform(customTrainID, samplingPeriod, voltages)  Loads a sampled waveform, as a custom pulse train
%   syncToDevice()                       Sends every parameter to the device in one command
%   syncFromDevice()                     Reads every parameter from the device into the properties
%   setDefaultParams()                   Programs the default parameters
%   exportParams()                       Returns every parameter as a struct, e.g. to save with your data;
%                                        importParams(params) programs the device with one
%   saveParameters(fileName)             Saves the parameters to a .json program file, which both GUIs open;
%                                        loadParameters(fileName) programs the device with one
%   saveSettingsFile(fileName)           Saves the parameters to a settings file on the device's microSD card;
%                                        loadSettingsFile(fileName) and deleteSettingsFile(fileName)
%   gui()                                Opens the parameter editor window
%   setScreenSaver(enabled, timeout), setCalibration(channel, voltageOffset), formatMicroSD()
% info holds the connected device's hardware and firmware versions, and its limits.
%
% The USB protocol is documented in /Firmware/PROTOCOL.md. The Python class, pulsepal.PulsePalDevice
% (/Python/PulsePal/pulsepal/pulse_pal.py), works the same way, with snake_case names.

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

    properties (SetAccess = private)
        port % The serial port connected to the device: a pulsepal.DotNetSerialPort on Windows, otherwise a serialport
        info % Properties of the connected device: hardware and firmware versions, and its limits
    end

    properties
        autoSync = true; % true: assigning a parameter programs the device at once. false: syncToDevice() sends them all
        isBiphasic % 1x4 logical. false for monophasic pulses, true for biphasic. See "Output channel parameters" above
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
        linkTriggerChannel1 % 1x4 logical. true if trigger channel 1 triggers the output channel
        linkTriggerChannel2 % 1x4 logical. true if trigger channel 2 triggers the output channel
        customTrainID % 1x4. 0 for the pulse train defined by the parameters, or the number of the custom train to play
        customTrainTarget % 1x4 cell array. 'Pulses': custom train times are pulse onsets. 'Bursts': burst onsets
        customTrainLoop % 1x4 logical. true repeats a custom train until pulseTrainDuration has elapsed
        continuousLoop % 1x4 logical. false plays the pulse train once per trigger, true plays it until stopped
        triggerMode % 1x2 cell array, one per trigger channel: 'Normal', 'Toggle', 'Gated' or 'Param Sync'
    end

    properties (Access = private)
        firmwareVersion % Actual firmware version of connected device
        hardwareVersion % With newer firmware, a hardware version is explicitly returned
        cyclePeriod  % Update period of Pulse Pal hardware timer. Units = us
        cycleFrequency  % Update frequency of Pulse Pal hardware timer. Units = Hz
        nCustomPulseTrains % Number of custom pulse trains supported
        maxCustomPulses % Maximum number of custom pulses per pulse train supported
        ui % Struct with user interface handles
        storing = false % True while values are stored unchecked: read from the device, or restored by importParams()
    end

    properties (Constant, Access = private)
        CurrentFirmwareVersion = 22; % Most recent firmware version
        OpMenuByte = 213; % Byte code to access op menu
        MaxTime = 9999.9999 % Longest time parameter or custom pulse time, in seconds: the most the device's joystick
                            % menu shows and edits (four digits before the point), as in the Python and C++ classes
        MaxSettingsFileNameLength = 15 % Characters, including '.pps'
        ProgramFormatVersion = 1 % Version of the program file format (see saveParameters()), as in the Python GUI
        % Output parameter names in order of their parameter code, and the kind of value each takes. The codes are
        % fixed: see "Parameter codes" in /Firmware/PROTOCOL.md. A 'PulseTime' is a time of at least 2 timer cycles.
        ParamNames = {...
            'isBiphasic' 'phase1Voltage' 'phase2Voltage' 'phase1Duration' 'interPhaseInterval' 'phase2Duration'...
            'interPulseInterval' 'burstDuration' 'interBurstInterval' 'pulseTrainDuration' 'pulseTrainDelay'...
            'linkTriggerChannel1' 'linkTriggerChannel2' 'customTrainID' 'customTrainTarget' 'customTrainLoop'...
            'restingVoltage' 'continuousLoop'};
        ParamKinds = {...
            'Logical' 'Volts' 'Volts' 'PulseTime' 'Time' 'PulseTime'...
            'PulseTime' 'Time' 'Time' 'PulseTime' 'Time'...
            'Logical' 'Logical' 'TrainID' 'Target' 'Logical'...
            'Volts' 'Logical'};
        TriggerModeCode = 128 % The parameter code of triggerMode, the one trigger channel parameter
        TriggerModeNames = {'Normal', 'Toggle', 'Gated', 'Param Sync'} % In order of their code on the device (0-3)
        CustomTrainTargetNames = {'Pulses', 'Bursts'} % In order of their code on the device
    end

    methods
        function obj = PulsePalDevice(varargin)
            % P = PulsePalDevice(portName) connects to the Pulse Pal on a USB serial port, e.g. 'COM3' on Windows or
            % '/dev/ttyACM0' on Linux, checks that it runs supported Pulse Pal firmware, reads its properties into
            % info, shows "MATLAB Connected" on its screen, and programs the default parameters (see
            % setDefaultParams). portName may also be an open port object with the methods of
            % pulsepal.DotNetSerialPort, as in /MATLAB/tests/testPulsePalDeviceOffline.m, which simulates a device.

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
            if ~(ischar(portString) || isstring(portString))
                obj.port = portString; % An open port object (see above)
                portString = char(obj.port.Port);
            elseif pulsepal.DotNetSerialPort.isAvailable()
                obj.port = pulsepal.DotNetSerialPort(portString, defaultBaudRate);
            else
                obj.port = serialport(portString, defaultBaudRate);
            end
            portString = char(portString);
            setDTR(obj.port, true);
            % Bytes already waiting, e.g. the end of a reply to a session that was cut short, would be read as the reply
            % to the handshake. A reply the device sends after this, to a command it had not reached yet, is skipped by
            % readHandshakeReply().
            flush(obj.port);
            obj.port.write([obj.OpMenuByte 72], 'uint8');
            reply = obj.readHandshakeReply(); % 'K' (75), then the firmware version (uint32)
            HandShakeOkByte = [];
            replyVersion = 0;
            if numel(reply) == 5
                HandShakeOkByte = reply(1);
                replyVersion = double(typecast(uint8(reply(2:5)), 'uint32'));
            end
            if HandShakeOkByte == 75
                % Check firmware version
                obj.firmwareVersion = replyVersion;
                if obj.firmwareVersion < 20
                    obj.port = [];
                    error(['Error: Pulse Pal 1 detected. You must use the legacy interface.'...
                           newline 'Add /PulsePal/MATLAB to the MATLAB path, and at the command prompt,'...
                           newline 'type PulsePal(''Port'') where ''Port'' is your serial port string.']);
                else
                    if obj.firmwareVersion < 21
                        obj.port = [];
                        error(['Error: Pulse Pal with old firmware detected. Please update your firmware.' ...
                               newline 'Update instructions are online at: '...
                               'https://sites.google.com/site/pulsepalwiki/updating-firmware']);
                    end
                    if obj.firmwareVersion > obj.CurrentFirmwareVersion
                        % New firmware only adds commands (see /Firmware/PROTOCOL.md), so this class still works with it
                        warning('PulsePalDevice:newerFirmware', ['Pulse Pal firmware v' num2str(obj.firmwareVersion) ...
                                ' is newer than this class knows (v' num2str(obj.CurrentFirmwareVersion) '). It '...
                                'works with it, but update the MATLAB software to use what is new.'])
                    elseif obj.firmwareVersion < obj.CurrentFirmwareVersion
                        warning('PulsePalDevice:olderFirmware', ['Pulse Pal firmware v' num2str(obj.firmwareVersion) ...
                                ' is supported, but v' num2str(obj.CurrentFirmwareVersion) ' is available: see '...
                                '/MATLAB/FirmwareLoader.'])
                    end
                end
                % Get hardware info if supported
                if obj.firmwareVersion > 21
                    obj.port.write([obj.OpMenuByte 94], 'uint8'); % Request hardware info
                    obj.hardwareVersion = obj.port.read(1, 'uint8');
                    obj.cyclePeriod = obj.port.read(1, 'uint32');
                    obj.cycleFrequency = 1/(obj.cyclePeriod/1000000);
                    obj.nCustomPulseTrains = obj.port.read(1, 'uint8');
                    obj.maxCustomPulses = obj.port.read(1, 'uint32');
                else
                    obj.hardwareVersion = 2;
                    obj.cyclePeriod = 50;
                    obj.cycleFrequency = 20000;
                    obj.nCustomPulseTrains = 2;
                    obj.maxCustomPulses = 5000;
                end

                % Create local copy of hardware description, with the fields of the Python class's DeviceInfo
                obj.info = struct;
                obj.info.outputParameterNames = obj.ParamNames;
                obj.info.triggerModes = obj.availableTriggerModes();
                obj.info.customTrainTargets = obj.CustomTrainTargetNames;
                obj.info.firmwareVersion = obj.firmwareVersion;
                obj.info.hardwareVersion = obj.hardwareVersion;
                obj.info.maxCustomPulses = obj.maxCustomPulses;
                obj.info.nCustomPulseTrains = obj.nCustomPulseTrains;
                obj.info.cycleFrequency = obj.cycleFrequency;
                obj.info.cyclePeriod_us = obj.cyclePeriod;
                obj.info.minPulseWidth_us = 2*obj.cyclePeriod;
                obj.info.maxTime = obj.MaxTime;
            elseif HandShakeOkByte == 87 % 'W': the device runs Wave Pal firmware (/Firmware/WavePal)
                wavePalVersion = replyVersion;
                obj.port = [];
                error(['The device at port ' portString ' runs Wave Pal firmware (v' num2str(wavePalVersion) ...
                       '), not Pulse Pal firmware.' newline 'To use it as a Pulse Pal, load Pulse Pal firmware '...
                       'onto it with LoadPulsePalFirmware (in /MATLAB/FirmwareLoader).' newline ...
                       'To use it as a Wave Pal, connect with WavePalDevice.'])
            elseif HandShakeOkByte == 83 % 'S': the device runs Synth Pal firmware (/Firmware/SynthPal)
                synthPalVersion = replyVersion;
                obj.port = [];
                error(['The device at port ' portString ' runs Synth Pal firmware (v' num2str(synthPalVersion) ...
                       '), not Pulse Pal firmware.' newline 'To use it as a Pulse Pal, load Pulse Pal firmware '...
                       'onto it with LoadPulsePalFirmware (in /MATLAB/FirmwareLoader).' newline ...
                       'To use it as a Synth Pal, connect with SynthPalDevice.'])
            else
                obj.port = [];
                error(['The device at port ' portString ' returned an unexpected handshake signature.'])
            end

            % Set name of connected software
            obj.port.write([obj.OpMenuByte 89 'MATLAB'], 'uint8');

            % Set default parameters
            obj.setDefaultParams;
        end

        function trigger(obj, channels)
            % Starts the pulse trains of output channels: one channel number, e.g. P.trigger(1), or several as an
            % array, e.g. P.trigger([1 3 4]). The channels start in the same timer cycle. A channel that is already
            % playing a pulse train ignores the trigger.
            obj.port.write([obj.OpMenuByte 77 obj.channelBits(channels)], 'uint8');
        end

        function stop(obj, channels)
            % Stops pulse trains: P.stop() stops all output channels, and P.stop([1 3 4]) stops channels 1, 3 and 4
            % (firmware v22 or newer). The stopped channels return to their resting voltage. A soft trigger that has
            % not started its channel yet is cancelled too.
            if nargin < 2
                bitCode = 15; % All channels
            else
                bitCode = obj.channelBits(channels);
                if obj.firmwareVersion < 22
                    error('stop() cannot address individual channels prior to firmware v22')
                end
            end
            if obj.firmwareVersion < 22
                obj.port.write([obj.OpMenuByte 80], 'uint8');
            else
                obj.port.write([obj.OpMenuByte 98 bitCode], 'uint8');
            end
        end

        function syncToDevice(obj)
            % Sends every parameter to the device in one command. Use it after assignments made with autoSync off; with
            % autoSync on, they have already reached the device.
            % On Pulse Pal 3, if either trigger channel is in param sync mode (triggerMode 'Param Sync'), the device
            % stores the parameters instead of programming them, and loads them on the next rising edge of that
            % channel. It still checks every value at once: an error is raised if one is out of range. See "Trigger
            % modes" in help PulsePalDevice.
            obj.syncAllParams;
        end

        function syncFromDevice(obj)
            % Reads every parameter from the device into the properties, e.g. after they were changed with the
            % joystick or by loading a settings file. Requires firmware v22 or newer. continuousLoop is left as it
            % is: the device does not report it.
            obj.importCurrentParamsFromPulsePal;
        end

        function setFixedVoltage(obj, channels, voltage)
            % Holds output channels at a fixed voltage until they are set again or a pulse train is triggered on them:
            % one channel number, e.g. P.setFixedVoltage(4, 2.5), or several as an array, e.g.
            % P.setFixedVoltage([1 3], -1). voltage: in volts, -10 to 10, for every channel given.
            channelList = obj.channelNumbers(channels);
            if ~isnumeric(voltage) || ~isscalar(voltage) || ~isreal(voltage) || ~(voltage >= -10 && voltage <= 10)
                error('setFixedVoltage() takes one voltage, from -10 to 10 V, which is set on every channel given.')
            end
            voltageBits = obj.volts2Bits(voltage);
            for channel = channelList
                obj.port.write([obj.OpMenuByte 79 channel typecast(uint16(voltageBits), 'uint8')], 'uint8');
                obj.confirmWrite;
            end
        end

        function setCalibration(obj, channel, voltageOffset)
            % Sets an output channel's zero code calibration: an offset, in volts from -0.1 to 0.1, that corrects the
            % DAC's offset error, e.g. P.setCalibration(2, -0.003). The device stores it in its EEPROM and applies it
            % after every power cycle, so it only needs to be set once. Requires Pulse Pal 3 (hardware v3 or newer).
            if obj.hardwareVersion < 3
                error('setCalibration() requires hardware v3 or newer.')
            end
            if ~isnumeric(channel) || ~isscalar(channel) || ~ismember(channel, [1 2 3 4])
                error('channel must be 1, 2, 3 or 4')
            end
            if ~isnumeric(voltageOffset) || ~isscalar(voltageOffset) || ...
                    ~(voltageOffset >= -0.1 && voltageOffset <= 0.1) % Also refuses NaN
                error('voltageOffset for zero code calibration must be in range [-0.1, 0.1]')
            end
            voltageBits = obj.roundHalfEven(voltageOffset*(1/(20/65536)));
            obj.port.write([obj.OpMenuByte 96 channel-1 typecast(int16(voltageBits), 'uint8')], 'uint8');
            obj.confirmWrite;
            disp(['Zero code calibration set to ' num2str(voltageOffset) ' on channel ' num2str(channel) '.'])
        end

        function setScreenSaver(obj, enabled, timeout)
            % Switches the device's screen saver on (enabled = true) or off (enabled = false), and sets its timeout in
            % seconds (a whole number, 1-65535). With the screen saver on, the device dims its screen once it has been
            % left alone for the timeout: no command from the computer, no rising edge on a trigger channel, and no
            % joystick click or push. The next of these brings the screen back. Both settings are stored in the device's
            % EEPROM and kept through power cycles; the screen saver can also be switched on and off from the device's
            % joystick menu. A new device has it on, with 1800 s. The timeout is sent with every call, so leaving it out
            % sets 1800 s.
            % The device saves the settings once no channel is playing. Saving pauses its timer, usually for about
            % 20 us but, once in about 2000 changes, for tens of milliseconds, which would delay a trigger. Change the
            % settings before an experiment rather than during one.
            % Requires firmware v22 or newer. Only Pulse Pal 3 has a screen saver: Pulse Pal 2 accepts enabled = false
            % only.
            % Example: P.setScreenSaver(true, 300) dims the screen after 5 minutes without activity.
            if nargin < 3
                timeout = 1800;
            end
            if obj.firmwareVersion < 22
                error(['setScreenSaver() requires firmware v22 or newer. Detected firmware is: v' num2str(obj.firmwareVersion)])
            end
            if ~(isnumeric(enabled) || islogical(enabled)) || ~isscalar(enabled) || ~(enabled == 0 || enabled == 1)
                error('enabled must be true (on) or false (off)') % NaN too: it fails both comparisons
            end
            if ~isnumeric(timeout) || ~isscalar(timeout) || ~(timeout >= 1 && timeout <= 65535) || timeout ~= round(timeout)
                error('timeout must be a whole number of seconds from 1 to 65535')
            end
            if enabled == 1 && obj.hardwareVersion < 3
                error('The screen saver requires hardware v3 or newer: Pulse Pal 2 accepts enabled = false only.')
            end
            obj.port.write([obj.OpMenuByte 99 double(enabled) typecast(uint16(timeout), 'uint8')], 'uint8');
            obj.confirmWrite;
        end

        function sendCustomPulseTrain(obj, customTrainID, pulseTimes, voltages)
            % Loads a custom pulse train onto the device: a list of pulse onset times and a voltage for each pulse.
            % customTrainID: the train's number, 1-4 on Pulse Pal 3, 1-2 on Pulse Pal 2.
            % pulseTimes: in seconds from the start of the train, increasing, in multiples of 100 us
            %             (info.minPulseWidth_us), up to 9999.9999 s (info.maxTime).
            % voltages: one per pulse, in volts, -10 to 10.
            % An output channel plays the train when its customTrainID property is set to the train's number. Each
            % pulse takes the channel's own phase durations; a biphasic pulse's second phase is its voltage with the
            % sign reversed. Example:
            %   P.sendCustomPulseTrain(1, [0 0.1 0.25 0.5], [5 2.5 -2.5 -5]);
            %   P.customTrainID(2) = 1;
            sendCustomTrain(obj, customTrainID, pulseTimes, voltages);
        end

        function sendCustomWaveform(obj, customTrainID, samplingPeriod, voltages)
            % Loads a sampled waveform onto the device, as a custom pulse train of adjoining pulses: one per sample,
            % samplingPeriod seconds apart (a multiple of 100 us). Set phase1Duration to samplingPeriod on the output
            % channel that plays it, so that each sample lasts until the next. customTrainID: the train's number, 1-4
            % on Pulse Pal 3, 1-2 on Pulse Pal 2. voltages: in volts, -10 to 10.
            nVoltages = length(voltages);
            if ~isnumeric(samplingPeriod) || ~isscalar(samplingPeriod) || ~isfinite(samplingPeriod) || samplingPeriod <= 0
                error('Error: the sampling period must be a positive number of seconds.');
            end
            if rem(obj.roundHalfEven(double(samplingPeriod)*1000000), obj.cyclePeriod*2) > 0
                error(['Error: sampling period must be a multiple of ' num2str(obj.cyclePeriod*2) ' microseconds.']);
            end
            pulseTimes = (0:nVoltages-1)*double(samplingPeriod); % One per sample
            sendCustomTrain(obj, customTrainID, pulseTimes, voltages);
        end

        function setDefaultParams(obj)
            % Programs the default parameters: on all four output channels, monophasic 5 V pulses of 1 ms, 10 ms
            % apart, for 1 second, resting at 0 V and linked to trigger channel 1; both trigger channels in normal
            % mode. They are sent at once, also while autoSync is off. The constructor calls it.
            if obj.hardwareVersion > 2
                % A device left in param sync mode would store the sync below instead of running it,
                % leaving the device on its old program until a TTL arrived. Assigning triggerMode is
                % not deferred that way, so take both trigger channels out of param sync mode first.
                % See "Trigger modes" in help PulsePalDevice.
                autoSyncState = obj.autoSync;
                cleanup = onCleanup(@() obj.restoreAutoSync(autoSyncState));
                obj.autoSync = true; % Assigning a parameter reaches the device at once only while autoSync is on
                obj.triggerMode = {'Normal', 'Normal'};
                clear cleanup
            end
            obj.importParams(obj.defaultParams);
        end

        function params = exportParams(obj)
            % Returns every parameter as a struct, with one field per parameter (info.outputParameterNames, then
            % triggerMode) holding one value per channel, e.g. to save with your data and record exactly what the
            % device played. importParams() programs the device with it again. Example:
            %   params = P.exportParams();
            %   save('Trial12.mat', 'params');           % Or P.saveParameters('Trial12.json'), for a text file
            % The Python class's export_params() returns the same, with snake_case names.
            params = struct;
            for i = 1:numel(obj.ParamNames)
                params.(obj.ParamNames{i}) = obj.(obj.ParamNames{i});
            end
            params.triggerMode = obj.triggerMode;
        end

        function importParams(obj, params)
            % Programs the device with parameters exported by exportParams(): a struct with one field per parameter,
            % each holding one value per channel. All of them are checked first, and then sent in one command, as
            % syncToDevice() sends them: in param sync mode, the device stores them for the next sync edge. Parameters
            % missing from params keep their values, and autoSync is left as it is. Fields that are not parameters of
            % this class, e.g. from a newer version's exportParams(), are skipped with a warning. Raises an error, and
            % changes and sends nothing, if a value is invalid.
            if ~isstruct(params) || ~isscalar(params)
                error('importParams() takes a struct of parameters, as exportParams() returns.')
            end
            if isfield(params, 'playbackMode') && ~isfield(params, 'continuousLoop')
                params.continuousLoop = params.playbackMode; % The parameter's name before continuousLoop
            end
            % Files saved by earlier versions of saveParameters() also hold autoSync, which is not a parameter
            names = setdiff(fieldnames(params), {'playbackMode', 'autoSync'}, 'stable');
            unknown = setdiff(names, [obj.ParamNames {'triggerMode'}], 'stable');
            if ~isempty(unknown)
                warning('PulsePalDevice:unknownParams', ['importParams(): skipped ' strjoin(unknown, ', ') ...
                        ', which this version of PulsePalDevice does not have.'])
                names = setdiff(names, unknown, 'stable');
            end
            snapshot = obj.exportParams();
            autoSyncState = obj.autoSync;
            cleanup = onCleanup(@() obj.restoreAutoSync(autoSyncState));
            obj.autoSync = false;
            try
                for i = 1:numel(names)
                    obj.(names{i}) = params.(names{i});
                end
                obj.checkSyncParams();
            catch err
                % Nothing has been sent, so the properties go back to what the device holds. They are stored as they
                % were, unchecked: values read from the device can be ones an assignment refuses (see syncFromDevice())
                obj.storing = true;
                restoring = onCleanup(@() obj.endStoring());
                snapshotNames = fieldnames(snapshot);
                for i = 1:numel(snapshotNames)
                    obj.(snapshotNames{i}) = snapshot.(snapshotNames{i});
                end
                clear restoring
                rethrow(err)
            end
            obj.syncAllParams;
            clear cleanup
        end

        function saveParameters(obj, fileName)
            % Saves the parameters to a program file on this computer, e.g. P.saveParameters('MyProgram.json'): the
            % .json file both GUIs save and open (see "Program files" in /USING.md). Its params are exportParams()
            % with the Python class's snake_case names, which its import_params() takes as they are.
            % loadParameters() programs the device with them again.
            if ~(ischar(fileName) || (isstring(fileName) && isscalar(fileName))) || ~endsWith(lower(fileName), '.json')
                error('The file to save must be a .json file, e.g. P.saveParameters(''MyProgram.json'')')
            end
            obj.writeProgramFile(char(fileName), obj.exportParams(), {}, {});
        end

        function loadParameters(obj, fileName)
            % Programs the device with the parameters in a program file (see importParams): a .json file saved by
            % saveParameters() or by either GUI, or a .mat file saved by saveParameters() before it saved .json files.
            % Parameters the file leaves out keep their values, and autoSync is left as it is. The custom trains in a
            % GUI's program are not loaded: open it with gui() for those.
            if ~(ischar(fileName) || (isstring(fileName) && isscalar(fileName)))
                error('loadParameters() takes the name of a .json file, e.g. P.loadParameters(''MyProgram.json'')')
            end
            fileName = char(fileName);
            if endsWith(lower(fileName), '.mat')
                S = load(fileName);
                if ~isfield(S, 'params')
                    error([fileName ' holds no parameters: saveParameters() saved them as the variable params.'])
                end
                obj.importParams(S.params);
                return
            end
            [params, timestamps, voltages] = obj.readProgramFile(fileName);
            if any(~cellfun(@isempty, [timestamps voltages]))
                warning('PulsePalDevice:customTrainsNotLoaded', ['loadParameters() loads the parameters only: ' ...
                        'open ' fileName ' with gui() to load its custom trains too.'])
            end
            obj.importParams(params);
        end

        function saveSettingsFile(obj, fileName)
            % Saves the parameters to a settings file on the device's microSD card, e.g.
            % P.saveSettingsFile('MyProgram.pps'). fileName: 1 to 11 ASCII characters followed by .pps. A file of the
            % same name is replaced. loadSettingsFile() and the device's joystick menu load it. To keep the parameters
            % on this computer instead, see exportParams().
            obj.settingsFileOp(fileName, 1, 'saveSettingsFile()');
        end

        function loadSettingsFile(obj, fileName)
            % Loads a settings file from the device's microSD card, e.g. P.loadSettingsFile('MyProgram.pps'). The
            % device plays the program in it, and the parameters are read back into the properties (see
            % syncFromDevice). If the load fails, the device loads its own default parameters (not the ones
            % setDefaultParams() sets), the properties are updated to match, and an error is raised.
            obj.settingsFileOp(fileName, 2, 'loadSettingsFile()');
        end

        function deleteSettingsFile(obj, fileName)
            % Deletes a settings file from the device's microSD card, e.g. P.deleteSettingsFile('MyProgram.pps').
            obj.settingsFileOp(fileName, 3, 'deleteSettingsFile()');
        end

        function formatted = formatMicroSD(obj, varargin)
            % Formats the device's microSD card (Pulse Pal 3), which erases every settings file on it, and programs
            % the default parameters. By default it asks for confirmation at the command prompt first. A script that
            % runs on its own, or an AI agent, formats without asking:
            %   P.formatMicroSD('Confirm', false);
            % Returns true once the card is formatted, or false if the answer at the prompt was not y.
            confirm = true;
            if ~isempty(varargin)
                if numel(varargin) ~= 2 || ~(ischar(varargin{1}) || isstring(varargin{1})) || ...
                        ~strcmpi(varargin{1}, 'Confirm')
                    error('formatMicroSD() takes one option: formatMicroSD(''Confirm'', false) formats without asking.')
                end
                confirm = varargin{2};
                if ~(islogical(confirm) || isnumeric(confirm)) || ~isscalar(confirm) || ~(confirm == 0 || confirm == 1)
                    error('Confirm must be true or false.')
                end
            end
            if obj.hardwareVersion < 3
                error('formatMicroSD() requires hardware v3 or newer.')
            end
            formatted = false;
            if confirm
                disp('*** Pulse Pal microSD Formatter ***')
                disp('This will format Pulse Pal''s microSD card,')
                disp('erase all settings files on the device')
                disp('and reset all parameters to defaults.')
                reply = input('Do you want to continue (y/n) > ', 's');
                if ~strcmpi(strtrim(reply), 'y')
                    disp('Choice confirmed - microSD Card NOT formatted.')
                    return
                end
            end
            obj.port.write([obj.OpMenuByte 97], 'uint8');
            % The device replies with lines of status text, the last of which contains '!', and then a confirm byte (1
            % if the card was formatted, 0 if not). The confirm byte is sent after the device has reloaded its default
            % parameters, so it can arrive well after the text. It must be read here: left in the buffer, it would be
            % taken as the reply to the next command (the one sent by setDefaultParams() below), and every reply after
            % that would be read one byte late.
            startTime = tic;
            msg = [];
            lineEnd = [];
            replyComplete = false;
            while toc(startTime) < 30 && ~replyComplete
                if obj.port.NumBytesAvailable > 0
                    msg = [msg obj.port.read(obj.port.NumBytesAvailable, 'uint8')]; %#ok<AGROW>
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
            formatted = true;
        end

        function set.isBiphasic(obj, val)
            obj.isBiphasic = obj.setOutputParam(1, val);
        end

        function set.phase1Voltage(obj, val)
            obj.phase1Voltage = obj.setOutputParam(2, val);
        end

        function set.phase2Voltage(obj, val)
            obj.phase2Voltage = obj.setOutputParam(3, val);
        end

        function set.phase1Duration(obj, val)
            obj.phase1Duration = obj.setOutputParam(4, val);
        end

        function set.interPhaseInterval(obj, val)
            obj.interPhaseInterval = obj.setOutputParam(5, val);
        end

        function set.phase2Duration(obj, val)
            obj.phase2Duration = obj.setOutputParam(6, val);
        end

        function set.interPulseInterval(obj, val)
            obj.interPulseInterval = obj.setOutputParam(7, val);
        end

        function set.burstDuration(obj, val)
            obj.burstDuration = obj.setOutputParam(8, val);
        end

        function set.interBurstInterval(obj, val)
            obj.interBurstInterval = obj.setOutputParam(9, val);
        end

        function set.pulseTrainDuration(obj, val)
            obj.pulseTrainDuration = obj.setOutputParam(10, val);
        end

        function set.pulseTrainDelay(obj, val)
            obj.pulseTrainDelay = obj.setOutputParam(11, val);
        end

        function set.linkTriggerChannel1(obj, val)
            obj.linkTriggerChannel1 = obj.setOutputParam(12, val);
        end

        function set.linkTriggerChannel2(obj, val)
            obj.linkTriggerChannel2 = obj.setOutputParam(13, val);
        end

        function set.customTrainID(obj, val)
            obj.customTrainID = obj.setOutputParam(14, val);
        end

        function set.customTrainTarget(obj, val)
            obj.customTrainTarget = obj.setOutputParam(15, val);
        end

        function set.customTrainLoop(obj, val)
            obj.customTrainLoop = obj.setOutputParam(16, val);
        end

        function set.restingVoltage(obj, val)
            obj.restingVoltage = obj.setOutputParam(17, val);
        end

        function set.continuousLoop(obj, val)
            obj.continuousLoop = obj.setOutputParam(18, val);
        end

        function set.triggerMode(obj, val)
            obj.triggerMode = obj.setOutputParam(128, val); % 128: TriggerModeCode
        end

        function set.autoSync(obj, val)
            if ~(islogical(val) || isnumeric(val)) || ~isscalar(val) || ~(val == 0 || val == 1)
                error('autoSync must be true or false, e.g. P.autoSync = true;')
            end
            obj.autoSync = logical(val);
        end

        function delete(obj)
            % Runs on clear P or delete(P). Tells the device that MATLAB is disconnecting, which stops all output
            % channels and puts the device's own name back on its screen, closes the GUI, and releases the serial
            % port. Errors are ignored: the device may already be unplugged.
            try
                obj.port.write([obj.OpMenuByte 81], 'uint8');
            catch
                % Fail silently
            end
            try
                close(obj.ui.Figure)
            catch
                % Fail silently
            end
            obj.port = [];
        end
    end

    methods (Access = private)
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

        function modes = availableTriggerModes(obj)
            % The trigger modes of the connected device: param sync mode is on Pulse Pal 3, with firmware v22 or newer
            modes = obj.TriggerModeNames;
            if obj.hardwareVersion < 3 || obj.firmwareVersion < 22
                modes = modes(1:3);
            end
        end

        function restoreAutoSync(obj, state)
            % For setDefaultParams(): puts autoSync back, also if programming the defaults failed
            obj.autoSync = state;
        end

        function [value, deviceValues] = checkOutputParam(obj, paramCode, val)
            % Checks a parameter's new value on every channel (1x4, or 1x2 for triggerMode), and returns it as the
            % property holds it, and as the device reads it: DAC codes for voltages, timer cycles for times, and
            % codes for the rest. A time is held as the device plays it, rounded to the timer cycle. Raises an error
            % for a value the device cannot play, and for a single value: it does not say which channels it is meant
            % for (see "One way to use all six" in /AGENTS.md).
            if paramCode == obj.TriggerModeCode
                modes = obj.availableTriggerModes();
                [~, codes] = ismember(modes, obj.TriggerModeNames);
                deviceValues = obj.namesToCodes(val, modes, codes - 1, 2, 'triggerMode', 'trigger channel');
                value = obj.TriggerModeNames(deviceValues + 1);
                return
            end
            name = obj.ParamNames{paramCode};
            switch obj.ParamKinds{paramCode}
                case 'Target'
                    deviceValues = obj.namesToCodes(val, obj.CustomTrainTargetNames, [0 1], 4, name, 'output channel');
                    value = obj.CustomTrainTargetNames(deviceValues + 1);
                    return
                case 'Logical'
                    val = obj.oneValuePerChannel(val, 4, name, 'output channel');
                    if ~(islogical(val) || isnumeric(val)) || any(val ~= 0 & val ~= 1) % Also refuses NaN
                        error([name ' values must be true or false (1 or 0).'])
                    end
                    value = logical(val);
                    deviceValues = double(value);
                    if paramCode == 18 && any(value) && obj.firmwareVersion < 22
                        error('continuousLoop requires firmware v22 or newer.')
                    end
                    return
            end
            val = obj.oneValuePerChannel(val, 4, name, 'output channel');
            % NaN fails every comparison, so the range checks below would let it through, and it would reach the device
            % as 0: -10 V, or a phase of 0 cycles
            if ~(isnumeric(val) || islogical(val)) || ~isreal(val) || ~all(isfinite(double(val)))
                error([name ' must be a number (NaN and Inf are not allowed).']);
            end
            value = double(val);
            switch obj.ParamKinds{paramCode}
                case 'Volts'
                    if any(value < -10 | value > 10)
                        error([name ' was out of range: -10 to 10 V']);
                    end
                    deviceValues = obj.volts2Bits(value);
                case 'TrainID'
                    if any(value ~= round(value)) || any(value < 0 | value > obj.nCustomPulseTrains)
                        error([name ' must be a whole number from 0 to ' num2str(obj.nCustomPulseTrains) '.']);
                    end
                    deviceValues = value;
                otherwise % 'Time' and 'PulseTime'
                    % Checked in whole timer cycles, as the device receives it: [100 100 100 100]*1e-6 is
                    % 9.999999999999999e-05 in floating point, under the 0.0001 minimum, but is exactly 2 cycles.
                    deviceValues = obj.roundHalfEven(value*obj.cycleFrequency);
                    lowestCycles = 0;
                    if strcmp(obj.ParamKinds{paramCode}, 'PulseTime')
                        lowestCycles = 2;
                    end
                    if any(deviceValues < lowestCycles) || any(deviceValues > obj.maxTimeCycles())
                        error([name ' was out of range: ' num2str(lowestCycles/obj.cycleFrequency) ' to '...
                               num2str(obj.MaxTime, 8) ' s']);
                    end
                    value = deviceValues/obj.cycleFrequency; % The time the device plays
            end
        end

        function value = setOutputParam(obj, paramCode, val)
            % Checks a parameter's new value (see checkOutputParam()), and with autoSync on, programs it on the device.
            % Returns the value as the property holds it. Values read from the device are stored as they are.
            if obj.storing
                value = val;
                return
            end
            [value, deviceValues] = obj.checkOutputParam(paramCode, val);
            if ~obj.autoSync
                return
            end
            kind = 'Byte';
            if paramCode < obj.TriggerModeCode
                kind = obj.ParamKinds{paramCode};
            end
            switch kind
                case 'Volts'
                    bytes = @(v) typecast(uint16(v), 'uint8');
                case {'Time', 'PulseTime'}
                    bytes = @(v) typecast(uint32(v), 'uint8');
                otherwise
                    bytes = @(v) uint8(v);
            end
            if obj.firmwareVersion > 21
                obj.port.write([obj.OpMenuByte 91 paramCode bytes(deviceValues)], 'uint8'); % All channels at once
                obj.confirmWrite;
            elseif paramCode ~= 18 % Firmware v21 has no parameter 18, and checkOutputParam() lets only false through
                for i = 1:numel(deviceValues)
                    obj.port.write([obj.OpMenuByte 74 paramCode i bytes(deviceValues(i))], 'uint8');
                    obj.confirmWrite; % Firmware v21 confirms each op 74
                end
            end
        end

        function codes = namesToCodes(obj, names, validNames, validCodes, nChannels, settingName, channelType)
            % Converts one name per channel to codes: validCodes(i) is validNames{i}'s code. The codes themselves are
            % accepted too, as numbers, as Pulse Pal's clients used them before names.
            if isnumeric(names) && ~isempty(names)
                names = obj.oneValuePerChannel(names, nChannels, settingName, channelType);
                if ~all(ismember(names, validCodes))
                    error([settingName ' takes one name per ' channelType ' (or its code). Valid names are: '...
                           strjoin(validNames, ', ') '.'])
                end
                codes = double(names);
                return
            end
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
                elseif isnumeric(names{i}) && isscalar(names{i})
                    match = find(validCodes == names{i});
                end
                if isempty(match)
                    error(['Unknown ' settingName ' for ' channelType ' ' num2str(i) '. Valid names are: '...
                           strjoin(validNames, ', ') '.'])
                end
                codes(i) = validCodes(match);
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
                       ' by its number, e.g. P.' name '{1} = ' example ', or all ' num2str(nChannels) ', e.g. P.' ...
                       name '(:) = {' example '}.'])
            elseif isscalar(values)
                example = num2str(values);
                error([name ' holds one value per ' channelType ', so a single value is ambiguous. Set one ' channelType ...
                       ' by its number, e.g. P.' name '(1) = ' example ', or all ' num2str(nChannels) ', e.g. P.' ...
                       name '(:) = ' example '.'])
            end
            error([name ' needs one value per ' channelType ' (1x' num2str(nChannels) ').'])
        end

        function channelList = channelNumbers(~, channels)
            % Checks output channel numbers: one, or several as an array
            if ~isnumeric(channels) || ~isreal(channels) || isempty(channels) || ~all(ismember(channels(:), 1:4))
                error('Output channels are numbered 1-4: give one, or several as an array, e.g. 1 or [2 4].')
            end
            channelList = unique(double(channels(:)))';
        end

        function bits = channelBits(obj, channels)
            % Converts output channel numbers to one bit per channel (bit 0 = channel 1)
            bits = uint8(sum(bitshift(1, obj.channelNumbers(channels) - 1)));
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
            % Converts serialized 16-bit DAC codes to volts. A code within one step (305 uV) of a value with 3 decimals,
            % such as 5 or 4.255, gives that value, as in the Python class: the code for 5 V plays 4.99992 V.
            voltageBits = typecast(uint8(bytes), 'uint16');
            volts = ((double(voltageBits) ./ 65535) .* 20) - 10;
            clean = round(volts, 3);
            near = abs(volts - clean) <= 20/65535;
            volts(near) = clean(near);
            volts(~near) = round(volts(~near), 4);
        end

        function seconds = bytes2Seconds(obj, Bytes)
            % Convert serialized hardware timer counts to seconds.
            seconds = double(typecast(uint8(Bytes), 'uint32'))/obj.cycleFrequency;
        end

        function cycles = maxTimeCycles(obj)
            % MaxTime in timer cycles: 199999998 on a 50 us timer
            cycles = obj.roundHalfEven(obj.MaxTime*obj.cycleFrequency);
        end

        function confirmed = confirmWrite(obj)
            % Verify that the device acknowledged a write command.
            confirmed = obj.port.read(1, 'uint8');
            % read() returns nothing (with a warning) if the byte does not arrive, and "if [] ~= 1" is false, so a
            % missing confirm byte has to be checked for: left unchecked, it arrives late and is read as the next reply
            if isempty(confirmed) || confirmed ~= 1
                error('Error: Pulse Pal did not return an expected byte to confirm the operation.');
            end
        end

        function checkSyncParams(obj)
            % The checks on the whole parameter set that assigning one parameter cannot make
            for i = 1:4
                if strcmp(obj.customTrainTarget{i}, 'Bursts') && obj.burstDuration(i) == 0
                    error(['Error in output channel ' num2str(i)...
                        ': When custom train times target burst onsets, a non-zero burst duration must be defined.'])
                end
            end
        end

        function syncAllParams(obj)
            %   Encode and transmit the complete parameter set in one command: op 92, or op 73 on firmware v21. This is
            %   more efficient than item-wise data transfers. See "Op codes" in /Firmware/PROTOCOL.md.
            obj.checkSyncParams();
            [~, targets] = ismember(obj.customTrainTarget, obj.CustomTrainTargetNames);
            [~, modes] = ismember(obj.triggerMode, obj.TriggerModeNames);
            TimeData = obj.roundHalfEven([obj.phase1Duration; obj.interPhaseInterval; obj.phase2Duration;...
                obj.interPulseInterval; obj.burstDuration; obj.interBurstInterval;...
                obj.pulseTrainDuration; obj.pulseTrainDelay]*obj.cycleFrequency);
            TimeData = TimeData';
            VoltageData = [obj.volts2Bits(obj.phase1Voltage); obj.volts2Bits(obj.phase2Voltage); obj.volts2Bits(obj.restingVoltage)];
            VoltageData = VoltageData';
            continuousLoopData = [];
            if obj.firmwareVersion > 21
                continuousLoopData = double(obj.continuousLoop);
            end
            SingleByteOutputParams = [double(obj.isBiphasic); obj.customTrainID; targets - 1;...
                double(obj.customTrainLoop); continuousLoopData];
            opCode = 92;
            if obj.firmwareVersion < 22 % Use op 73 for firmware v21
                opCode = 73;
                TimeData = TimeData';
                VoltageData = VoltageData';
            else
                SingleByteOutputParams = SingleByteOutputParams';
            end
            SingleByteParams = [SingleByteOutputParams(1:end) double(obj.linkTriggerChannel1)...
                double(obj.linkTriggerChannel2) modes - 1];
            obj.port.write([obj.OpMenuByte opCode typecast(uint32(TimeData(1:end)), 'uint8') ...
                typecast(uint16(VoltageData(1:end)), 'uint8') SingleByteParams], 'uint8');
            obj.confirmWrite;
        end

        function sendCustomTrain(obj, trainID, pulseTimes, voltages)
            %   Validate and transmit a custom train of pulses to Pulse Pal.
            if length(pulseTimes) ~= length(voltages)
                error('There must be one voltage value for every timestamp');
            end
            nPulses = length(pulseTimes);
            if ~isnumeric(pulseTimes) || ~isnumeric(voltages) || ~all(isfinite(double(pulseTimes(:)))) || ...
                    ~all(isfinite(double(voltages(:))))
                error('Error: custom pulse times and voltages must be numbers (NaN and Inf are not allowed).');
            end
            if nPulses > obj.maxCustomPulses
                error(['Error: Attempted to send ' num2str(nPulses) ' pulses. Pulse Pal '...
                    num2str(obj.info.hardwareVersion) ' can only store '...
                    num2str(obj.maxCustomPulses) ' pulses per custom pulse train.']);
            end
            % Times are refused, not rounded, between two steps of info.minPulseWidth_us (100 us), as in the Python
            % class, so that a train plays as written. Checked to the nearest microsecond.
            microseconds = obj.roundHalfEven(double(pulseTimes(:)')*1000000);
            if any(rem(microseconds, obj.cyclePeriod*2) ~= 0)
                error(['Custom pulse times must be multiples of ' num2str(obj.cyclePeriod*2) ' microseconds.']);
            end
            if any(microseconds < 0)
                error('Error: Custom pulse times must be positive');
            end
            CandidateTimes = microseconds/obj.cyclePeriod; % Timer cycles
            % The device plays each pulse until the next one's time, so a time that is not later than the one
            % before it would freeze the output for the rest of the train
            if any(diff(CandidateTimes) <= 0)
                error('Error: Custom pulse times must always increase');
            end
            if nPulses > 0 && CandidateTimes(end) > obj.maxTimeCycles()
                error(['Error: Custom pulse times must be at most ' num2str(obj.MaxTime, 8) ' s']);
            end
            if any(abs(double(voltages(:))) > 10)
                error('Error: Custom voltage range = -10V to +10V');
            end
            VoltageOutput = obj.volts2Bits(voltages(:)');
            if ~isnumeric(trainID) || ~isscalar(trainID) || ~ismember(trainID, 1:obj.nCustomPulseTrains)
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
            obj.port.write([obj.OpMenuByte opCode trainCode typecast(uint32([nPulses CandidateTimes]), 'uint8') ...
                typecast(uint16(VoltageOutput), 'uint8')], 'uint8');
            obj.confirmWrite;
        end

        function settingsFileOp(obj, fileName, opByte, context)
            % Saves (opByte 1), loads (2) or deletes (3) a settings file on the microSD card, with op 90
            if isstring(fileName) && isscalar(fileName)
                fileName = char(fileName);
            end
            if ~ischar(fileName) || ~isrow(fileName) || any(fileName < 32 | fileName > 126) || ...
                    numel(fileName) < 5 || numel(fileName) > obj.MaxSettingsFileNameLength || ...
                    ~endsWith(lower(fileName), '.pps')
                error([context ': the file name must be 1 to 11 ASCII characters followed by .pps, e.g. '...
                       '''Protocol1.pps''.'])
            end
            obj.port.write([obj.OpMenuByte 90 opByte numel(fileName) double(fileName)], 'uint8');
            confirmed = 1;
            if obj.firmwareVersion > 21
                confirmed = obj.port.read(1, 'uint8'); % Sent after the file operation has finished
            elseif opByte == 2
                pause(.1); % Firmware v21 does not acknowledge, so allow time for the load
            end
            if opByte == 2
                % Read the parameters back even if the load failed: the device then loads its own default
                % parameters (not the ones setDefaultParams() sets), and the properties must describe them.
                obj.importCurrentParamsFromPulsePal;
            end
            if isempty(confirmed) || confirmed ~= 1
                if opByte == 2
                    error(['Error: Pulse Pal could not load ' fileName '. It has loaded its default '...
                        'parameters instead, and the properties have been updated to match.']);
                else
                    error(['Error: Pulse Pal did not confirm ' context '.']);
                end
            end
        end

        function importCurrentParamsFromPulsePal(obj)
            %   Import all parameters currently stored on the device (op 93). Firmware v22 or newer is required.
            %   The values are stored as the device holds them, without the checks of an assignment, as the Python
            %   class stores them: the device can hold values that a client would refuse, set by an older client.
            %   continuousLoop is not part of op 93.
            if obj.firmwareVersion < 22
                error(['syncFromDevice() requires firmware v22 or newer.'...
                      newline 'Detected firmware is v' num2str(obj.firmwareVersion)])
            end
            obj.port.write([obj.OpMenuByte 93], 'uint8');
            Msg = obj.port.read(178, 'uint8');
            if length(Msg) ~= 178
                error(['Pulse Pal sent ' num2str(length(Msg)) ' of the 178 bytes of its parameters.'])
            end
            obj.storing = true;
            cleanup = onCleanup(@() obj.endStoring());
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
            obj.isBiphasic = Msg(Pos:Pos+3) > 0; Pos = Pos + 4;
            obj.customTrainID = double(Msg(Pos:Pos+3)); Pos = Pos + 4;
            obj.customTrainTarget = obj.CustomTrainTargetNames(min(Msg(Pos:Pos+3), 1) + 1); Pos = Pos + 4;
            obj.customTrainLoop = Msg(Pos:Pos+3) > 0; Pos = Pos + 4;
            obj.linkTriggerChannel1 = Msg(Pos:Pos+3) > 0; Pos = Pos + 4;
            obj.linkTriggerChannel2 = Msg(Pos:Pos+3) > 0; Pos = Pos + 4;
            obj.triggerMode = obj.TriggerModeNames(min(Msg(Pos:Pos+1), 3) + 1);
            clear cleanup
        end

        function endStoring(obj)
            % For importCurrentParamsFromPulsePal() and importParams(): assignments are checked again, also if storing
            % failed
            obj.storing = false;
        end

        function writeProgramFile(obj, fileName, params, timestamps, voltages)
            % Writes a program file (see saveParameters()), in the Python GUI's format: format_version, then params,
            % the custom trains' text and the device's info. params: parameters by their MATLAB names, in any form an
            % assignment takes (names or codes, logicals or 0 and 1), written as exportParams() holds them, with the
            % Python class's snake_case names. timestamps and voltages: the text of each custom train, as the GUI's
            % boxes hold it, or {} for none.
            program = struct;
            program.format_version = obj.ProgramFormatVersion;
            program.params = struct;
            names = fieldnames(params);
            for i = 1:numel(names)
                value = params.(names{i});
                kind = 'TriggerMode';
                if ~strcmp(names{i}, 'triggerMode')
                    kind = obj.ParamKinds{strcmp(obj.ParamNames, names{i})};
                end
                switch kind
                    case 'TriggerMode'
                        value = obj.modeNames(value, obj.TriggerModeNames);
                    case 'Target'
                        value = obj.modeNames(value, obj.CustomTrainTargetNames);
                    case 'Logical'
                        value = logical(value);
                    otherwise
                        value = double(value);
                end
                program.params.(obj.snakeCase(names{i})) = value;
            end
            if ~isempty(timestamps)
                program.custom_train_timestamps = timestamps;
                program.custom_train_voltages = voltages;
            end
            program.device_info = struct;
            infoNames = fieldnames(obj.info);
            for i = 1:numel(infoNames)
                value = obj.info.(infoNames{i});
                if strcmp(infoNames{i}, 'outputParameterNames')
                    value = cellfun(@PulsePalDevice.snakeCase, value, 'UniformOutput', false); % As Python names them
                end
                program.device_info.(obj.snakeCase(infoNames{i})) = value;
            end
            if verLessThan('matlab', '9.10') %#ok<VERLESSMATLAB> jsonencode's PrettyPrint is in R2021a and newer
                text = jsonencode(program);
            else
                text = jsonencode(program, 'PrettyPrint', true);
            end
            fileID = fopen(fileName, 'w', 'n', 'UTF-8');
            if fileID < 0
                error([fileName ' could not be opened for writing.'])
            end
            fwrite(fileID, text, 'char');
            fclose(fileID);
        end

        function [params, timestamps, voltages] = readProgramFile(obj, fileName)
            % Reads a program file (see saveParameters()). params: the parameters it holds, by their MATLAB names, as
            % the file holds them (names and logicals, or the GUIs' numbers in files from before format_version), each
            % as a row. timestamps and voltages: the text of each custom train, 1x4, '' for none. Keys that are not
            % parameters of this class are skipped with a warning.
            try
                program = jsondecode(fileread(fileName));
            catch err
                error(['Could not read ' fileName ': ' err.message])
            end
            if ~isstruct(program) || ~isscalar(program) || ~isfield(program, 'params') || ~isstruct(program.params)
                error([fileName ' is not a Pulse Pal program file: it has no params.'])
            end
            if isfield(program, 'format_version') && isnumeric(program.format_version) && ...
                    any(program.format_version > obj.ProgramFormatVersion)
                warning('PulsePalDevice:newerProgramFile', [fileName ' was saved in a newer format (format_version ' ...
                        num2str(program.format_version) '): what this version does not know is skipped.'])
            end
            fileParams = program.params;
            if ~isfield(fileParams, 'trigger_mode') && isfield(program, 'trigger_mode')
                fileParams.trigger_mode = program.trigger_mode; % Files from before format_version held it here
            end
            names = [obj.ParamNames {'triggerMode'}];
            keys = cellfun(@PulsePalDevice.snakeCase, names, 'UniformOutput', false);
            unknown = setdiff(fieldnames(fileParams), keys, 'stable');
            if ~isempty(unknown)
                warning('PulsePalDevice:unknownParams', [fileName ': skipped ' strjoin(unknown, ', ') ...
                        ', which this version of PulsePalDevice does not have.'])
            end
            params = struct;
            for i = 1:numel(names)
                if isfield(fileParams, keys{i})
                    params.(names{i}) = reshape(fileParams.(keys{i}), 1, []); % jsondecode returns columns
                end
            end
            timestamps = obj.customTrainTexts(program, 'custom_train_timestamps');
            voltages = obj.customTrainTexts(program, 'custom_train_voltages');
        end

        function texts = customTrainTexts(obj, program, key)
            % The text of each custom train in a program file: the text as typed in a GUI, or a list of numbers
            texts = repmat({''}, 1, 4);
            if ~isfield(program, key) || isempty(program.(key))
                return
            end
            values = program.(key);
            if ~iscell(values)
                values = num2cell(values, 2); % Lists of numbers of the same length: one row per train
            end
            for i = 1:min(numel(values), obj.info.nCustomPulseTrains)
                value = values{i};
                if ischar(value) || isstring(value)
                    texts{i} = char(value);
                elseif isnumeric(value) && ~isempty(value)
                    texts{i} = char(strjoin(string(value(:)'), ', '));
                end
            end
        end

        function names = modeNames(~, values, validNames)
            % A mode parameter's values as names, for a program file: the GUI keeps them as codes
            names = values;
            if isnumeric(values)
                names = validNames(double(values) + 1);
            end
            names = reshape(names, 1, []);
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
        function name = snakeCase(name)
            % A name as the Python class spells it: 'phase1Voltage' is 'phase1_voltage', 'customTrainID' is
            % 'custom_train_id'
            name = lower(regexprep(name, '([A-Z]+)', '_$1'));
        end

        function params = defaultParams
            % The default parameters, as the GUI keeps them: numbers, with each mode as its code (0 = 'Normal',
            % 'Pulses') and each on/off parameter as 0 or 1. setDefaultParams() programs them.
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
            params.customTrainID = zeros(1,4);
            params.customTrainTarget = zeros(1,4);
            params.customTrainLoop = zeros(1,4);
            params.continuousLoop = zeros(1,4);
            params.triggerMode = zeros(1,2);
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

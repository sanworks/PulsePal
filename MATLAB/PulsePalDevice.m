%{
----------------------------------------------------------------------------

This file is part of the Sanworks Pulse Pal repository
Copyright (C) 2025 Sanworks LLC, Rochester, New York, USA

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
    properties
        Port % Serial port
        autoSync = 'on'; % If 'on', changing parameter fields automatically updates PulsePal device. Otherwise, use 'sync' method.
        isBiphasic % See parameter descriptions at: https://sites.google.com/site/pulsepalwiki/parameter-guide
        phase1Voltage
        phase2Voltage
        restingVoltage
        phase1Duration
        interPhaseInterval
        phase2Duration
        interPulseInterval
        burstDuration
        interBurstInterval
        pulseTrainDuration
        pulseTrainDelay
        linkTriggerChannel1
        linkTriggerChannel2
        customTrainID
        customTrainTarget
        customTrainLoop
        playbackMode
        triggerMode
    end

    properties (Access = private)
        currentFirmwareVersion = 22; % Most recent firmware version
        opMenuByte = 213; % Byte code to access op menu
        firmwareVersion % Actual firmware version of connected device
        hardwareVersion % With newer firmware, a hardware version is explicitly returned
        cycleFrequency = 20000; % Update rate of Pulse Pal hardware timer
        autoSyncOn = true; % logical version of public property autoSync, to avoid strcmp
        maxCustomPulses % Maximum number of custom pulses supported
        rootPath = fileparts(which('PulsePalObject'));
        paramNames = {'isBiphasic' 'phase1Voltage' 'phase2Voltage' 'phase1Duration' 'interPhaseInterval' 'phase2Duration'...
            'interPulseInterval' 'burstDuration' 'interBurstInterval' 'pulseTrainDuration' 'pulseTrainDelay'...
            'linkTriggerChannel1' 'linkTriggerChannel2' 'customTrainID' 'customTrainTarget' 'customTrainLoop' 'restingVoltage' 'playbackMode'};
    end

    methods
        function obj = PulsePalDevice(varargin) % Constructor method, executed when creating the object
            % Check for minimum MATLAB version
            MinVer = '9.7';
            MinVerName = 'R2019b';
            if verLessThan('matlab', MinVer)
                error(['PulsePalObject requires MATLAB ' MinVerName ' or newer.'...
                    char(10) 'If you must use previous MATLAB versions, please consider using the legacy interface.'])
            end
            if nargin > 0
                portString = varargin{1};
            else
                PortList = obj.findSerialPorts();
                if ~isempty(PortList)
                    error(['You must call PulsePalObject with a serial port string argument, e.g. P = PulsePalObject(''COM3'')' newline 'Detected serial ports are: ' strjoin(PortList, ', ')])
                else
                    error('You must call PulsePalObject with a serial port string argument.')
                end
            end
            defaultBaudRate = 12000000;
            if isunix
                defaultBaudRate = 4000000;
            end
            obj.Port = serialport(portString, defaultBaudRate);
            setDTR(obj.Port, true);
            obj.Port.write([obj.opMenuByte 72], 'uint8');
            pause(.1);
            HandShakeOkByte = obj.Port.read(1, 'uint8');
            if HandShakeOkByte == 75
                obj.firmwareVersion = obj.Port.read(1, 'uint32');
                if obj.firmwareVersion < 20
                    obj.Port.close();
                    error('Error: Pulse Pal 1 detected. You must use the legacy API. Add /PulsePal/MATLAB to the MATLAB path, and at the command prompt, type PulsePal(''Port'') where ''Port'' is your serial port string.');
                else
                    if obj.firmwareVersion < obj.currentFirmwareVersion
                        obj.Port.close();
                        error('Error: Pulse Pal with old firmware detected. Please update your firmware. Update instructions are online at: https://sites.google.com/site/pulsepalwiki/updating-firmware');
                    end
                    if obj.firmwareVersion > obj.currentFirmwareVersion
                        obj.Port.close();
                        error(['Error: Pulse Pal with future firmware detected. Please update your MATLAB software or downgrade the firmware to v' num2str(obj.currentFirmwareVersion) '.']);
                    end
                end
                obj.Port.write([obj.opMenuByte 94], 'uint8'); % Request hardware info
                obj.hardwareVersion = obj.Port.read(1, 'uint8');
                obj.maxCustomPulses = obj.Port.read(1, 'uint32');
            else
                disp('Error: Pulse Pal returned an unexpected handshake signature.')
            end
            obj.Port.write([obj.opMenuByte 89 'MATLAB'], 'uint8');
            obj.setDefaultParams;
        end

        function trigger(obj, channels, varargin) % Soft-trigger output channels
            if ischar(channels)
                TriggerAddress = bin2dec(channels);
            else
                if nargin > 1
                    channels = [channels cell2mat(varargin)];
                end
                ChannelsBinary = zeros(1,4);
                ChannelsBinary(channels) = 1;
                TriggerAddress = sum(ChannelsBinary .* [1 2 4 8]);
            end
            obj.Port.write([obj.opMenuByte 77 TriggerAddress], 'uint8');
        end

        function stop(obj) % Stop all ongoing playback
            obj.Port.write([obj.opMenuByte 80], 'uint8');
        end

        function syncToDevice(obj) % If autoSync is off, this will sync all parameters at once.
            obj.syncAllParams;
        end

        function syncFromDevice(obj) % Read all parameters from device to the object.
            obj.importCurrentParamsFromPulsePal;
        end

        function setVoltage(obj, channel, voltage)
            % Sets a fixed output channel voltage. Channel = 1-4. Voltage = volts (-10 to +10)
            obj.checkParamRange(voltage, 'Volts');
            voltageBits = obj.volts2Bits(voltage);
            obj.Port.write([obj.opMenuByte 79 channel], 'uint8', voltageBits, 'uint16');
        end

        function sendCustomPulseTrain(obj, trainID, pulseTimes, voltages)
            % Sends a custom pulse train to the device. trainId = 1 or 2. pulseTimes = sec. voltages = volts.
            sendCustomTrain(obj, trainID, pulseTimes, voltages);
        end

        function sendCustomWaveform(obj, trainID, samplingPeriod, voltages)
            % Sends a custom waveform to the device. trainId = 1 or 2. samplingPeriod = sec. voltages = volts.
            nVoltages = length(voltages);
            if rem(round(samplingPeriod*1000000), 100) > 0
                error('Error: sampling period must be a multiple of 100 microseconds.');
            end
            pulseTimes = 0:samplingPeriod:((nVoltages*samplingPeriod)-(1*samplingPeriod));
            sendCustomTrain(obj, trainID, pulseTimes, voltages);
        end

        function setDefaultParams(obj)
            % Loads default parameters and sends them to the device
            autoSyncState = obj.autoSync;
            obj.autoSync = 'off';
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
            if autoSyncState
                obj.autoSync = 'on';
            end
        end

        function sdSettings(obj, settingsFileName, op)
            % Saves, loads or deletes settings. settingsFileName = full
            % path to settings file, incl. extension. op = 'save',
            % 'load', or 'delete'
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
            Message = [obj.opMenuByte 90 OpByte SettingsNameLength settingsFileName];
            obj.Port.write(Message, 'uint8');
            if OpByte == 2
                pause(.1);
                obj.importCurrentParamsFromPulsePal;
            end
        end

        function saveParameters(obj, filename)
            % Saves current parameters to a .mat file.
            if (~strcmp(filename(end-3:end), '.mat'))
                error('The file to save must be a .mat file')
            end
            Parameters = struct;
            Parameters.autoSync = obj.autoSync;
            Parameters.isBiphasic = obj.isBiphasic;
            Parameters.phase1Voltage = obj.phase1Voltage;
            Parameters.phase2Voltage = obj.phase2Voltage;
            Parameters.restingVoltage = obj.restingVoltage;
            Parameters.phase1Duration = obj.phase1Duration;
            Parameters.interPhaseInterval = obj.interPhaseInterval;
            Parameters.phase2Duration = obj.phase2Duration;
            Parameters.interPulseInterval = obj.interPulseInterval;
            Parameters.burstDuration = obj.burstDuration;
            Parameters.interBurstInterval = obj.interBurstInterval;
            Parameters.pulseTrainDuration = obj.pulseTrainDuration;
            Parameters.pulseTrainDelay = obj.pulseTrainDelay;
            Parameters.linkTriggerChannel1 = obj.linkTriggerChannel1;
            Parameters.linkTriggerChannel2 = obj.linkTriggerChannel2;
            Parameters.customTrainID = obj.customTrainID;
            Parameters.customTrainTarget = obj.customTrainTarget;
            Parameters.customTrainLoop = obj.customTrainLoop;
            Parameters.playbackMode = obj.playbackMode;
            Parameters.triggerMode = obj.triggerMode;
            save(filename, 'Parameters');
        end

        function loadParameters(obj, filename)
            % Loads parameters from a settings file previously saved with
            % the saveParameters method
            S = load(filename);
            Parameters = S.Parameters;
            obj.autoSync = 'off';
            obj.isBiphasic = Parameters.isBiphasic;
            obj.phase1Voltage = Parameters.phase1Voltage;
            obj.phase2Voltage = Parameters.phase2Voltage;
            obj.restingVoltage = Parameters.restingVoltage;
            obj.phase1Duration = Parameters.phase1Duration;
            obj.interPhaseInterval = Parameters.interPhaseInterval;
            obj.phase2Duration = Parameters.phase2Duration;
            obj.interPulseInterval = Parameters.interPulseInterval;
            obj.burstDuration = Parameters.burstDuration;
            obj.interBurstInterval = Parameters.interBurstInterval;
            obj.pulseTrainDuration = Parameters.pulseTrainDuration;
            obj.pulseTrainDelay = Parameters.pulseTrainDelay;
            obj.linkTriggerChannel1 = Parameters.linkTriggerChannel1;
            obj.linkTriggerChannel2 = Parameters.linkTriggerChannel2;
            obj.customTrainID = Parameters.customTrainID;
            obj.customTrainTarget = Parameters.customTrainTarget;
            obj.customTrainLoop = Parameters.customTrainLoop;
            obj.playbackMode = Parameters.playbackMode;
            obj.triggerMode = Parameters.triggerMode;
            obj.syncToDevice;
            obj.autoSync = Parameters.autoSync;
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
            switch val
                case 'off'
                    obj.autoSyncOn = false;
                case 'on'
                    obj.autoSyncOn = true;
                otherwise
                    error('autoSync must be either ''off'' or ''on''.');
            end
            obj.autoSync = val;
        end
    end

    methods (Access = private)
        function portStrings = findSerialPorts(obj) % If no COM port is specified, give the user a list of likely candidates
            portStrings = {}; % Initialize empty cell array
            if exist('serialportlist','file')
                portLocations = sort(serialportlist('available'));
            elseif exist('seriallist','file')
                portLocations = sort(seriallist('available'));
            else % Likely MATLAB pre r2017a. Fall back to system call.
                % Get and split the system's list of available ports
                if ispc
                    % For Windows: Use PowerShell command to list serial ports
                    [~, RawString] = system('powershell.exe -inputformat none "[System.IO.Ports.SerialPort]::getportnames()"');
                    portLocations = strsplit(RawString, {'\r\n', '\n', '\r'}); % Split the output by possible newline characters
                elseif ismac
                    % For macOS: List USB serial devices
                    [~, rawSerialPortList] = system('ls /dev/cu.usbmodem*');
                    portLocations = strsplit(strtrim(rawSerialPortList), '\n');
                else
                    % For Linux: List ACM serial devices
                    [~, rawSerialPortList] = system('ls /dev/ttyACM*');
                    portLocations = strsplit(strtrim(rawSerialPortList), {'  ', '\n'});
                end
            end

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

        function delete(obj)
            obj.Port.write([obj.opMenuByte 81], 'uint8');
        end

        function checkParamRange(obj, param, type, range, varargin)
            RangeLow = range(1);
            RangeHigh = range(2);
            if nargin > 4
                paramCode = varargin{1};
                if paramCode < 128
                    paramCodeString = obj.paramNames{paramCode};
                else
                    paramCodeString = 'triggerMode';
                end
            else
                paramCodeString = 'A parameter';
            end
            if (sum(param < RangeLow) > 0) || (sum(param > RangeHigh) > 0)
                error([paramCodeString ' was out of range: ' num2str(RangeLow) ' to ' num2str(RangeHigh)]);
            end
        end

        function bits = volts2Bits(obj, voltage)
            bits = ceil(((voltage+10)/20)*65535);
        end

        function volts = bytes2Volts(obj, bytes)
            VoltageBits = typecast(uint8(bytes), 'uint16');
            volts = round((((double(VoltageBits)/65535)*20)-10)*100)/100;
        end

        function seconds = bytes2Seconds(obj, Bytes)
            seconds = double(typecast(uint8(Bytes), 'uint32'))/obj.cycleFrequency;
        end
        function confirmWrite(obj)
            confirmed = obj.Port.read(1, 'uint8');
            if confirmed ~= 1
                error('Error: Pulse Pal did not confirm the parameter change.');
            end
        end

        function setOutputParam(obj, paramCode, val, units)
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
                    obj.checkParamRange(val, 'Time', range, paramCode);
                    value2send = val*obj.cycleFrequency;
                case 'Byte'
                    switch paramCode
                        case 1
                            range = [0 1];
                        case 12
                            range = [0 1];
                        case 13
                            range = [0 1];
                        case 14
                            range = [0 2];
                        case 15
                            range = [0 1];
                        case 16
                            range = [0 1];
                        case 18
                            range = [0 1];
                        case 128
                            range = [0 2];
                    end
                    obj.checkParamRange(val, 'Byte', range, paramCode);
                    value2send = val;
            end
            if obj.autoSyncOn
                if sum(paramCode == [2 3 17]) > 0
                    obj.Port.write([obj.opMenuByte 91 paramCode typecast(uint16(value2send), 'uint8')], 'uint8');
                elseif sum(paramCode == [4 5 6 7 8 9 10 11]) > 0
                    obj.Port.write([obj.opMenuByte 91 paramCode typecast(uint32(value2send), 'uint8')], 'uint8');
                else
                    obj.Port.write([obj.opMenuByte 91 paramCode value2send], 'uint8');
                end
                obj.confirmWrite;
            end
        end

        function syncAllParams(obj)
            if obj.autoSyncOn
                error('autoSync is set to ''on''. syncAllParams() may be used when autoSync is off.')
            end
            for i = 1:4
                if obj.customTrainTarget(i) == 1
                    BDuration = obj.burstDuration(i);
                    if BDuration == 0
                        error(['Error in output channel ' num2str(i) ': When custom train times target burst onsets, a non-zero burst duration must be defined.'])
                    end
                end
            end
            TimeData = [obj.phase1Duration; obj.interPhaseInterval; obj.phase2Duration;...
                obj.interPulseInterval; obj.burstDuration; obj.interBurstInterval;...
                obj.pulseTrainDuration; obj.pulseTrainDelay]*obj.cycleFrequency;
            TimeData = TimeData';
            VoltageData = [obj.volts2Bits(obj.phase1Voltage); obj.volts2Bits(obj.phase2Voltage); obj.volts2Bits(obj.restingVoltage)];
            VoltageData = VoltageData';
            SingleByteOutputParams = [obj.isBiphasic; obj.customTrainID; obj.customTrainTarget; obj.customTrainLoop];
            SingleByteOutputParams = SingleByteOutputParams';
            SingleByteParams = [SingleByteOutputParams(1:end) obj.linkTriggerChannel1 obj.linkTriggerChannel2 obj.triggerMode];
            obj.Port.write([obj.opMenuByte 92 typecast(uint32(TimeData(1:end)), 'uint8') ...
                            typecast(uint16(VoltageData(1:end)), 'uint8') SingleByteParams], 'uint8');
            obj.confirmWrite;
        end

        function sendCustomTrain(obj, trainID, pulseTimes, voltages)
            if length(pulseTimes) ~= length(voltages)
                error('There must be one voltage value (0-255) for every timestamp');
            end
            nPulses = length(pulseTimes);
            if nPulses > obj.maxCustomPulses
                error(['Error: Pulse Pal can only store ' num2str(obj.maxCustomPulses) ' pulses per custom pulse train.']);
            end
            if sum(sum(rem(round(pulseTimes*1000000), 100))) > 0
                error('Non-zero time values for Pulse Pal must be multiples of 100 microseconds.');
            end
            CandidateTimes = uint32(pulseTimes*obj.cycleFrequency);
            CandidateVoltages = voltages;
            if (sum(CandidateTimes < 0) > 0)
                error('Error: Custom pulse times must be positive');
            end
            if sum(diff(CandidateTimes) < 0) > 0
                error('Error: Custom pulse times must always increase');
            end
            if (CandidateTimes(end) > (3600*obj.cycleFrequency))
                0; error('Error: Custom pulse times must be < 3600 s');
            end
            if (sum(abs(CandidateVoltages) > 10) > 0)
                error('Error: Custom voltage range = -10V to +10V');
            end
            if (length(CandidateVoltages) ~= length(CandidateTimes))
                error('Error: There must be a voltage for every timestamp');
            end
            if (length(unique(CandidateTimes)) ~= length(CandidateTimes))
                error('Error: Duplicate custom pulse times detected');
            end
            TimeOutput = CandidateTimes;
            VoltageOutput = obj.volts2Bits(voltages);
            if ~((trainID == 1) || (trainID == 2))
                error('The first argument must be the stimulus train ID (1 or 2)')
            end

            if trainID == 1
                OpCode = 75;
            else
                OpCode = 76;
            end
            obj.Port.write([obj.opMenuByte OpCode typecast(uint32([nPulses TimeOutput]), 'uint8') ...
                            typecast(uint16(VoltageOutput), 'uint8')], 'uint8');
            obj.confirmWrite;
        end
        function importCurrentParamsFromPulsePal(obj)
            obj.Port.write([obj.opMenuByte 93], 'uint8');
            Msg = obj.Port.read(178, 'uint8');
            autoSyncState = obj.autoSync;
            obj.autoSync = 'off';
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
            if autoSyncState
                obj.autoSync = 'on';
            end
        end
    end
end
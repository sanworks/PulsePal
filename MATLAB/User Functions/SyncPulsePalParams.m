%{
----------------------------------------------------------------------------

This file is part of the Sanworks Pulse Pal repository
Copyright (C) 2016 Sanworks LLC, Sound Beach, New York, USA

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

function ConfirmBit = SyncPulsePalParams

% Import virtual serial port object into this workspace from base
global PulsePalSystem;
    Params = PulsePalSystem.Params;
    % Extract trigger address bytes
    Chan1TrigAddressBytes = uint8(Params.LinkTriggerChannel1);
    Chan2TrigAddressBytes = uint8(Params.LinkTriggerChannel2);
    
    % Extract custom override byte (0 if parameterized, 1 if this channel uses custom
    % stimulus train 1, 2 if this channel uses custom stimulus train 2)
    CustomTrainID = uint8(Params.CustomTrainID);
    
    % Extract custom stim target byte (0 if custom timestamps point to
    % pulse onsets ignoring inter-pulse interval, 1 if custom timestamps point to burst onsets, 
    % ignoring inter-burst interval)
    CustomTrainTarget = uint8(Params.CustomTrainTarget);
    
    % Extract custom stim loop byte (0 if the sequence is to be played only
    % once, 1 if it is to be looped until the end of
    % StimulusTrainDuration.)
    CustomTrainLoop = uint8(Params.CustomTrainLoop);
    
    % Extract biphasic settings for the four channels - 0 if monophasic pulses, 1 if biphasic
    IsBiphasic = uint8(Params.IsBiphasic);
    
    % Extract pulse voltage for phase 1
    Phase1Voltages = Params.Phase1Voltage;
    % Extract pulse voltage for phase 2
    Phase2Voltages = Params.Phase2Voltage;
    % Extract resting voltages
    RestingVoltages = Params.RestingVoltage;
    
    % Check if pulse amplitude is in range
    AllVoltages = [Phase1Voltages Phase2Voltages RestingVoltages];
    if (sum(AllVoltages > 10) > 0) || (sum(AllVoltages < -10) > 0)
        error(['Error: Invalid parameter detected. Voltages must be in the range -10V to 10V, and will be rounded to the nearest ' num2str(PulsePalSystem.VoltageStep*1000) ' mV.'])
    end
    
    % Check if burst duration is defined when custom timestamps target
    % burst onsets
    for i = 1:4
        if CustomTrainTarget(i) == 1
            BDuration = Params.BurstDuration(i);
            if BDuration == 0
                error(['Error in output channel ' num2str(i) ': When custom train times target burst onsets, a non-zero burst duration must be defined.'])
            end
        end
    end

    % For parameterized mode, check whether partial pulses will be
    % generated, and adjust specified stimulus duration to exclude them.
    for i = 1:4
        if Params.IsBiphasic(i) == 0
            PulseDuration = Params.Phase1Duration(i);
        else
            PulseDuration = Params.Phase1Duration(i) + Params.InterPhaseInterval(i) + Params.Phase2Duration(i);
        end
        PulseTrainDuration = Params.PulseTrainDuration(i);
        PulseOverlap = rem(PulseTrainDuration, PulseDuration);
        if PulseOverlap > 0
            PulseTrainDuration(i) = PulseTrainDuration - PulseOverlap;
            PulsePalSystem.Params.PulseTrainDuration(i) = PulseTrainDuration(i);
        end
    end
    
    
    % Extract voltages for phases 1 and 2
    Phase1Voltages = PulsePalVolts2Bits(Phase1Voltages, PulsePalSystem.RegisterBits);
    Phase2Voltages = PulsePalVolts2Bits(Phase2Voltages, PulsePalSystem.RegisterBits);
    RestingVoltages = PulsePalVolts2Bits(RestingVoltages, PulsePalSystem.RegisterBits);
    
    % Extract input channel settings
    
    TriggerMode = Params.TriggerMode; % if 0, "Normal mode", triggers on low to high transitions and ignores triggers until end of stimulus train. 
    % if 1, "Toggle mode", triggers on low to high and shuts off stimulus
    % train on next high to low. If 2, "Button mode", triggers on low to
    % high and shuts off on high to low.
    
    TimeData = [Params.Phase1Duration; Params.InterPhaseInterval; Params.Phase2Duration; Params.InterPulseInterval; Params.BurstDuration; Params.InterBurstInterval; Params.PulseTrainDuration; Params.PulseTrainDelay];
    
    % Ensure time data is within range
    if sum(sum(rem(round(TimeData*1000000), PulsePalSystem.MinPulseDuration))) > 0
        errordlg(['Non-zero time values must be multiples of ' num2str(PulsePalSystem.MinPulseDuration) ' microseconds. Please check your program matrix.'], 'Invalid program');
    end
    
    TimeData = uint32(TimeData*PulsePalSystem.CycleFrequency); % Convert to multiple of cycle frequency
    
    % Arrange program into a single byte-string
    FormattedProgramTimestamps = TimeData(1:end); 
    if PulsePalSystem.FirmwareVersion < 19 % Pulse Pal 1
        SingleByteOutputParams = [IsBiphasic; Phase1Voltages; Phase2Voltages; CustomTrainID; CustomTrainTarget; CustomTrainLoop; RestingVoltages];
        FormattedParams = [SingleByteOutputParams(1:end) Chan1TrigAddressBytes Chan2TrigAddressBytes TriggerMode];
        ByteString = [PulsePalSystem.OpMenuByte 73 typecast(FormattedProgramTimestamps, 'uint8') FormattedParams];
    else % Pulse Pal 2
        FormattedVoltages = [Phase1Voltages; Phase2Voltages; RestingVoltages];
        FormattedVoltages = uint16(FormattedVoltages(1:end));
        SingleByteOutputParams = [IsBiphasic; CustomTrainID; CustomTrainTarget; CustomTrainLoop;];
        FormattedParams = [SingleByteOutputParams(1:end) Chan1TrigAddressBytes Chan2TrigAddressBytes TriggerMode];
        ByteString = [PulsePalSystem.OpMenuByte 73 typecast(FormattedProgramTimestamps, 'uint8') typecast(FormattedVoltages, 'uint8') FormattedParams];
    end
    PulsePalSerialInterface('write', ByteString, 'uint8');
    ConfirmBit = PulsePalSerialInterface('read', 1, 'uint8'); % Get confirmation
    OriginalProgMatrix = PulsePalSystem.CurrentProgram; % Compile Legacy Pulse Pal program matrix
    if isempty(OriginalProgMatrix)
        DefaultParams = load(fullfile(PulsePalSystem.PulsePalPath, 'Programs', 'ParameterMatrix_Example.mat'));
        OriginalProgMatrix = DefaultParams.ParameterMatrix;
    end
    OutputChanMatrix = [Params.IsBiphasic; Params.Phase1Voltage; Params.Phase2Voltage; Params.Phase1Duration; Params.InterPhaseInterval; Params.Phase2Duration; Params.InterPulseInterval; Params.BurstDuration; Params.InterBurstInterval; Params.PulseTrainDuration; Params.PulseTrainDelay; Params.LinkTriggerChannel1; Params.LinkTriggerChannel2; Params.CustomTrainID; Params.CustomTrainTarget; Params.CustomTrainLoop; Params.RestingVoltage];
    OriginalProgMatrix(2:18,2:5) = num2cell(OutputChanMatrix);
    OriginalProgMatrix(2,8:9) = num2cell(Params.TriggerMode);
    PulsePalSystem.CurrentProgram = OriginalProgMatrix; % Update Legacy Pulse Pal program matrix
    
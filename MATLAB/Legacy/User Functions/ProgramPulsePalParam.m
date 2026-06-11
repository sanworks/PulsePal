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

function ConfirmBit = ProgramPulsePalParam(Channel, ParamCode, ParamValue)

% Channel = Number of output channel to program (1-4)
% ParamCode = Parameter code for transmission from the following list:

% 1 = IsBiphasic (0 = no, 1 = yes)
% 2 = Phase1Voltage (-10V to +10V) 
% 3 = Phase2Voltage (-10V to +10V)
% 4 = Phase1Duration (100us-3600s)
% 5 = InterPhaseInterval (100us-3600s)
% 6 = Phase2Duration (100us-3600s)
% 7 = InterPulseInterval (100us-3600s)
% 8 = BurstDuration (0us-3600s)
% 9 = BurstInterval (0us-3600s)
% 10 = PulseTrainDuration (100us-3600s)
% 11 = PulseTrainDelay (100us-3600s)
% 12 = LinkedToTriggerCH1 (0 = no, 1 = yes)
% 13 = LinkedToTriggerCH2 (0 = no, 1 = yes)
% 14 = CustomTrainID (0, 1 or 2)
% 15 = CustomTrainTarget (0 = pulses, 1 = bursts)
% 16 = CustomTrainLoop (0 = no, 1 = yes)
% 17 = RestingVoltage (-10V to +10V)
% 128 = TriggerMode (1 = normal, 2 = toggle, 3 = gated, FOR TRIGGER CHANNELS ONLY

% For the ParamCode argument, use the number of the parameter (1-17; 128, faster) or optionally, the string
% (i.e. 'Phase1Voltage' for slower but more readable code)

% convert string param code to integer
global PulsePalSystem;
ValidParamCodes = [1:17 128];
if ischar(ParamCode)
    if strcmp(ParamCode, 'TriggerMode')
        ParamCode = 128;
    else
        ParamCode = strcmpi(ParamCode, PulsePalSystem.ParamNames);
        if sum(ParamCode) == 0
            error('Error: invalid parameter code.')
        end
            ParamCode = find(ParamCode);
    end
elseif ~ismember(ParamCode, ValidParamCodes)
    error('Error: invalid parameter code.')
end

% Assert that trigger channel is 1 or 2
if ParamCode >= 128
    if Channel > 2
        error('Error: Pulse Pal has only two trigger channels.')
    end
end

% Import virtual serial port object into this workspace from base
OriginalValue = ParamValue;
% Determine whether data is time data
if (ParamCode < 12) && (ParamCode > 3)
    isTimeData = 1;
else
    isTimeData = 0;
end

% Extract voltages for phases 1 and 2
if (ParamCode == 2) || (ParamCode == 3) || (ParamCode == 17)
    ParamValue = PulsePalVolts2Bits(ParamValue, PulsePalSystem.RegisterBits);
    if PulsePalSystem.FirmwareVersion > 19 % Pulse Pal 2
        ParamValue = typecast(uint16(ParamValue), 'uint8');
    else
        ParamValue = uint8(ParamValue);
    end
end

% Sanity-check time data
if isTimeData
    if sum(sum(rem(round(ParamValue*1000000), PulsePalSystem.MinPulseDuration))) > 0
        error(['Non-zero time values for Pulse Pal must be multiples of ' num2str(PulsePalSystem.MinPulseDuration) ' microseconds.']);
    end
    ParamValue = round(ParamValue*PulsePalSystem.CycleFrequency); % Convert to multiple of 100us
end


% Format data to bytes
if isTimeData
    ParamBytes = typecast(uint32(ParamValue), 'uint8');
else
    ParamBytes = ParamValue;
end

% Assemble byte string instructing PulsePal to recieve a new single parameter (op code 74) and specify parameter and target channel before data
Bytestring = [PulsePalSystem.OpMenuByte 74 ParamCode Channel ParamBytes];
PulsePalSerialInterface('write', Bytestring, 'uint8');
ConfirmBit = PulsePalSerialInterface('read', 1, 'uint8'); % Get confirmation
if ConfirmBit == 1
    if ParamCode == 128
        PulsePalSystem.Params.TriggerMode(Channel) = OriginalValue;
        PulsePalSystem.CurrentProgram{2,Channel+7} = OriginalValue;
    else
        PulsePalSystem.Params.(PulsePalSystem.ParamNames{ParamCode})(Channel) = OriginalValue;
        PulsePalSystem.CurrentProgram{ParamCode+1,Channel+1} = OriginalValue;
    end
end

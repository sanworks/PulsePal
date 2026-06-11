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

function ConfirmBit = PulsePalSDSettings(SettingsFileName, Op)
% This function sets the current settings file on Pulse Pal 2's MicroSD
% card. 
% Arguments:
% SettingsFileName: A string specifying the name of the settings file to
% use. Must have a valid extension (i.e. something.dat)
% Op: 'Load' to load an existing settings file on the SD card. 'Save' to
% save Pulse Pal's current settings to the file (will create if the file
% does not exist, otherwise will overwrite).
% ConfirmBit: 1 if completed successfully, 0 if error.
global PulsePalSystem
if PulsePalSystem.FirmwareVersion < 20
    error('Error: You are connected to Pulse Pal 1.X, which does not have an external microSD memory for settings files.')
end
if sum(SettingsFileName == '.') == 0
    error('Error: The file name must have a valid extension.')
end
Op = lower(Op);
switch Op
    case 'save'
        OpByte = 1;
    case 'load'
        OpByte = 2;
    case 'delete'
        OpByte = 3;
end
SettingsNameLength = length(SettingsFileName);
Message = [PulsePalSystem.OpMenuByte 90 OpByte SettingsNameLength SettingsFileName];
PulsePalSerialInterface('write', Message, 'uint8');
CycleFreq = PulsePalSystem.CycleFrequency;
RegisterBits = PulsePalSystem.RegisterBits;
maxBits = 2^RegisterBits - 1;
if strcmp(Op, 'load')
    pause(.1);
    if PulsePalSerialInterface('bytesAvailable') > 0
        Pos = 1;
        Msg = PulsePalSerialInterface('read',178, 'uint8');
        for i = 1:4
            PulsePalSystem.Params.Phase1Duration(i) = Bytes2Seconds(Msg(Pos:Pos+3), CycleFreq); Pos = Pos + 4;
            PulsePalSystem.Params.InterPhaseInterval(i) = Bytes2Seconds(Msg(Pos:Pos+3), CycleFreq); Pos = Pos + 4;
            PulsePalSystem.Params.Phase2Duration(i) = Bytes2Seconds(Msg(Pos:Pos+3), CycleFreq); Pos = Pos + 4;
            PulsePalSystem.Params.InterPulseInterval(i) = Bytes2Seconds(Msg(Pos:Pos+3), CycleFreq); Pos = Pos + 4;
            PulsePalSystem.Params.BurstDuration(i) = Bytes2Seconds(Msg(Pos:Pos+3), CycleFreq); Pos = Pos + 4;
            PulsePalSystem.Params.InterBurstInterval(i) = Bytes2Seconds(Msg(Pos:Pos+3), CycleFreq); Pos = Pos + 4;
            PulsePalSystem.Params.PulseTrainDuration(i) = Bytes2Seconds(Msg(Pos:Pos+3), CycleFreq); Pos = Pos + 4;
            PulsePalSystem.Params.PulseTrainDelay(i) = Bytes2Seconds(Msg(Pos:Pos+3), CycleFreq); Pos = Pos + 4;
        end
        for i = 1:4
            PulsePalSystem.Params.Phase1Voltage(i) = Bytes2Volts(Msg(Pos:Pos+1), maxBits); Pos = Pos + 2;
            PulsePalSystem.Params.Phase2Voltage(i) = Bytes2Volts(Msg(Pos:Pos+1), maxBits); Pos = Pos + 2;
            PulsePalSystem.Params.RestingVoltage(i) = Bytes2Volts(Msg(Pos:Pos+1), maxBits); Pos = Pos + 2;
        end
        for i = 1:4
            PulsePalSystem.Params.IsBiphasic(i) = Msg(Pos); Pos = Pos + 1;
            PulsePalSystem.Params.CustomTrainID(i) = Msg(Pos); Pos = Pos + 1;
            PulsePalSystem.Params.CustomTrainTarget(i) = Msg(Pos); Pos = Pos + 1;
            PulsePalSystem.Params.CustomTrainLoop(i) = Msg(Pos); Pos = Pos + 1;
        end
        PulsePalSystem.Params.LinkTriggerChannel1 = Msg(Pos:Pos+3); Pos = Pos + 4;
        PulsePalSystem.Params.LinkTriggerChannel2 = Msg(Pos:Pos+3); Pos = Pos + 4;
        PulsePalSystem.Params.TriggerMode = Msg(Pos:Pos+1);
    end
end
ConfirmBit = 1;
function Seconds = Bytes2Seconds(Bytes, CycleFreq)
Seconds = double(typecast(uint8(Bytes), 'uint32'))/CycleFreq;

function Volts = Bytes2Volts(Bytes, maxBits)
VoltageBits = typecast(uint8(Bytes), 'uint16');
Volts = round((((double(VoltageBits)/maxBits)*20)-10)*100)/100;
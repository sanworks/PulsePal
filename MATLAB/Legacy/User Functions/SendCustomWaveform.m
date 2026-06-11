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

function ConfirmBit = SendCustomWaveform(TrainID, SamplingPeriod, Voltages)
global PulsePalSystem

OriginalSamplingPeriod = SamplingPeriod;

if rem(round(SamplingPeriod*1000000), PulsePalSystem.MinPulseDuration) > 0
        error(['Error: sampling period must be a multiple of ' num2str(PulsePalSystem.MinPulseDuration) ' microseconds.']);
end

SamplingPeriod = SamplingPeriod*PulsePalSystem.CycleFrequency; % Convert to multiple of cycle frequency
PulseTimes = 0:SamplingPeriod:((length(Voltages)*SamplingPeriod)-1);

nPulses = length(PulseTimes);
if PulsePalSystem.FirmwareVersion > 19
    if nPulses > 5000
        error('Error: Pulse Pal 2 can only store 5000 pulses per custom pulse train.');
    end
else
    if nPulses > 1000
        error('Error: Pulse Pal 1.X can only store 1000 pulses per custom pulse train.');
    end
end

% Sanity-check PulseTimes and voltages
CandidateVoltages = Voltages;
if (sum(abs(CandidateVoltages) > 10) > 0) 
    error('Error: Custom voltage range = -10V to +10V');
end
TimeOutput = uint32(PulseTimes); % Convert to multiple of 100us
if (length(unique(TimeOutput)) ~= length(TimeOutput))
    error('Error: Duplicate custom pulse times detected');
end
VoltageOutput = PulsePalVolts2Bits(Voltages, PulsePalSystem.RegisterBits);

if ~((TrainID == 1) || (TrainID == 2))
    error('The first argument must be the stimulus train ID (1 or 2)')
end

if TrainID == 1
    OpCode = 75;
else 
    OpCode = 76;
end


if strcmp(PulsePalSystem.OS, 'Microsoft Windows XP') && PulsePalSystem.FirmwareVersion < 20
    % This section calculates whether the transmission will result in
    % attempting to send a string of a multiple of 64 bytes, which will cause
    % WINXP machines to crash. If so, a byte is added to the transmission and
    % removed at the other end.
    if nPulses < 200
        USBPacketLengthCorrectionByte = uint8((rem(nPulses, 16) == 0));
    else
        nFullPackets = ceil(length(TimeOutput)/200) - 1;
        RemainderMessageLength = nPulses - (nFullPackets*200);
        if  uint8((rem(RemainderMessageLength, 16) == 0)) || (uint8((rem(nPulses, 16) == 0)))
            USBPacketLengthCorrectionByte = 1;
        else
            USBPacketLengthCorrectionByte = 0;
        end
    end
    if USBPacketLengthCorrectionByte == 1
        nPulsesByte = uint32(nPulses+1);
    else
        nPulsesByte = uint32(nPulses);
    end
    ByteString = [PulsePalSystem.OpMenuByte OpCode USBPacketLengthCorrectionByte typecast(nPulsesByte, 'uint8')]; 
    PulsePalSerialInterface('write', ByteString, 'uint8');
    % Send PulseTimes
    nPackets = ceil(length(TimeOutput)/200);
    Ind = 1;
    if nPackets > 1
        for x = 1:nPackets-1
            PulsePalSerialInterface('write', TimeOutput(Ind:Ind+199), 'uint32');
            Ind = Ind + 200;
        end
        if USBPacketLengthCorrectionByte == 1
            PulsePalSerialInterface('write', [TimeOutput(Ind:length(TimeOutput)) 5], 'uint32');
        else
            PulsePalSerialInterface('write', TimeOutput(Ind:length(TimeOutput)), 'uint32');
        end
    else
        if USBPacketLengthCorrectionByte == 1
            PulsePalSerialInterface('write', [TimeOutput 5], 'uint32');
        else
            PulsePalSerialInterface('write', TimeOutput, 'uint32');
        end
    end
    
    % Send voltages
    if nPulses > 800
        PulsePalSerialInterface('write', VoltageOutput(1:800), 'uint8');
        if USBPacketLengthCorrectionByte == 1
            PulsePalSerialInterface('write', [VoltageOutput(801:nPulses) 5], 'uint8');
        else
            PulsePalSerialInterface('write', VoltageOutput(801:nPulses), 'uint8');
        end
    else
        if USBPacketLengthCorrectionByte == 1
            PulsePalSerialInterface('write', [VoltageOutput(1:nPulses) 5], 'uint8');
        else
            PulsePalSerialInterface('write', VoltageOutput(1:nPulses), 'uint8');
        end
    end
    
else % This is the normal transmission scheme, as a single bytestring
    nPulsesByte = uint32(nPulses);
    if PulsePalSystem.FirmwareVersion < 20
        ByteString = [PulsePalSystem.OpMenuByte OpCode 0 typecast(nPulsesByte, 'uint8') typecast(TimeOutput, 'uint8') uint8(VoltageOutput)];
    else 
        TimeBytes = typecast(TimeOutput, 'uint8');
        VoltageBytes = typecast(uint16(VoltageOutput), 'uint8');
%         TimeVoltageBytes = uint8(zeros(1,length(TimeBytes)+length(VoltageBytes)));
%         Tpos = 1; Vpos = 1; TVpos = 1;
%         for x = 1:nPulses
%             TimeVoltageBytes(TVpos:TVpos+5) = [TimeBytes(Tpos:Tpos+3) VoltageBytes(Vpos:Vpos+1)];
%             Tpos = Tpos + 4;
%             Vpos = Vpos + 2;
%             TVpos = TVpos + 6;
%         end
        ByteString = [PulsePalSystem.OpMenuByte OpCode typecast(nPulsesByte, 'uint8') TimeBytes VoltageBytes];
    end
    PulsePalSerialInterface('write', ByteString, 'uint8');
    
end
ConfirmBit = PulsePalSerialInterface('read', 1, 'uint8'); % Get confirmation
% Change sampling period of last matrix sent on all channels that use the custom stimulus and re-send
TargetChannels = PulsePalSystem.Params.CustomTrainID == TrainID;
PulsePalSystem.Params.Phase1Duration(TargetChannels) = OriginalSamplingPeriod;
PulsePalSystem.Params.IsBiphasic(TargetChannels) = 0;
SyncPulsePalParams;
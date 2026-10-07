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

function Cycles = PulsePalTimes2Cycles(TimeData, IsBiphasic, Channels)
% Converts output channel time parameters from seconds to hardware timer cycles, for ops 73 and 74.
% TimeData: one row per time parameter, in parameter code order (Phase1Duration, InterPhaseInterval,
% Phase2Duration, InterPulseInterval, BurstDuration, InterBurstInterval, PulseTrainDuration, PulseTrainDelay),
% and one column per output channel. IsBiphasic: one value per column. Channels: the output channel number of
% each column, for error messages.
%
% Firmware v22 and newer refuse a phase or inter-pulse interval of 0 cycles, which firmware v21 accepted. With
% those versions, zeros are translated here, so that programs written for v21 play as they did:
% - InterPulseInterval 0 on a monophasic channel: v21 played one pulse per train (or one per burst), because it
%   scheduled the next pulse for a cycle that had already passed. Newer firmware would use 1 cycle instead, and
%   play pulses 50us apart. 3600s, the longest time this interface accepts, is sent, so that only the first
%   pulse plays.
% - Phase2Duration 0 on a monophasic channel: phase 2 is not played, so 1 cycle is sent.
% - Phase1Duration 0, or Phase2Duration 0 on a biphasic channel: v21 held the phase's voltage until the train
%   (or burst) ended. Newer firmware cannot play that, so it is an error. The documented minimum is 100us.
global PulsePalSystem
Cycles = uint32(TimeData*PulsePalSystem.CycleFrequency);
if PulsePalSystem.FirmwareVersion > 21
    for i = 1:size(Cycles, 2)
        if (Cycles(1,i) == 0) || (IsBiphasic(i) && (Cycles(3,i) == 0))
            error(['Error in output channel ' num2str(Channels(i)) ': phase durations must be at least ' ...
                num2str(PulsePalSystem.MinPulseDuration) ' microseconds.'])
        end
        if ~IsBiphasic(i)
            if Cycles(3,i) == 0
                Cycles(3,i) = 1;
            end
            if Cycles(4,i) == 0
                Cycles(4,i) = 3600*PulsePalSystem.CycleFrequency;
            end
        end
    end
end

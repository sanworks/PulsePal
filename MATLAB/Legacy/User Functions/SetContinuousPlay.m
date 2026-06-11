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

function ConfirmBit = SetContinuousPlay(Channel, State)
% This function was formerly called SetContinuousLoop()

% Import virtual serial port object into this workspace from base
global PulsePalSystem;

if ischar(Channel)
    error('Error: expected channel format is an integer 1-4')
end
if (Channel > 4) || (Channel < 1)
    error('Error: expected channel format is an integer 1-4')
end
if (State == 0) || (State == 1)
    PulsePalSerialInterface('write', [PulsePalSystem.OpMenuByte 82 Channel State], 'uint8');
else
    error('Error: Channel state must be 0 (for normal playback) or 1 (for continuous looping)')
end
ConfirmBit = PulsePalSerialInterface('read', 1, 'uint8'); % Get confirmation
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

function TriggerPulsePal(BinaryChannelIDsString)
global PulsePalSystem;


if ~ischar(BinaryChannelIDsString)
    error('Error: Format the channels to trigger as a string of 1s and 0s')
end

try    
TriggerAddress = bin2dec(BinaryChannelIDsString);
catch
    error('Error: Format the channels to trigger as a string of 1s and 0s')
end

if TriggerAddress > 15
     error('Error: There are only four output channels.')
end
TriggerAddress = uint8(TriggerAddress);
PulsePalSerialInterface('write', [PulsePalSystem.OpMenuByte 77 TriggerAddress], 'uint8');
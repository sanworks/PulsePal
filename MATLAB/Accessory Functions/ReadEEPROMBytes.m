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

function Data = ReadEEPROMBytes(RawStartAddress, nBytes)
% This function reads bytes from Pulse Pal's EEPROM chip.
global PulsePalSystem
if PulsePalSystem.FirmwareVersion > 19
    error('Error: Pulse Pal 2+ does not have an external EEPROM memory. See microSD card functions.')
end
PulsePalSerialInterface('write', [PulsePalSystem.OpMenuByte char(85) char(RawStartAddress) char(nBytes)], 'uint8');
Data = PulsePalSerialInterface('read', nBytes, 'uint8');
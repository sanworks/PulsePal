%{
----------------------------------------------------------------------------

This file is part of the PulsePal Project
Copyright (C) 2014 Joshua I. Sanders, Cold Spring Harbor Laboratory, NY, USA

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

function Data = ReadSDBytes(StartAddress, nBytes)
% This function reads bytes from Pulse Pal's EEPROM chip.
global PulsePalSystem;
nBytesBytes = typecast(uint32(nBytes), 'uint8');
StartAddressBytes = typecast(uint32(StartAddress), 'uint8');
fwrite(PulsePalSystem.SerialPort, [PulsePalSystem.OpMenuByte 85 StartAddressBytes nBytesBytes], 'uint8');

Data = fread(PulsePalSystem.SerialPort, nBytes);
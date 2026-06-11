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

function Data = ReadSDProgram
% This function reads bytes from the current file on Pulse Pal 2's MicroSD card.
global PulsePalSystem;
nBytesBytes = typecast(uint32(nBytes), 'uint8');
PulsePalSerialInterface('write', [PulsePalSystem.OpMenuByte 85 nBytesBytes], 'uint8');
Data = PulsePalSerialInterface('read', nBytes, 'uint8');

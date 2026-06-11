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
global PulsePalSystem
% This function returns the currently loaded parameter settings file from Pulse Pal 2's MicroSD card.
% It is returned as an array of bytes, read directly from the settings file.
if PulsePalSystem.FirmwareVersion < 20
    error('Error: Pulse Pal 1.X does not have a microSD card.')
end
PulsePalSerialInterface('write', [PulsePalSystem.OpMenuByte 85], 'uint8');
Data = PulsePalSerialInterface('read', 178, 'uint8');

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

function LogicLevel = PulsePalDigitalRead(MicrocontrollerPin)
% Writes logic 0 or 1 to an i/o pin on the microcontroller. Pin will be automatically configured for output.
global PulsePalSystem;
if (MicrocontrollerPin < 1) || (MicrocontrollerPin > 45)
    error('Error: Invalid microcontroller pin.')
end
PulsePalSerialInterface('write', [PulsePalSystem.OpMenuByte 87 MicrocontrollerPin], 'uint8');
LogicLevel = PulsePalSerialInterface('read', 1, 'uint8');


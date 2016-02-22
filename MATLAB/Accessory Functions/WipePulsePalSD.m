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

function Confirmed = WipePulsePalSD(varargin)
% Optional argument "Verbose" = 1 (default), or 0 (no prompts)
global PulsePalSystem
if PulsePalSystem.FirmwareVersion < 20
    error('Error: Pulse Pal 1 does not have an external microSD memory for settings files.')
end
Verbose = 1;
if nargin > 0
    Verbose = varargin{1};
end
ByteString = [PulsePalSystem.OpMenuByte 93 219];
if Verbose
    Response = input('Delete ALL of your settings files? (Y/N) >', 's');
else
    Response = 'y';
end
if (lower(Response == 'y'))
    PulsePalSerialInterface('write', ByteString, 'uint8');
    Confirmed = PulsePalSerialInterface('read', 1, 'uint8');
    if Verbose
        disp('microSD card wiped. Default settings restored.');
    end
else
    if Verbose
        disp('microSD card wipe aborted.');
    end
end

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

function PulsePalDisplay(varargin)
global PulsePalSystem;
Message = varargin{1};
if nargin == 2
    Message = [Message char(254) varargin{2}];
end
Message = [PulsePalSystem.OpMenuByte char(78) char(length(Message)) Message];
PulsePalSerialInterface('write', Message, 'uint8');

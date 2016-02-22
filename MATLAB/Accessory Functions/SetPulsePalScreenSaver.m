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

function Confirmed = SetPulsePalScreenSaver(ScreenSaverEnabled, varargin)
% Arguments:
% ScreenSaverEnabled (boolean): 0 to disable, 1 to enable
% ScreenSaverOnsetDelay (seconds)
global PulsePalSystem;
if ScreenSaverEnabled == 0
    ByteString = [PulsePalSystem.OpMenuByte 94 ScreenSaverEnabled];
elseif ScreenSaverEnabled > 0
    if ScreenSaverEnabled == 1
        ScreenSaverOnsetDelay = varargin{1};
        ScreenSaverOnsetDelay = round(ScreenSaverOnsetDelay);
        if (ScreenSaverOnsetDelay <= 0)
            error('The ScreenSaverOnsetDelay argument must be positive and non-zero')
        end
        ByteString = [PulsePalSystem.OpMenuByte 94 ScreenSaverEnabled typecast(uint32(ScreenSaverOnsetDelay*PulsePalSystem.CycleFrequency), 'uint8')];
    end
else
   error('The first argument must be 0 (screen saver disabled) or 1 (enabled)') 
end

PulsePalSerialInterface('write', ByteString, 'uint8');
Confirmed = PulsePalSerialInterface('read', 1, 'uint8');


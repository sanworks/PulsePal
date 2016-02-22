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
% This function is a hacky way to find available Serial ports on Windows platforms.
% It was intended as a substitute for instrfind, a MATLAB instrument
% control toolbox command. If you have instrument control toolbox
% installed, you can use the following instead: Ports = instrfind;
function SerialPorts = FindPulsePalPorts
% Find Leaflabs Maple ports (in case Pulse Pal 1 is connected)
[~, RawString] = system('wmic path Win32_SerialPort Where "Caption LIKE ''%Maple%''" Get DeviceID'); % Search for Maple serial USB port
if strfind(RawString, '''wmic'' is not recognized')
    error('Error: Must call PulsePal with a com port argument (i.e. PulsePal(''COM3'') in Windows XP Home edition and Windows 2000.')
end
PortLocations = strfind(RawString, 'COM');
LeafLabsPorts = cell(1,100);
nPorts = length(PortLocations);
for x = 1:nPorts
    Clip = RawString(PortLocations(x):PortLocations(x)+6);
    LeafLabsPorts{x} = Clip(1:find(Clip == 32,1, 'first')-1);
end
LeafLabsPorts = LeafLabsPorts(1:nPorts);
% Find Arduino Due ports (in case Pulse Pal 2 is connected)
[Status RawString] = system('wmic path Win32_SerialPort Where "Caption LIKE ''%Arduino DUE%''" Get DeviceID');
PortLocations = strfind(RawString, 'COM');
ArduinoPorts = cell(1,100);
nPorts = length(PortLocations);
for x = 1:nPorts
    Clip = RawString(PortLocations(x):PortLocations(x)+6);
    ArduinoPorts{x} = Clip(1:find(Clip == 32,1, 'first')-1);
end
ArduinoPorts = ArduinoPorts(1:nPorts);
SerialPorts = [LeafLabsPorts ArduinoPorts];
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

function Ports = ParseCOMString_LINUX(string)
string = strtrim(string);
PortStringPositions = strfind(string, '/dev/ttyACM');
nPorts = length(PortStringPositions);
CandidatePorts = cell(1,nPorts);
nGoodPorts = 0;
for x = 1:nPorts
    if PortStringPositions(x)+11 <= length(string)
        CandidatePort = strtrim(string(PortStringPositions(x):PortStringPositions(x)+11));
        nGoodPorts = nGoodPorts + 1;
        CandidatePorts{nGoodPorts} = CandidatePort;
    end
end
Ports = CandidatePorts(1:nGoodPorts);
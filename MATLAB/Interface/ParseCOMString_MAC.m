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

function words = ParseCOMString_MAC(string)
string = strtrim(string);
string = lower(string);
nSpaces = sum(string == char(9)) + sum(string == char(10));
if nSpaces > 0
    Spaces = find((string == char(9)) + (string == char(10)));
    Pos = 1;
    words = cell(1,nSpaces);
    for x = 1:nSpaces
        words{x} = string(Pos:Spaces(x) - 1);
        Pos = Pos + length(words{x}) + 1;
    end
    words{x+1} = string(Pos:length(string));
else
    words{1} = string;
end

% Eliminate bluetooth ports
nGoodPortsFound = 0;
TempList = cell(1,1);
for x = 1:length(words)
    Portstring = words{x};
    ValidPort = 1;
    for y = 1:(length(Portstring) - 4)
        if sum(Portstring(y:y+3) == 'blue') == 4
            ValidPort = 0;
        end
    end
    if ValidPort == 1
        nGoodPortsFound = nGoodPortsFound + 1;
        TempList{nGoodPortsFound} = Portstring;
    end
end
words = TempList;
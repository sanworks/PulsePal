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
function [HighestByte HighByte LowByte LowestByte] = BreakLongToBytes(LongInteger)

BinaryWord = dec2bin(LongInteger);
nSpaces = 32-length(BinaryWord);
Pad = '';
if nSpaces < 32
    for x = 1:nSpaces
        Pad = [Pad '0'];
    end
    BinaryWord = [Pad BinaryWord];
end

HighestByte = BinaryWord(1:8);
HighByte = BinaryWord(9:16);
LowByte = BinaryWord(17:24);
LowestByte = BinaryWord(25:32);
HighestByte = uint8(bin2dec(HighestByte));
HighByte = uint8(bin2dec(HighByte));
LowByte = uint8(bin2dec(LowByte));
LowestByte = uint8(bin2dec(LowestByte));
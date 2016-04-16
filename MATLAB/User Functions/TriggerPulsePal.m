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

function TriggerPulsePal(Channels, varargin)
% This function triggers Pulse Pal's output channels directly.
% It accepts three possible argument formats to specify the channels:
%
% 1. A vector containing a list of output channels to trigger 
% (i.e. TriggerPulsePal([1 3 4]) to trigger channels 1, 3 and 4 simultaneously)
% Note: Calling the function this way causes the lowest processing latency.
%
% 2. Up to four separate arguments, each listing an output channel to trigger
% (i.e. TriggerPulsePal(1, 3, 4) to trigger channels 1, 3 and 4 simultaneously)
%
% 3. (legacy) A character string specifying the channels to trigger in binary (i.e.
% TriggerPulsePal('1101') to trigger channels 1, 3 and 4 simultaneously)

global PulsePalSystem;


if ischar(Channels)
    TriggerAddress = bin2dec(Channels);
else
    if nargin > 1
        Channels = [Channels cell2mat(varargin)];
    end
        ChannelsBinary = zeros(1,4);
        ChannelsBinary(Channels) = 1;
        TriggerAddress = sum(ChannelsBinary .* [1 2 4 8]);
end

TriggerAddress = uint8(TriggerAddress);
PulsePalSerialInterface('write', [PulsePalSystem.OpMenuByte 77 TriggerAddress], 'uint8');
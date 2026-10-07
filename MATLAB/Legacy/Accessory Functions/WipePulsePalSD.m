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
if strcmpi(Response, 'y') && (PulsePalSystem.FirmwareVersion > 21)
    % Firmware v22 and newer use op 93 to send the current parameters, and wipe the card by formatting it (op 97,
    % Pulse Pal 3 only). The device replies with lines of status text, the last of which contains '!', and then a
    % confirm byte, sent once it has reloaded its default parameters, which can be well after the text.
    Confirmed = 0;
    if PulsePalSystem.HardwareVersion < 3
        warning('Pulse Pal 2 cannot wipe its microSD card from MATLAB. Delete settings files with PulsePalSDSettings(FileName, ''delete'').');
        return
    end
    PulsePalSerialInterface('write', [PulsePalSystem.OpMenuByte 97], 'uint8');
    Msg = [];
    LineEnd = [];
    tic;
    while toc < 30
        nBytes = PulsePalSerialInterface('bytesAvailable');
        if nBytes > 0
            Msg = [Msg double(reshape(PulsePalSerialInterface('read', nBytes, 'uint8'), 1, []))];
            FlagIndex = find(Msg == '!', 1);
            if ~isempty(FlagIndex)
                LineEnd = FlagIndex - 1 + find(Msg(FlagIndex:end) == 10, 1); % 10 = newline
            end
            if ~isempty(LineEnd) && (length(Msg) > LineEnd)
                break
            end
        end
        pause(.01);
    end
    if isempty(LineEnd) || (length(Msg) <= LineEnd)
        error('Error: Pulse Pal did not report the result of wiping its microSD card within 30 seconds.')
    end
    Confirmed = Msg(LineEnd+1);
    PulsePalSystem.Params = DefaultPulsePalParameters; % The device has loaded its default parameters
    if Verbose
        if Confirmed == 1
            disp('microSD card wiped. Default settings restored.');
        else
            disp(['microSD card wipe failed: ' strtrim(char(Msg(1:LineEnd)))]);
        end
    end
elseif strcmpi(Response, 'y')
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

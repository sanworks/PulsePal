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

function ConfirmBit = SetPulsePalVoltage(ChannelID, Voltage)
global PulsePalSystem
Message = [PulsePalSystem.OpMenuByte 79 ChannelID];
if PulsePalSystem.FirmwareVersion < 20
    ArCOM_PulsePal('write', PulsePalSystem.SerialPort, [Message PulsePalVolts2Bits(Voltage, PulsePalSystem.RegisterBits)], 'uint8');
else
    ArCOM_PulsePal('write', PulsePalSystem.SerialPort, Message, 'uint8', PulsePalVolts2Bits(Voltage, PulsePalSystem.RegisterBits), 'uint16');
end
ConfirmBit = ArCOM_PulsePal('read', PulsePalSystem.SerialPort, 1, 'uint8'); % Get confirmation
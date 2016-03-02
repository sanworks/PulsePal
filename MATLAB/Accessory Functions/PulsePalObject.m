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

classdef PulsePalObject < handle
    
    properties
        GUIHandles
        Graphics
        FirmwareVersion
        OS
        MinPulseDuration
        Bits
        RegisterBits
        VoltageStep
        CycleFrequency
        CurrentProgram
        PulsePalPath
        SerialPort
        OpMenuByte
        SerialInterface
        UsingOctave
        ParamNames
        Params
    end
    
    methods
        
    end
    
end

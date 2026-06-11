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

function ClosePreviousPulsePalInstances
try
    out1 = com.mathworks.toolbox.instrument.Instrument.getNonLockedObjects;
    if isempty(out1)
        return;
    end
    for i = 0:out1.size-1
        inputObj  = out1.elementAt(i);
        className = class(inputObj);
        try
            obj = feval(char(getMATLABClassName(inputObj)), inputObj);
        catch %#ok<CTCH>
            if strcmp(className, 'com.mathworks.toolbox.instrument.SerialComm')
                obj = serial(inputObj);
            end
        end
        Props = get(obj);
        if isfield(Props, 'Tag')
            if strcmp(Props.Tag, 'PulsePal')
                fclose(obj);
                delete(obj);
            end
        end
    end
catch
end

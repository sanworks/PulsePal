% portName = pulsepal.portForSerialNumber(serialNumber) returns the name of the USB serial port whose device has this USB
% serial number, e.g. 'COM9' for '20345420'. A Pulse Pal 3's serial number is the Teensy's, unique to each device and the
% same whichever firmware it runs: info.serialNumber, which PulsePalDevice, WavePalDevice and SynthPalDevice read when
% they connect. Each class takes it in place of a port name, and calls this function to find the port:
%   P = PulsePalDevice('SerialNumber', '20345420');
% A port name can change when the device is plugged into another USB socket; its serial number does not. Pulse Pal 2
% has no serial number. Works on Windows (the serial numbers are in the registry, read through .NET) and Linux (in
% sysfs). On macOS, connect by port name.

%{
----------------------------------------------------------------------------

This file is part of the Sanworks Pulse Pal repository
Copyright (C) 2026 Sanworks LLC, Rochester, New York, USA

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

function portName = portForSerialNumber(serialNumber)
if isnumeric(serialNumber) && isscalar(serialNumber)
    serialNumber = num2str(serialNumber, '%.0f');
end
if ~(ischar(serialNumber) || (isstring(serialNumber) && isscalar(serialNumber)))
    error('The serial number is a number or text, e.g. ''20345420''.')
end
serialNumber = strtrim(char(serialNumber));
if ispc
    [names, serials] = windowsPorts();
elseif isunix && ~ismac
    [names, serials] = linuxPorts();
else
    error(['Finding a port by serial number works on Windows and Linux. On macOS, connect by port name, e.g. ' ...
           'PulsePalDevice(''/dev/cu.usbmodem203454201''): serialportlist lists them.'])
end
present = ismember(names, cellstr(serialportlist('all'))); % The registry also remembers devices unplugged since
names = names(present);
serials = serials(present);
[~, order] = sort(regexprep(names, '\d+', '${sprintf(''%010d'', str2double($0))}')); % Natural order: COM9 before COM12
names = names(order);
serials = serials(order);
matches = unique(names(strcmp(serials, serialNumber)));
if isscalar(matches)
    portName = matches{1};
elseif isempty(matches)
    listed = strjoin(cellfun(@(n, s) [n ' (' s ')'], names, serials, 'UniformOutput', false), ', ');
    if isempty(listed)
        listed = 'none';
    end
    error(['No USB serial port has serial number ' serialNumber '. Is the device plugged in, and not open in another '...
           'program? Ports with a serial number: ' listed '.'])
else
    error(['Several USB serial ports have serial number ' serialNumber ': ' strjoin(matches, ', ') ...
           '. Connect by port name.'])
end
end

function [names, serials] = windowsPorts()
% Each USB device's instance in HKLM\SYSTEM\CurrentControlSet\Enum\USB\VID_xxxx&PID_xxxx is named by its serial number
% (or by a generated ID, with '&' in it, for a device that has none). A serial port's instance holds its port name in
% "Device Parameters". A Teensy is a composite device: its serial number's instance holds a ParentIdPrefix instead, and
% the port is an interface's instance, VID_xxxx&PID_xxxx&MI_nn\<ParentIdPrefix>&00nn. winqueryreg cannot list
% subkeys, so .NET's registry classes read them.
names = {};
serials = {};
NET.addAssembly('mscorlib'); % Microsoft.Win32.Registry
usb = Microsoft.Win32.Registry.LocalMachine.OpenSubKey('SYSTEM\CurrentControlSet\Enum\USB');
if isempty(usb)
    return
end
devices = cell(usb.GetSubKeyNames());
for i = 1:numel(devices)
    if contains(devices{i}, '&MI_', 'IgnoreCase', true)
        continue % An interface of a composite device: reached from its device below
    end
    interfaces = devices(startsWith(devices, [devices{i} '&MI_'], 'IgnoreCase', true));
    try
        device = usb.OpenSubKey(devices{i});
        instances = cell(device.GetSubKeyNames());
    catch
        continue % A key this user cannot read
    end
    for j = 1:numel(instances)
        try
            instance = device.OpenSubKey(instances{j});
            portName = devicePortName(instance);
            prefix = instance.GetValue('ParentIdPrefix');
        catch
            continue
        end
        if isempty(portName) && ~isempty(prefix)
            for k = 1:numel(interfaces) % The interface instances whose names start with the prefix
                try
                    interface = usb.OpenSubKey(interfaces{k});
                    children = cell(interface.GetSubKeyNames());
                    children = children(startsWith(children, [char(prefix) '&'], 'IgnoreCase', true));
                    for m = 1:numel(children)
                        portName = devicePortName(interface.OpenSubKey(children{m}));
                        if ~isempty(portName)
                            break
                        end
                    end
                catch
                end
                if ~isempty(portName)
                    break
                end
            end
        end
        if ~isempty(portName)
            names{end+1} = portName; %#ok<AGROW>
            serials{end+1} = instances{j}; %#ok<AGROW>
        end
    end
end
end

function portName = devicePortName(instance)
% The PortName in an instance's "Device Parameters", or '' if it has none
portName = '';
if isempty(instance)
    return
end
parameters = instance.OpenSubKey('Device Parameters');
if ~isempty(parameters)
    value = parameters.GetValue('PortName');
    if ~isempty(value) && isa(value, 'System.String')
        portName = char(value);
    end
end
end

function [names, serials] = linuxPorts()
% /sys/class/tty/ttyACMn/device is the USB interface of the port; the USB device above it holds the serial number
names = {};
serials = {};
ttys = [dir('/sys/class/tty/ttyACM*'); dir('/sys/class/tty/ttyUSB*')];
for i = 1:numel(ttys)
    serialFile = fullfile('/sys/class/tty', ttys(i).name, 'device', '..', 'serial');
    if isfile(serialFile)
        names{end+1} = ['/dev/' ttys(i).name]; %#ok<AGROW>
        serials{end+1} = strtrim(fileread(serialFile)); %#ok<AGROW>
    end
end
end

%{
----------------------------------------------------------------------------

This file is part of the Sanworks Pulse Pal repository
Copyright (C) Sanworks LLC, Rochester, New York, USA

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

% LoadPulsePalFirmware is a GUI-driven class to select and load firmware for
% PulsePal devices.
%
% Example usage:
% LoadPulsePalFirmware;

classdef LoadPulsePalFirmware < handle
    properties

    end
    properties (Access = private)
        Port
        gui
        PortType
        FirmwareVersions
        LoaderApps
        tycmd
    end
    methods
        function obj = LoadPulsePalFirmware
            % Optional args:
            % set2Device: A string containing a filter for the firmware list
            if ~ismember(computer,{'PCWIN64', 'GLNXA64'})
                error(['Error: The PulsePal firmware updater is not yet available on %s.' char(10)...
                    'Please follow instructions <a href="matlab:web(''https://sites.google.com/site/pulsepalwiki/upgrading-firmware'',''-browser'')">here</a> to update with the Arduino application.'],computer)
            end

            % Location of firmware binaries
            firmwarePath = fileparts(mfilename('fullpath'));

            % Define path for tycmd executable
            switch computer
                case 'PCWIN64'
                    obj.tycmd = fullfile(firmwarePath,'tycmd');
                case 'GLNXA64'
                    obj.tycmd = fullfile(firmwarePath,'tycmd_linux64');
            end

            % Check for udev rules on linux
            if ~ismac && isunix && ~exist('/etc/udev/rules.d/00-teensy.rules','file')
                error(['Error: Cannot find teensy udev rules.' char(10) ...
                    'Please follow instructions <a href="matlab:web(''https://www.pjrc.com/teensy/' ...
                    'td_download.html'',''-browser'')">here</a> to install them.'])
            end

            % Parse firmware filenames to populate menus
            allFiles = dir(firmwarePath);
            nFirmwareFound = 0;
            firmwareNames = cell(0,1);
            obj.FirmwareVersions = cell(0,1);
            obj.LoaderApps = cell(0,1);
            for i = 3:length(allFiles)
                fileExt = allFiles(i).name(end-2:end);
                if strcmp(fileExt, 'bin') || strcmp(fileExt, 'hex')
                    fileName = allFiles(i).name(1:end-4);
                    divPos = strfind(fileName, '_'); divPos = divPos(end);
                    thisFirmwareName = fileName(1:divPos-1);
                    thisFirmwareVersion = fileName(divPos+2:end);
                    if sum(strcmp(thisFirmwareName,firmwareNames)) > 0
                        obj.FirmwareVersions{nFirmwareFound} = [obj.FirmwareVersions{nFirmwareFound} {thisFirmwareVersion}];
                        [~, Inds] = sort(str2double(obj.FirmwareVersions{nFirmwareFound}),'descend');
                        obj.FirmwareVersions{nFirmwareFound} = obj.FirmwareVersions{nFirmwareFound}(Inds);
                    else
                        nFirmwareFound = nFirmwareFound + 1;
                        firmwareNames{nFirmwareFound} = thisFirmwareName;
                        obj.FirmwareVersions{nFirmwareFound} = {thisFirmwareVersion};
                        switch(fileExt)
                            case 'bin'
                                obj.LoaderApps{nFirmwareFound} = 'bossac';
                            case 'hex'
                                obj.LoaderApps{nFirmwareFound} = 'tycmd';
                        end
                    end
                end
            end

            % Get list of USB serial ports

            usbSerialPorts = sort(serialportlist('available'));
            if isunix && ~ismac
                % Linux also lists 32 motherboard serial ports, /dev/ttyS0 to /dev/ttyS31, whether or not they exist.
                % Pulse Pal 2 and 3 are USB CDC devices, which Linux names /dev/ttyACM0, /dev/ttyACM1...
                usbSerialPorts = usbSerialPorts(startsWith(usbSerialPorts, '/dev/ttyACM'));
            end
            obj.PortType(1:length(usbSerialPorts)) = 1;

            % Get Teensy RawHID boards
            [~, Tstring] = system(['"' obj.tycmd '" list']);
            allRawHIDs = cell(1,0);
            if ~isempty(Tstring)
                hardReturns = find(Tstring == 10);
                pos = 1;
                found = 0;
                if ~isempty(hardReturns)
                    for i = 1:length(hardReturns)-1
                        segment = Tstring(pos:hardReturns(i));
                        if ~isempty(strfind(segment, 'Teensyduino RawHID')) || ~isempty(strfind(segment, 'HalfKay'))
                            found = found + 1;
                            allRawHIDs{found} = ['SER#' segment(strfind(segment, 'add ') + 4:strfind(segment, '-Teensy')-1)];
                        end
                        pos = pos + length(segment);
                    end
                    segment = Tstring(pos:end);
                    if ~isempty(strfind(segment, 'Teensyduino RawHID')) || ~isempty(strfind(segment, 'HalfKay'))
                        found = found + 1;
                        allRawHIDs{found} = ['SER#' segment(strfind(segment, 'add ') + 4:strfind(segment, '-Teensy')-1)];
                    end
                end
            end
            obj.PortType = [obj.PortType ones(1,length(allRawHIDs))*2];

            % Combine lists of USB serial ports & RawHID devices
            allPorts = [usbSerialPorts allRawHIDs];
            if isempty(allPorts)
                error('Error: No USB serial devices were detected.');
            end
            labelOffset = 0;
            dropMenuFontSize = 10;
            labelFontSize = 16;
            if ispc
                labelOffset = -2;
                dropMenuFontSize = 12;
                labelFontSize = 18;
            end
            % Set up GUI
            bgPath = fullfile(firmwarePath,'FirmwareBG.bmp');
            bg = imread(bgPath);
            obj.gui.Fig  = figure('name','Pulse Pal Firmware Loading Tool', 'position',[100,100,855,200],...
                'numbertitle','off', 'MenuBar', 'none', 'Resize', 'off', 'Visible', 'off',...
                'Color',[0.1 0.1 0.1],'CloseRequestFcn',@(h,e)obj.clear_obj());
            LoadPulsePalFirmware.fit_window_to_zoom(obj.gui.Fig);
            bgAxes = axes('units','normalized', 'position',[0 0 1 1]);
            uistack(bgAxes,'bottom');
            image(bg); axis off;
            devicesPos = [25 80 320 30];
            dropdownYPos = 80;
            uiType = 'popup';
            if ~verLessThan('matlab', '25.1')
                devicesPos = [25 20 320 110];
                dropdownYPos = 100;
                uiType = 'listbox';
            end

            uicontrol('Style', 'text', 'Position', [18+labelOffset 150 120 30], 'String', 'Firmware', 'FontSize', labelFontSize,...
                'FontWeight', 'bold', 'BackgroundColor', [0.05 0.1 0.05], 'ForegroundColor', [0.1 1 0.1]);
            obj.gui.Devices = uicontrol('Style', uiType, 'Position', devicesPos, 'String', firmwareNames, 'FontSize', dropMenuFontSize,...
                'FontWeight', 'bold','Callback', @(h,e)obj.update_versions(), 'BackgroundColor', [0.05 0.1 0.05],...
                'ForegroundColor', [0.1 1 0.1]);
            uicontrol('Style', 'text', 'Position', [375+labelOffset 150 105 30], 'String', 'Version', 'FontSize', labelFontSize,...
                'FontWeight', 'bold', 'BackgroundColor', [0.05 0.1 0.05], 'ForegroundColor', [0.1 1 0.1]);
            obj.gui.Versions = uicontrol('Style', 'popup', 'Position', [385 dropdownYPos 80 30], 'String', obj.FirmwareVersions{1},...
                'FontSize', dropMenuFontSize,'FontWeight', 'bold', 'BackgroundColor', [0.05 0.1 0.05], 'ForegroundColor', [0.1 1 0.1]);
            uicontrol('Style', 'text', 'Position', [490 150 65 30], 'String', 'Port', 'FontSize', labelFontSize,...
                'FontWeight', 'bold', 'BackgroundColor', [0.05 0.1 0.05], 'ForegroundColor', [0.1 1 0.1]);
            obj.gui.Ports = uicontrol('Style', 'popup', 'Position', [500 dropdownYPos 180 30], 'String', allPorts, 'FontSize', dropMenuFontSize,...
                'FontWeight', 'bold', 'BackgroundColor', [0.05 0.1 0.05], 'ForegroundColor', [0.1 1 0.1], 'Callback', @(h,e)obj.update_target_hw());
            obj.gui.smButton = uicontrol('Style', 'pushbutton', 'Position', [730 75 100 50], 'String', 'Load', 'FontSize', 14,...
                'FontWeight', 'bold', 'Enable', 'on','Callback', @(h,e)obj.update_firmware(), 'BackgroundColor', [0.05 0.1 0.05],...
                'ForegroundColor', [0.1 1 0.1]);
            obj.gui.status = uicontrol('Style', 'text', 'Position', [500 0 355 30], 'String', 'Status: Idle', 'FontSize', 12,...
                'FontWeight', 'bold', 'FontName', 'Courier', 'BackgroundColor', [0.05 0.1 0.05], 'ForegroundColor', [0.1 1 0.1]);
            obj.select_target_device(allPorts{1});
            obj.update_versions(); % The versions of the firmware selected, not of the first in the list
            set(obj.gui.Fig, 'Visible', 'on');
        end

        function update_versions(obj, varargin)
            firmwareIndex = get(obj.gui.Devices, 'Value');
            set(obj.gui.Versions, 'Value', 1);
            set(obj.gui.Versions, 'String', obj.FirmwareVersions{firmwareIndex});
        end

        function update_firmware(obj, varargin)
            moduleNamePos = get(obj.gui.Devices, 'Value');
            moduleNameList = get(obj.gui.Devices, 'String');
            moduleName = moduleNameList{moduleNamePos};
            versionPos = get(obj.gui.Versions, 'Value');
            versionList = get(obj.gui.Versions, 'String');
            if ~iscell(versionList)
                versionList = {versionList};
            end
            version = versionList{versionPos};
            portNamePos = get(obj.gui.Ports, 'Value');
            portNameString = get(obj.gui.Ports, 'String');
            if ~iscell(portNameString)
                portNameString = {portNameString};
            end
            portName = portNameString{portNamePos};
            fileName = [moduleName '_v' version];
            durationMsg = '10 seconds';
            if strcmp(moduleName, 'PulsePal_HW2') && (ispc || ismac)
                durationMsg = '60 seconds'; % On Linux, the packaged bossac loaded it in about 5 s (2026-11-02)
            end

            h = warndlg(['Firmware upload may take up to ' durationMsg ' and progress will be shown in the Command Window. Press Ok to begin!']);
            LoadPulsePalFirmware.fit_window_to_zoom(h);
            uiwait(h);
            set(obj.gui.status, 'String', 'Status: Loading Firmware...');
            drawnow;
            [OK,msg] = obj.upload_firmware(portName, fileName, obj.LoaderApps{moduleNamePos}, obj.PortType(portNamePos));
            fontSize = 14;
            if isunix
                fontSize = 12;
            end
            if OK
                bgColor = [0.1 0.9 0.1];
                msg1 = '*GREAT SUCCESS*';
                msg2 = 'Firmware update complete';
                set(obj.gui.status, 'String', 'Status: Firmware Loaded.');
            else
                bgColor = [1 0.4 0.4];
                msg1 = '*FAILED*';
                msg2 = 'See command window';
                disp('Console output:')
                disp(msg)
                set(obj.gui.status, 'String', 'Status: Firmware Load Error');
            end

            obj.gui.ConfirmModal  = figure('name','Firmware Update', 'position',[335,120,280,200],...
                'numbertitle','off', 'MenuBar', 'none', 'Resize', 'off', 'Visible', 'off',...
                'Color',bgColor);
            LoadPulsePalFirmware.fit_window_to_zoom(obj.gui.ConfirmModal);

            obj.gui.Msg1 = uicontrol('Style', 'text', 'Position', [25 140 220 30], 'String', msg1, 'FontSize', fontSize,...
                'FontWeight', 'bold', 'BackgroundColor', bgColor, 'ForegroundColor', [0,0,0]);
            obj.gui.Msg2 = uicontrol('Style', 'text', 'Position', [15 90 250 30], 'String', msg2, 'FontSize', fontSize,...
                'FontWeight', 'bold', 'BackgroundColor', bgColor, 'ForegroundColor', [0,0,0]);
            uicontrol('Style', 'pushbutton', 'Position', [90 20 100 40], 'String', 'Ok', 'FontSize', 14,...
                'FontWeight', 'bold', 'BackgroundColor', [0.05 0.1 0.05], 'ForegroundColor', [0.1 1 0.1], 'Callback',...
                @(h,e)obj.close_modal());
            set(obj.gui.ConfirmModal, 'Visible', 'on');
            if OK
                bgColor(2) = 0.1;
                try
                    if verLessThan('matlab', '25.1')
                        for i = 1:100
                            bgColor(2) = bgColor(2) + (0.8/100);
                            set(obj.gui.ConfirmModal, 'Color', bgColor);
                            set(obj.gui.Msg1, 'BackgroundColor', bgColor);
                            set(obj.gui.Msg2, 'BackgroundColor', bgColor);
                            pause(.005);
                            drawnow;
                        end
                    end
                    bgColor = [0.1 0.9 0.1];
                    set(obj.gui.ConfirmModal, 'Color', bgColor);
                    set(obj.gui.Msg1, 'BackgroundColor', bgColor);
                    set(obj.gui.Msg2, 'BackgroundColor', bgColor);
                    figure(obj.gui.ConfirmModal);
                catch
                end

            end

        end

        function close_modal(obj)
            delete(obj.gui.ConfirmModal);
            delete(obj.gui.Fig);
            evalin('base', 'clear ans');
        end

        function [ok,msg] = upload_firmware(obj, targetPort, filename, loaderApp, portType)
            thisFolder = fileparts(which('LoadPulsePalFirmware'));
            switch loaderApp
                case 'bossac'
                    firmwarePath = fullfile(thisFolder, [filename '.bin']);

                    verifyUpload = false;   % set true for development/factory validation
                    nativeUsb = true;       % true for Due Native USB, false for Programming Port

                    verifyFlag = '';
                    if verifyUpload
                        verifyFlag = '-v';
                    end

                    if ispc
                        bossacPath = fullfile(thisFolder, 'bossac.exe');

                        if nativeUsb
                            uploadPort = obj.enter_due_bootloader(targetPort);
                        else
                            % Programming Port usually does not re-enumerate.
                            system(['@mode ' targetPort ':1200,N,8,1']);
                            pause(0.5);
                            uploadPort = targetPort;
                        end

                        programPath = sprintf('"%s" --port=%s -U %s -e -w %s -b "%s" -R', ...
                            bossacPath, uploadPort, lower(string(nativeUsb)), ...
                            verifyFlag, firmwarePath);

                    elseif isunix
                        if system('command -v bossac >/dev/null 2>&1')
                            if ismac
                                error('Cannot find bossac. Please install bossa-cli using your system package manager.')
                            end
                            % Ubuntu's package is in its universe repository, which a minimal install leaves off
                            error('LoadPulsePalFirmware:NoBossac', ['Cannot find bossac, the program that loads ' ...
                                'Pulse Pal 2''s firmware. To install it on Ubuntu, open a terminal (Ctrl+Alt+T) and ' ...
                                'enter these two commands, typing your password when asked:\n\n' ...
                                '    sudo apt update\n' ...
                                '    sudo apt install bossa-cli\n\n' ...
                                'Then click Load again. If apt reports "Unable to locate package bossa-cli", enable ' ...
                                'Ubuntu''s universe repository first, with:\n\n' ...
                                '    sudo add-apt-repository universe\n'])
                        end

                        if nativeUsb
                            uploadPort = obj.enter_due_bootloader(targetPort);
                        else
                            uploadPort = targetPort;
                        end

                        % The packaged bossac (1.9.1 on Ubuntu) reads -U's value as a number: -U=true read as 0,
                        % which selects RS-232. It also resets the CPU (-R) as soon as it has sent the command that
                        % sets the boot flag, without waiting for the flash controller: in the same call as the
                        % write, the Due came back in its bootloader with the flag unset and the flash's first page
                        % garbled (2 times in 2). So the reset is a second call, made once this one has finished.
                        programPath = sprintf('bossac --port=%s --usb-port=%d -e -w %s -b "%s"', ...
                            uploadPort, nativeUsb, verifyFlag, firmwarePath);
                        resetPath = sprintf('bossac --port=%s --usb-port=%d -R', uploadPort, nativeUsb);
                    end
                case 'tycmd'
                    if ~ispc && ~ismac
                        try % Try to give the uploader execute permissions
                            [ok, msg] = system(['chmod a+x "' fullfile(thisFolder, 'tycmd_linux64') '"']);
                            if ~isempty(msg)
                                warning(msg)
                            end
                        catch
                        end
                    end
                    firmwarePath = fullfile(thisFolder, [filename '.hex']);
                    if ispc
                        [x, y] = system('taskkill /F /IM teensy.exe');
                    elseif isunix
                        [x, y] = system('killall teensy');
                    end
                    pause(.1);
                    switch portType
                        case 1
                            programPath = ['"' obj.tycmd '" upload "' firmwarePath '" --board "@' targetPort '"'];
                        case 2
                            programPath = ['"' obj.tycmd '" upload "' firmwarePath '" --board "' targetPort(5:end) '"'];
                    end
            end
            disp('------Uploading new firmware------')
            disp([filename ' ==> ' targetPort])
            [status, msg] = system(programPath, '-echo');
            ok = 0;
            if ~isempty(strfind(msg, 'CPU reset')) || ~isempty(strfind(msg, 'Sending reset command'))
                ok = 1;
            end
            if ~ispc && strcmp(loaderApp, 'bossac') && status == 0
                % The packaged bossac prints nothing when it resets the CPU, and returned 0 when the Due then came
                % back in its bootloader, so on Linux the upload has worked once the Due enumerates as itself again
                [status, resetMsg] = system(resetPath, '-echo');
                msg = [msg resetMsg];
                ok = status == 0;
                if ok && ~ismac
                    ok = LoadPulsePalFirmware.wait_for_usb_id_linux(uploadPort, '2341:003e', 5);
                    if ~ok
                        msg = [msg 'The Arduino Due did not start the firmware (USB ID 2341:003e) after its reset.'];
                    end
                end
            end
            if ok
                disp('----------UPDATE COMPLETE---------')
            else
                disp('----------FAILED TO LOAD----------')
            end
            disp(' ');
        end

        function tf = is_teensy_port(obj, port)
            persistent loaded
            if isempty(loaded), NET.addAssembly('System.Management'); loaded = true; end

            q = sprintf("SELECT PNPDeviceID FROM Win32_PnPEntity WHERE Name LIKE '%%(%s)%%'", port);
            e = System.Management.ManagementObjectSearcher(q).Get().GetEnumerator();

            tf = false;
            while e.MoveNext()
                tf = contains(char(e.Current.GetPropertyValue('PNPDeviceID')), 'VID_16C0', 'IgnoreCase', true);
                if tf, return, end
            end
        end

        function select_target_device(obj, PortName)
            if ispc
                try
                    % On PC, ensure that HW v2 is not initially selected if
                    % Teensy is detected on the first COM port displayed
                    if obj.PortType(1) == 1
                        if ~obj.is_teensy_port(PortName)
                            set(obj.gui.Devices, 'Value', 1);
                        else
                            set(obj.gui.Devices, 'Value', 2);
                        end
                    end
                catch
                    % Fail silently, this is a nice-to-have
                end
            elseif isunix && ~ismac
                % On Linux, likewise: Pulse Pal 3's firmware for a Teensy, or a Teensy in its bootloader (listed by
                % tycmd as SER#...), and Pulse Pal 2's for another device (an Arduino Due)
                try
                    isTeensy = strncmp(PortName, 'SER#', 4) || LoadPulsePalFirmware.is_teensy_port_linux(PortName);
                    if isTeensy
                        set(obj.gui.Devices, 'Value', 2);
                    else
                        set(obj.gui.Devices, 'Value', 1);
                    end
                catch
                    % Fail silently, this is a nice-to-have
                end
            end
        end

        function update_target_hw(obj)
            newPort = obj.gui.Ports;
            obj.select_target_device(newPort.String{newPort.Value});
            obj.update_versions();
        end

        function uploadPort = enter_due_bootloader(obj, targetPort)
            before = serialportlist("available");
            try
                s = serialport(targetPort, 1200);
                pause(0.1);
                clear s
                pause(1);
            catch
                system(['@mode ' char(targetPort) ':1200,N,8,1']);
            end

            uploadPort = targetPort;
            t0 = tic;
            while toc(t0) < 8
                pause(0.1);
                if isunix && ~ismac
                    % Linux usually gives the bootloader the port's name again, so it is found by its USB ID:
                    % SAM-BA's, 03eb:6124 (the Due itself is 2341:003e)
                    ports = dir('/dev/ttyACM*');
                    for i = 1:numel(ports)
                        port = fullfile('/dev', ports(i).name);
                        try
                            if strcmp(LoadPulsePalFirmware.usb_id_linux(port), '03eb:6124')
                                uploadPort = port;
                                return
                            end
                        catch
                            % The port went away between the list and the read
                        end
                    end
                    continue
                end
                nowPorts = serialportlist("available");
                added = setdiff(nowPorts, before);

                if ~isempty(added)
                    uploadPort = char(added(1));
                    return
                end
            end
        end

        function clear_obj(obj)
            delete(obj.gui.Fig);
            evalin('base', 'clear ans');
        end
    end

    methods (Static, Access = private)
        function tf = is_teensy_port_linux(port)
            % True if the port belongs to a Teensy: its USB vendor ID is PJRC's, 16c0
            tf = strncmpi(LoadPulsePalFirmware.usb_id_linux(port), '16c0:', 5);
        end

        function id = usb_id_linux(port)
            % The USB vendor and product IDs of a port's device, as 'vvvv:pppp'. sysfs links
            % /sys/class/tty/ttyACM0/device to the port's USB interface, whose parent folder is the USB device.
            [~, name] = fileparts(char(port));
            deviceFolder = fullfile('/sys/class/tty', name, 'device', '..');
            id = [strtrim(fileread(fullfile(deviceFolder, 'idVendor'))) ':' ...
                  strtrim(fileread(fullfile(deviceFolder, 'idProduct')))];
        end

        function found = wait_for_usb_id_linux(port, id, timeout)
            % Waits up to timeout seconds for a port to belong to the USB device with the given ID
            found = false;
            t0 = tic;
            while ~found && toc(t0) < timeout
                pause(0.1);
                try
                    found = strcmp(LoadPulsePalFirmware.usb_id_linux(port), id);
                catch
                    % The port is not there yet
                end
            end
        end

        function fit_window_to_zoom(fig)
            % Enlarges a window by the desktop zoom, before it is shown. On Linux, MATLAB R2025b is scaled for a
            % high resolution screen by its Zoom setting (s.matlab.desktop.Zoom, a percentage). A figure is then laid
            % out and drawn zoomed, but its window opens at the size its Position names in screen pixels: at 200%,
            % the 855 x 200 loader opened 428 x 100, showing only the bottom of the firmware list, and it cannot be
            % resized. These windows have no toolbar, and their controls keep their pixel positions, so the window
            % only needs enlarging. Pulse Pal's parameter editor (@PulsePalDevice/gui.m) does the same.
            % warndlg shows its window before it returns, and the window reports its zoomed size about 0.1 s later:
            % called at once, this still reads the size warndlg chose (5 times in 5 at 200%), and sets it right.
            % Called later, it would leave the dialog as small as it was.
            if ~strcmp(computer('arch'), 'glnxa64')
                return
            end
            try
                s = settings;
                zoomFactor = s.matlab.desktop.Zoom.ActiveValue / 100;
            catch
                return % A MATLAB without the Zoom setting
            end
            fig.Position(3:4) = round(fig.Position(3:4) * zoomFactor);
        end
    end
end
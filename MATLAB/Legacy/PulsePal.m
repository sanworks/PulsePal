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
function PulsePal(varargin)

if nargin == 1
    TargetPort = varargin{1};
end
% Determine if using Octave
if (exist('OCTAVE_VERSION'))
  UsingOctave = 1;
else
  UsingOctave = 0;
end

% Add Pulse Pal folders to path
PulsePalPath = fileparts(which('PulsePal'));
Folder1 = fullfile(PulsePalPath, 'User Functions');
Folder2 = fullfile(PulsePalPath, 'Accessory Functions');
Folder3 = fullfile(PulsePalPath, 'Interface');
Folder4 = fullfile(PulsePalPath, 'GUI');
Folder5 = fullfile(PulsePalPath, 'Media');
Folder6 = fullfile(PulsePalPath, 'Programs');
addpath(Folder1, Folder2, Folder3, Folder4, Folder5, Folder6);
try
    evalin('base', 'PulsePalSystem;');
    disp('Pulse Pal is already open. Close it with EndPulsePal first.');
catch
    if ~UsingOctave
      ClosePreviousPulsePalInstances;
    end
    global PulsePalSystem;
    if ~UsingOctave
      if exist('rng','file') == 2
          rng('shuffle', 'twister'); % Seed the random number generator by CPU clock
      else
          rand('twister', sum(100*fliplr(clock))); % For older versions of MATLAB
      end
    end
    UsingObject = 0;
    if ~UsingOctave
      if ~verLessThan('matlab', '7.6.0')
          evalin('base','global PulsePalSystem;');
          evalin('base','PulsePalSystem = PulsePalObject;');
          UsingObject = 1;
      end
    end
    if ~UsingObject
        % Initialize empty fields
        PulsePalSystem = struct;
        PulsePalSystem.GUIHandles = struct;
        PulsePalSystem.Graphics = struct;
        PulsePalSystem.LastProgramSent = [];
        PulsePalSystem.PulsePalPath = [];
        PulsePalSystem.SerialPort = [];
        PulsePalSystem.CurrentProgram = [];
        PulsePalSystem.UsingOctave = UsingOctave;
    end
    PulsePalSystem.Params = DefaultPulsePalParameters;
    PulsePalSystem.PulsePalPath = PulsePalPath;
    PulsePalSystem.ParamNames = {'IsBiphasic' 'Phase1Voltage' 'Phase2Voltage' 'Phase1Duration' 'InterPhaseInterval' 'Phase2Duration'...
        'InterPulseInterval' 'BurstDuration' 'InterBurstInterval' 'PulseTrainDuration' 'PulseTrainDelay'...
        'LinkTriggerChannel1' 'LinkTriggerChannel2' 'CustomTrainID' 'CustomTrainTarget' 'CustomTrainLoop' 'RestingVoltage'};
    if ~UsingOctave
      PulsePalSystem.OS = strtrim(system_dependent('getos'));
    else
      PulsePalSystem.OS = ''; % Only used to avoid a communication problem with MATLAB on WinXP, unnecessary for octave
    end
    PulsePalSystem.OpMenuByte = 213;
    if (nargin == 0) && (strcmp(PulsePalSystem.OS, 'Microsoft Windows XP'))
        error('Error: On Windows XP, please specify a serial port. For instance, if Pulse Pal is on port COM3, use: PulsePal(''COM3'')');
    end
    if (nargin == 0) && UsingOctave
        error('Error: On Octave, please specify a serial port. For instance, if Pulse Pal is on port COM3, use: PulsePal(''COM3'')');
    end
    try
        % Connect to hardware
        if nargin > 1
            PulsePalSerialInterface('init', varargin{1}, varargin{2});
        elseif nargin > 0
            PulsePalSerialInterface('init', varargin{1});
        else
            PulsePalSerialInterface('init');
        end
        if UsingOctave
            SendClientIDString('OCTAVE');
        else
            SendClientIDString('MATLAB');
        end
        pause(.1);
        SetPulsePalVersion;
    catch
        if ~UsingOctave
            evalin('base','delete(PulsePalSystem)')
        end
        evalin('base','clear PulsePalSystem')
        rethrow(lasterror)
        msgbox('Error: Unable to connect to Pulse Pal.', 'Modal')
    end
end


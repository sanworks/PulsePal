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

% This function generates a waveform that can be uploaded to Pulse Pal with:
% SendCustomWaveform(Channel, SamplingFrequency, Wave).
% The waveform can then be looped until PulseTrainDuration by setting the "CustomTrainLoop"
% parameter for an output channel that uses it.
% Input arguments:
% Frequency (Hz)
% Amplitude (V, peak to peak)
% SamplingFrequency (Hz, optional 3rd argument)

function Wave = GenerateZeroPhaseSineWave(Frequency, Amplitude, varargin)
global PulsePalSystem
if nargin > 2
    SamplingFrequency = varargin{3};
else
    SamplingFrequency = PulsePalSystem.CycleFrequency/2;
end 
Wave = sin(pi:((2*pi)/(SamplingFrequency/Frequency)):3*pi)*Amplitude;
if length(Wave) > 1000
    error('Pulse Pal has insufficient memory to store one iteration of this wave.')
end
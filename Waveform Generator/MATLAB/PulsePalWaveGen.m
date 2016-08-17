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
classdef PulsePalWaveGen < handle
    properties
        Port % ArCOM Serial port
        waveform = 'sine'; % sine, sawtooth, triangle, square, noise, custom
        frequency = 100; % Hz
        amplitude = 1; % V (peak to peak)
        duration = 0.1; % Seconds
        playbackMode = 'triggered'; % triggered, continuous, gated
        LEDMode = 'off'; % off, on
        activeOutputChannel = 'all'; % 1, 2, 3, 4, 'all'
        customWaveform = []; % 1xN waveform (Volts)
        customWaveformSF = 10000; % Hz
    end
    methods
        function obj = PulsePalWaveGen(portString)
            if nargin == 0
                error('Specify a serial port to create a PulsePalWaveGen object, i.e. ''COM3'', ''tty.usbmodem1411''');
            end
            obj.Port = ArCOMObject_WaveGen(portString, 115200);
            updateParameters(obj);
        end
        
        function trigger(obj) % Soft-trigger playback
            obj.Port.write('T', 'uint8');
        end
        
        function set.playbackMode(obj, mode)
            switch lower(mode)
                case 'triggered' 
                case 'continuous'
                case 'gated'
                otherwise
                    error ('Error: valid playbackMode values are: ''continuous'', ''triggered'' or ''gated''');
            end
            obj.playbackMode = mode;
            updatePreferences(obj);
        end
        
        function set.LEDMode(obj, mode)
            switch lower(mode)
                case 'on' 
                case 'off'
                otherwise
                    error ('Error: valid LEDMode values are: ''on'' or ''off''');
            end
            obj.LEDMode = mode;
            updatePreferences(obj);
        end
        
        function set.activeOutputChannel(obj, ch)
            if ischar(ch)
                if ~strcmp(ch, 'all')
                    error('Error: valid activeOutputChannel values are: 1, 2, 3, 4 or ''all''');
                end
            elseif (ch < 1) || (ch > 4)
                error('Error: valid activeOutputChannel values are: 1, 2, 3, 4 or ''all''');
            end
            obj.activeOutputChannel = ch;
            updatePreferences(obj);
        end
        
        function set.frequency(obj, freq)
            if (freq > 15000) || (freq < 1)
                error ('Error: Frequency out of range. The valid range is 1 - 15,000 Hz');
            end
            obj.frequency = freq;
            updateParameters(obj);
        end
        
        function set.amplitude(obj, amp)
            if (amp > 20) || (amp < .01)
                error ('Error: Amplitude out of range. The valid range is 0.01 - 20V (peak to peak)');
            end
            obj.amplitude = amp;
            updateParameters(obj);
        end
        
        function set.duration(obj, dur)
            if (dur > 3600) || (dur < 0.0001)
                error ('Error: Duration out of range. The valid range is 0.0001 - 3600 seconds');
            end
            obj.duration = dur;
            updateParameters(obj);
        end
        
        function set.waveform(obj, waveform)
            switch lower(waveform)
                case 'sine'
                case 'sawtooth'
                case 'triangle'
                case 'square'
                case 'noise'
                case 'custom'
                    if isempty(obj.customWaveform)
                        error ('Error: No custom waveform specified.') 
                    end
                otherwise
                   error ('Error: Invalid waveform. Valid waveforms are: sine, sawtooth, triangle, square, noise and custom.') 
            end
            obj.waveform = waveform;
            updateParameters(obj);
        end
        
        function set.customWaveformSF(obj, sf)
            if sf > 60000 || sf < 1
                error ('Error: Sampling frequency must be between 1 and 50,000Hz.');
            end
            obj.Port.write('F', 'uint8', sf, 'uint16');
            obj.customWaveformSF = sf;
        end
        
        function set.customWaveform(obj, waveform)
            if ~isnumeric(waveform)
                error ('Error: The custom waveform must be a vector of voltages');
            end
            nSamples = length(waveform);
            if nSamples > 40000
                error ('Error: The custom waveform cannot be more than 40,000 samples');
            end
            [a,b] = size(waveform);
            if min(a,b) ~= 1
                ('Error: The waveform must be a 1xN vector of samples');
            end
            if a > b
                waveform = waveform';
            end
            if max(waveform) > 10 || min(waveform) < -10
                error ('Error: A sample in the waveform was out of range (-10V to +10V)');
            end
            obj.customWaveform = waveform;
            obj.waveform = 'custom';
            updateCustomWaveform(obj)
        end
    end
    methods (Access = private)
        function updateCustomWaveform(obj)
            nSamples = length(obj.customWaveform);
            WaveBits = ceil(((obj.customWaveform+10)/20)*(2^(16)-1));
            obj.Port.write('C', 'uint8', [obj.customWaveformSF nSamples WaveBits], 'uint16');
        end
        
        function updateParameters(obj)
            switch lower(obj.waveform)
                case 'sine'
                    waveCode = 1;
                case 'sawtooth'
                    waveCode = 2;
                case 'triangle'
                    waveCode = 3;
                case 'square'
                    waveCode = 4;
                case 'noise'
                    waveCode = 5;
                otherwise
                    waveCode = 0; % Custom
            end
            if waveCode > 0
                obj.Port.write(['P' waveCode], 'uint8', [round(obj.amplitude*(32767/10)) obj.frequency], 'uint16', round(obj.duration*1000000), 'uint32');
            else
                updateCustomWaveform(obj);
            end
        end
        
        function updatePreferences(obj)
            switch lower(obj.playbackMode)
                case 'continuous'
                    playbackMode = 0;
                case 'triggered' 
                    playbackMode = 1;
                case 'gated'
                    playbackMode = 2;
            end
            switch lower(obj.LEDMode)
                case 'on' 
                    ledMode = 1;
                case 'off'
                    ledMode = 0;
            end
            switch lower(obj.activeOutputChannel)
                case 1
                    activeOutputChannel = 0;
                case 2
                    activeOutputChannel = 1;
                case 3
                    activeOutputChannel = 2;
                case 4
                    activeOutputChannel = 3;
                case 'all'
                    activeOutputChannel = 4;
            end
            obj.Port.write(['S' playbackMode ledMode activeOutputChannel], 'uint8');
        end
    end
end
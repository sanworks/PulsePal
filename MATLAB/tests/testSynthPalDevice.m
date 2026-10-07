% testSynthPalDevice tests the SynthPalDevice class on a connected Synth Pal.
%
% Usage: add /MATLAB to the MATLAB path, then run testSynthPalDevice('COM3'), with the device's port.
% From a terminal: matlab -batch "addpath('MATLAB', 'MATLAB/tests'); testSynthPalDevice('COM3')"
%
% The device must be a Pulse Pal 3 running Synth Pal firmware. The outputs play waveforms of up to +/-10 V, so
% disconnect anything that should not receive them. Takes about 15 seconds.
%
% The device counts the samples each channel plays and sums their DAC codes. The tests compare these with values
% worked out here from the settings, independently of SynthPalDevice, so a mistake in how it encodes frequencies,
% voltages or durations fails: play durations in samples, the resting voltage's DAC code (the mean of any whole
% number of cycles), the codes of a square wave's high half (resting voltage plus half the amplitude), and the code
% of a fixed voltage.
% /Python/PulsePal/tests/synthpal_hardware_test.py tests the firmware's synthesis itself, code by code.

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

function testSynthPalDevice(portString)
tests = {@testConnectionAndDefaults, @testSamplesPerCycle, @testPlayDurationsAreExactInSamples, ...
    @testRestingVoltageIsTheMean, @testAmplitudeOfASquareWave, @testOutputRanges, @testFixedVoltage, ...
    @testInfiniteDurationAndStop, ...
    @testSettingsChangeDuringPlayback, @testInvalidArgumentsAreRefused};
S = SynthPalDevice(portString);
nFailed = 0;
for i = 1:numel(tests)
    testName = func2str(tests{i});
    startTime = tic;
    try
        tests{i}(S);
        result = 'ok';
    catch err
        nFailed = nFailed + 1;
        result = ['FAILED: ' err.message];
        S.stop();
    end
    fprintf('%-42s %6.1f s  %s\n', testName, toc(startTime), result);
end
S.setDefaults();
delete(S);
% This test needs the port to itself, so it runs last
startTime = tic;
try
    testOtherClassesNameTheFirmware(portString);
    result = 'ok';
catch err
    nFailed = nFailed + 1;
    result = ['FAILED: ' err.message];
end
fprintf('%-42s %6.1f s  %s\n', 'testOtherClassesNameTheFirmware', toc(startTime), result);
nTests = numel(tests) + 1;
fprintf('\n%d/%d tests passed\n', nTests - nFailed, nTests);
if nFailed > 0
    error('testSynthPalDevice:failed', '%d test(s) failed.', nFailed);
end
end

function testConnectionAndDefaults(S)
assert(S.info.firmwareVersion == 1, 'firmware version');
assert(S.info.hardwareVersion == 3, 'hardware version');
assert(S.info.minFrequency == 1 && S.info.maxFrequency == 20000, 'frequency limits');
assert(S.info.maxPlayDuration == 3600, 'maximum play duration');
assert(S.frequency == 100 && S.samplesPerCycle == 1000 && S.samplingRate == 100000, 'default frequency');
assert(isequal(S.waveform, {'Sine', 'Sine', 'Sine', 'Sine'}), 'default waveform');
assert(isequal(S.amplitude, [5 5 5 5]) && isequal(S.restingVoltage, [0 0 0 0]), 'default levels');
assert(isequal(S.playDuration, [1 1 1 1]), 'default play duration');
assert(isequal(S.triggerMode, {'Normal', 'Normal'}), 'default trigger mode');
assert(isequal(S.linkTriggerChannel1, true(1,4)) && isequal(S.linkTriggerChannel2, false(1,4)), 'default links');
status = S.status();
assert(isempty(status.playing), 'a channel is playing after connecting');
assert(isequal(status.outputRanges, {'-5V:5V', '-5V:5V', '-5V:5V', '-5V:5V'}), 'default output ranges');
end

function testSamplesPerCycle(S)
% The largest multiple of 4 whose sampling rate is at most 100 kHz
frequencies = [1 100 300 333.33 1.555 12600 20000];
expected = [100000 1000 332 300 4*floor(2500000/156) 4 4];
for i = 1:numel(frequencies)
    S.frequency = frequencies(i);
    assert(S.samplesPerCycle == expected(i), '%g Hz: %d samples per cycle, expected %d', ...
        frequencies(i), S.samplesPerCycle, expected(i));
    assert(S.status().samplesPerCycle == expected(i), 'status() disagrees');
end
assert(S.frequency == 20000, 'frequency');
S.frequency = 1.555; % Rounded to 0.01 Hz, a half to the even hundredth
assert(S.frequency == 1.56, 'frequency rounding');
end

function testPlayDurationsAreExactInSamples(S)
frequencies = [100 300 12345.67 1];
durations = [1 0.12345 0.25 0.00001];
for i = 1:numel(frequencies)
    S.frequency = frequencies(i);
    S.playDuration(4) = durations(i);
    S.play(4);
    waitUntilStopped(S, 4, durations(i) + 2);
    checkPlayed(S, 4, expectedSamples(S, durations(i)));
end
end

function testRestingVoltageIsTheMean(S)
% Over whole cycles the codes average to the resting voltage's code, in each channel's range
S.frequency = 1000;
restingVoltages = [2.5 -2.25 6 -7.5];
amplitudes = [4 3 6 5];
for i = 1:4
    setLevels(S, i, amplitudes(i), restingVoltages(i));
end
S.waveform = {'Sine', 'Triangle', 'Square', 'Sawtooth'};
S.playDuration = 0.02; % 20 cycles
S.play(1:4);
waitUntilStopped(S, 1:4, 2);
[samplesPlayed, sums] = S.playbackChecksums();
for i = 1:4
    assert(samplesPlayed(i) == 2000, 'channel %d played %d samples', i, samplesPlayed(i));
    limits = rangeLimits(restingVoltages(i), amplitudes(i));
    restCode = (restingVoltages(i) - limits(1))/(limits(2) - limits(1))*65536;
    meanCode = sums(i)/2000;
    assert(abs(meanCode - restCode) <= 0.5, 'channel %d: mean code %.3f, resting voltage code %.3f', ...
        i, meanCode, restCode);
end
end

function testAmplitudeOfASquareWave(S)
% The first half of a square wave's cycle is the resting voltage plus half the amplitude
S.frequency = 500; % 200 samples per cycle
setLevels(S, 2, 3.3, -1);
S.waveform{2} = 'Square';
S.playDuration(2) = 100/S.samplingRate;
S.play(2);
waitUntilStopped(S, 2, 1);
limits = rangeLimits(-1, 3.3);
highCode = round((-1 + 3.3/2 - limits(1))/(limits(2) - limits(1))*65536);
checkPlayed(S, 2, 100, 100*highCode);
end

function testOutputRanges(S)
S.frequency = 1000;
cases = {2, 2.5, '0V:5V'; 8, 5, '0V:10V'; 1, 0, '-5V:5V'; 2, -4, '-5V:5V'; 12, 0, '-10V:10V'; 0.5, 9.75, '0V:10V'};
for i = 1:size(cases, 1)
    setLevels(S, 3, cases{i,1}, cases{i,2});
    ranges = S.status().outputRanges;
    assert(strcmp(ranges{3}, cases{i,3}), 'amplitude %g V at %g V: range %s, expected %s', ...
        cases{i,1}, cases{i,2}, ranges{3}, cases{i,3});
end
end

function testFixedVoltage(S)
% A fixed voltage plays its amplitude's code, in the range that holds it and the resting voltage, for its play
% duration. Only a fixed voltage takes a negative amplitude.
S.frequency = 1000; % 100 kHz
cases = {-2.5, 1, '-5V:5V'; 4, 0, '0V:5V'; 9.99, -3, '-10V:10V'; 0, 7, '0V:10V'};
for i = 1:size(cases, 1)
    [fixedVoltage, restingVoltage] = cases{i, 1:2};
    setChannel(S, 3, 'Fixed Voltage', fixedVoltage, restingVoltage);
    ranges = S.status().outputRanges;
    assert(strcmp(ranges{3}, cases{i,3}), 'fixed voltage %g V at %g V: range %s, expected %s', ...
        fixedVoltage, restingVoltage, ranges{3}, cases{i,3});
    limits = sscanf(strrep(cases{i,3}, 'V', ''), '%f:%f')';
    fixedCode = min(round((fixedVoltage - limits(1))/(limits(2) - limits(1))*65536), 65535);
    S.playDuration(3) = 150/S.samplingRate;
    S.play(3);
    waitUntilStopped(S, 3, 1);
    checkPlayed(S, 3, 150, 150*fixedCode);
end
setChannel(S, 3, 'Fixed Voltage', -2.5, 0);
amplitudes = S.amplitude;
expectError(@() setProperty(S, 'waveform', 'Sine')); % A sine wave of -2.5 V
expectError(@() setProperty(S, 'amplitude', [1 1 10.5 1])); % Beyond 10 V
expectError(@() setProperty(S, 'amplitude', [-1 1 1 1])); % Channel 1 plays a sine wave
assert(strcmp(S.waveform{3}, 'Fixed Voltage') && isequal(S.amplitude, amplitudes), 'a refused setting was changed');
setChannel(S, 3, 'Sine', 5, 0);
end

function testInfiniteDurationAndStop(S)
S.frequency = 1000;
S.playDuration(1) = 0;
S.play(1);
pause(0.3);
assert(ismember(1, S.status().playing), 'channel 1 stopped by itself');
S.play(1); % Ignored while it plays
pause(0.05);
[samplesPlayed, ~] = S.playbackChecksums();
assert(samplesPlayed(1) > 30000, 'the second trigger restarted channel 1');
S.stop(1);
pause(0.01);
assert(~ismember(1, S.status().playing), 'channel 1 did not stop');
S.playDuration(1) = 1;
end

function testSettingsChangeDuringPlayback(S)
% A channel playing 1 s at 100 Hz changes frequency, waveform and levels: it plays on, and stops after 1 s
S.frequency = 100;
setLevels(S, 4, 2, 0);
S.playDuration(4) = 1;
startTime = tic;
S.play(4);
pause(0.3);
S.frequency = 1000;
S.waveform{4} = 'Triangle';
setLevels(S, 4, 16, 1);
assert(ismember(4, S.status().playing), 'channel 4 stopped');
waitUntilStopped(S, 4, 2);
elapsed = toc(startTime);
assert(elapsed > 0.95 && elapsed < 1.2, 'channel 4 played for %.3f s', elapsed);
checkPlayed(S, 4, expectedSamples(S, 1));
end

function testInvalidArgumentsAreRefused(S)
% Each must raise an error without changing the setting
setLevels(S, 1, 2, 9);
expectError(@() setProperty(S, 'frequency', 0.99));
expectError(@() setProperty(S, 'frequency', 20000.01));
expectError(@() setProperty(S, 'frequency', NaN));
expectError(@() setProperty(S, 'waveform', 'Ramp'));
expectError(@() setProperty(S, 'waveform', {'Sine', 'Sine'}));
expectError(@() setProperty(S, 'amplitude', [3 1 1 1])); % 9 V + 1.5 V on channel 1
expectError(@() setProperty(S, 'amplitude', 20.1));
expectError(@() setProperty(S, 'restingVoltage', [-9.5 0 0 0])); % -9.5 V - 1 V on channel 1
expectError(@() setProperty(S, 'restingVoltage', 10.5));
expectError(@() setProperty(S, 'playDuration', -1));
expectError(@() setProperty(S, 'playDuration', 3600.5));
expectError(@() setProperty(S, 'triggerMode', 'Master'));
expectError(@() setProperty(S, 'triggerMode', {'Normal', 'Normal', 'Normal', 'Normal'}));
expectError(@() setProperty(S, 'linkTriggerChannel1', [1 0 2 0]));
expectError(@() S.play([1 7]));
expectError(@() S.play([]));
expectError(@() S.setScreenSaver(2));
assert(S.amplitude(1) == 2 && S.restingVoltage(1) == 9, 'a refused level was changed');
assert(isequal(S.triggerMode, {'Normal', 'Normal'}), 'a refused trigger mode was changed');
setLevels(S, 1, 5, 0);
end

function testOtherClassesNameTheFirmware(portString)
% PulsePalDevice and WavePalDevice, given this Synth Pal, must say that the device runs Synth Pal firmware
classes = {@PulsePalDevice, @WavePalDevice};
for i = 1:numel(classes)
    connected = false;
    try
        device = classes{i}(portString); %#ok<NASGU>
        connected = true;
    catch err
        assert(contains(err.message, 'runs Synth Pal firmware'), 'unexpected error: %s', err.message);
    end
    assert(~connected, '%s connected to a Synth Pal', func2str(classes{i}));
end
end

% --- Helpers ---

function setLevels(S, channel, amplitude, restingVoltage)
% Sets a channel's amplitude and resting voltage in an order the device accepts from any earlier levels
S.amplitude(channel) = 0;
S.restingVoltage(channel) = restingVoltage;
S.amplitude(channel) = amplitude;
end

function setChannel(S, channel, waveform, amplitude, restingVoltage)
% Sets a channel's waveform and levels in an order the device accepts from any earlier settings: an amplitude of 0
% goes with any waveform and resting voltage
S.amplitude(channel) = 0;
S.waveform{channel} = waveform;
S.restingVoltage(channel) = restingVoltage;
S.amplitude(channel) = amplitude;
end

function limits = rangeLimits(restingVoltage, amplitude)
% The output range the device should choose, as in "Output ranges" in /Firmware/SynthPal/PROTOCOL.md
low = restingVoltage - amplitude/2;
high = restingVoltage + amplitude/2;
if low >= 0 && high <= 5
    limits = [0 5];
elseif low >= 0 && high <= 10
    limits = [0 10];
elseif low >= -5 && high <= 5
    limits = [-5 5];
else
    limits = [-10 10];
end
end

function n = expectedSamples(S, duration)
% A play duration in samples at the current sampling rate, as the device rounds it
n = max(1, round(round(duration*1e6)*S.samplingRate/1e6));
end

function checkPlayed(S, channel, nExpected, expectedSum)
[samplesPlayed, sums] = S.playbackChecksums();
assert(samplesPlayed(channel) == nExpected, 'channel %d played %d samples, expected %d', ...
    channel, samplesPlayed(channel), nExpected);
if nargin > 3
    assert(sums(channel) == mod(expectedSum, 2^32), 'channel %d: the sum of the samples played is %d, expected %d', ...
        channel, sums(channel), mod(expectedSum, 2^32));
end
end

function waitUntilStopped(S, channels, timeout)
startTime = tic;
while any(ismember(channels, S.status().playing))
    if toc(startTime) > timeout
        error('channels %s still playing after %g s', mat2str(channels), timeout);
    end
    pause(0.02);
end
end

function expectError(func)
try
    func();
catch
    return
end
error('no error was raised by %s', func2str(func));
end

function setProperty(S, name, value)
S.(name) = value;
end

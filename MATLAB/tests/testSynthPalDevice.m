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
% voltages or durations fails: play durations in samples, the mean voltage's DAC code (the mean of any whole
% number of cycles), the codes of a square wave's high half (mean voltage plus half the peak to peak voltage), the code of a
% fixed voltage, and playbacks lengthened by their ramps.
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
    @testMeanVoltageIsTheMean, @testAmplitudeOfASquareWave, @testOutputRanges, @testFixedVoltage, @testWhiteNoise, ...
    @testWaveformChangesFindAnAcceptedOrder, @testConfigureSetsLevelsTogether, ...
    @testRampsLengthenPlayback, @testInfiniteDurationAndStop, ...
    @testSettingsChangeDuringPlayback, @testSyncToDevice, @testSyncFromDeviceReadsWhatTheDevicePlays, ...
    @testScreenSaverSettings, @testParamSyncStoresTheSet, @testInvalidArgumentsAreRefused};
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
S.setDefaultParams();
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
assert(isequal(S.peakToPeak, [5 5 5 5]) && isequal(S.fixedVoltage, [5 5 5 5]) && ...
    isequal(S.restingVoltage, [0 0 0 0]) && isequal(S.meanVoltage, [0 0 0 0]), 'default levels');
assert(isequal(S.playDuration, [1 1 1 1]), 'default play duration');
assert(isequal(S.onRampDuration, [0 0 0 0]) && isequal(S.offRampDuration, [0 0 0 0]), 'default ramps');
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
    S.trigger(4);
    waitUntilStopped(S, 4, durations(i) + 2);
    checkPlayed(S, 4, expectedSamples(S, durations(i)));
end
end

function testMeanVoltageIsTheMean(S)
% Over whole cycles the codes average to the mean voltage's code, in each channel's range, whatever the resting voltage
S.frequency = 1000;
meanVoltages = [2.5 -2.25 6 -7.5];
restingVoltages = [0 1 -3 -7.5];
peakToPeaks = [4 3 6 5];
for i = 1:4
    setLevels(S, i, peakToPeaks(i), meanVoltages(i), restingVoltages(i));
end
S.waveform = {'Sine', 'Triangle', 'Square', 'Sawtooth'};
S.playDuration(:) = 0.02; % 20 cycles
S.trigger(1:4);
waitUntilStopped(S, 1:4, 2);
[samplesPlayed, sums] = S.playbackChecksums();
for i = 1:4
    assert(samplesPlayed(i) == 2000, 'channel %d played %d samples', i, samplesPlayed(i));
    limits = rangeLimits(meanVoltages(i), peakToPeaks(i), restingVoltages(i));
    meanVoltageCode = (meanVoltages(i) - limits(1))/(limits(2) - limits(1))*65536;
    meanCode = sums(i)/2000;
    assert(abs(meanCode - meanVoltageCode) <= 0.5, 'channel %d: mean code %.3f, mean voltage code %.3f', ...
        i, meanCode, meanVoltageCode);
end
end

function testWhiteNoise(S)
% White noise has no model of its samples, so its statistics are checked, through the sums of the codes played: their
% mean is the mean voltage's exact code, within 5 standard errors of noise uniform over the peak to peak voltage. Noise
% of 0 V peak to peak plays the mean voltage's code exactly. Every playback, and every channel, plays new noise.
S.frequency = 100; % 1000 samples per cycle: 100 kHz
setChannel(S, 1, 'White Noise', 4, 0, 1); % -1 V to 3 V: the -5 V to 5 V range
setChannel(S, 2, 'White Noise', 4, 0, 1);
setChannel(S, 3, 'White Noise', 0, 0, 1); % The 0 V to 5 V range
S.playDuration(1:3) = 0.5;
S.trigger(1:3);
waitUntilStopped(S, 1:3, 2);
[samplesPlayed, sums] = S.playbackChecksums();
n = expectedSamples(S, 0.5);
meanCode = (1 + 5)/10*65536;
sigma = sqrt(n)*(2/10*65536)/sqrt(3); % Standard deviation of the sum: n samples uniform over +/- 2 V
for i = 1:2
    total = sums(i) + round((n*meanCode - sums(i))/2^32)*2^32; % The sums wrap at 2^32
    assert(samplesPlayed(i) == n && abs(total - n*meanCode) <= 5*sigma, ...
        'channel %d: mean code %.2f, mean voltage''s code %.2f', i, total/n, meanCode);
end
assert(sums(1) ~= sums(2), 'channels 1 and 2 played the same noise');
checkPlayed(S, 3, n, n*13107); % 1 V in the 0 V to 5 V range: code 13107.2
first = sums(1);
S.trigger(1);
waitUntilStopped(S, 1, 2);
[~, sums] = S.playbackChecksums();
assert(sums(1) ~= first, 'channel 1 played the same noise twice');
S.setDefaultParams();
end

function testAmplitudeOfASquareWave(S)
% The first half of a square wave's cycle is the mean voltage plus half the peak to peak voltage
S.frequency = 500; % 200 samples per cycle
setLevels(S, 2, 3.3, -1);
S.waveform{2} = 'Square';
S.playDuration(2) = 100/S.samplingRate;
S.trigger(2);
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
    assert(strcmp(ranges{3}, cases{i,3}), '%g V peak to peak around %g V: range %s, expected %s', ...
        cases{i,1}, cases{i,2}, ranges{3}, cases{i,3});
end
end

function testFixedVoltage(S)
% A fixed voltage plays its own code, in the range that holds it and the resting voltage, for its play duration
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
    S.trigger(3);
    waitUntilStopped(S, 3, 1);
    checkPlayed(S, 3, 150, 150*fixedCode);
end
setChannel(S, 3, 'Fixed Voltage', -2.5, 0);
fixedVoltages = S.fixedVoltage;
expectError(@() setProperty(S, 'fixedVoltage', [1 1 10.5 1])); % Beyond 10 V
expectError(@() setProperty(S, 'peakToPeak', [-1 1 1 1])); % Peak to peak voltages are 0 to 20 V
assert(strcmp(S.waveform{3}, 'Fixed Voltage') && isequal(S.fixedVoltage, fixedVoltages), 'a refused setting was changed');
setChannel(S, 3, 'Sine', 5, 0);
end

function testWaveformChangesFindAnAcceptedOrder(S)
% A sine wave of 20 V peak to peak cannot become a fixed voltage of -5 V by either op first: the device would refuse
% a fixed voltage of 20 V, and a sine wave of -5 V peak to peak. The class goes through 0 V, and back.
S.frequency = 1000; % 100 kHz
S.configure(4, 'waveform', 'Sine', 'peakToPeak', 20, 'meanVoltage', 0, 'restingVoltage', 0, 'fixedVoltage', -5);
S.waveform{4} = 'Fixed Voltage';
assert(strcmp(S.status().outputRanges{4}, '-5V:5V'), 'channel 4 does not hold a fixed voltage of -5 V');
S.playDuration(4) = 100/S.samplingRate;
S.trigger(4);
waitUntilStopped(S, 4, 1);
checkPlayed(S, 4, 100, 0); % Code 0: -5 V in the -5 V to 5 V range
S.waveform{4} = 'Sine';
assert(strcmp(S.status().outputRanges{4}, '-10V:10V'), 'channel 4 does not hold a sine wave of 20 V peak to peak');
setChannel(S, 4, 'Sine', 5, 0);
S.playDuration(4) = 1;
end

function testConfigureSetsLevelsTogether(S)
% From 2 V peak to peak around 9 V to 20 V around 0 V needs the mean voltage first, and back needs the peak to peak
% voltage first: configure() works out the order. Settings that do not go together are refused, and nothing changes.
S.configure(2, 'peakToPeak', 2, 'meanVoltage', 9, 'restingVoltage', 9);
assert(strcmp(S.status().outputRanges{2}, '0V:10V'), '2 V around 9 V not in the 0 V to 10 V range');
S.configure(2, 'peakToPeak', 20, 'meanVoltage', 0, 'restingVoltage', 0);
assert(strcmp(S.status().outputRanges{2}, '-10V:10V'), '20 V around 0 V not in the -10 V to 10 V range');
S.configure([2 3], 'peakToPeak', [2 4], 'meanVoltage', [9 -1], 'restingVoltage', [9 -1]);
assert(isequal(S.peakToPeak(2:3), [2 4]) && isequal(S.meanVoltage(2:3), [9 -1]), 'configure() of two channels');
expectError(@() S.configure(2, 'peakToPeak', 20)); % 9 V + 10 V
expectError(@() S.configure(2, 'amplitude', 1)); % Not a setting
expectError(@() S.configure([2 3], 'peakToPeak', [1 2 3]));
expectError(@() S.configure(5, 'peakToPeak', 1));
assert(S.peakToPeak(2) == 2 && S.meanVoltage(2) == 9, 'a refused configure() changed a setting');
setLevels(S, 2, 5, 0);
setLevels(S, 3, 5, 0);
end

function testRampsLengthenPlayback(S)
% From a trigger to rest takes the on ramp, the play duration and the off ramp, each in samples
S.frequency = 1000; % 100 kHz
setChannel(S, 2, 'Triangle', 6, -1, 2);
ramps = [0.0123 0.0071; 0.00001 0; 0 0.00001; 0.02 0.03];
for i = 1:size(ramps, 1)
    S.onRampDuration(2) = ramps(i, 1);
    S.playDuration(2) = 0.005;
    S.offRampDuration(2) = ramps(i, 2);
    S.trigger(2);
    waitUntilStopped(S, 2, 2);
    checkPlayed(S, 2, expectedSamples(S, ramps(i, 1), true) + 500 + expectedSamples(S, ramps(i, 2), true));
end
S.onRampDuration(2) = 0;
S.offRampDuration(2) = 0;
setChannel(S, 2, 'Sine', 5, 0);
end

function testInfiniteDurationAndStop(S)
S.frequency = 1000;
S.playDuration(1) = 0;
S.trigger(1);
pause(0.3);
assert(ismember(1, S.status().playing), 'channel 1 stopped by itself');
S.trigger(1); % Ignored while it plays
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
S.trigger(4);
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

function testSyncToDevice(S)
% With autoSync off, settings change only the object's copy, and syncToDevice() sends them all: with no trigger
% channel in param sync mode, the device applies them at once
S.frequency = 1000;
S.autoSync = false;
cleanup = onCleanup(@() setProperty(S, 'autoSync', true)); %#ok<NASGU> Also if the test fails
S.frequency = 2000;
S.waveform{1} = 'Fixed Voltage';
S.fixedVoltage(1) = 2;
S.restingVoltage(1) = 0.5;
S.playDuration(1) = 0.005;
assert(S.status().samplesPerCycle == 100, 'a setting reached the device before syncToDevice()');
assert(S.samplesPerCycle == 48, 'samplesPerCycle %d, expected 48', S.samplesPerCycle);
S.syncToDevice();
status = S.status();
assert(status.samplesPerCycle == 48 && strcmp(status.outputRanges{1}, '0V:5V'), 'the set was not applied');
S.trigger(1);
waitUntilStopped(S, 1, 1);
checkPlayed(S, 1, 480, 480*round(2/5*65536)); % 5 ms at 96 kHz, at the fixed voltage's code
S.autoSync = true;
setChannel(S, 1, 'Sine', 5, 0);
S.playDuration(1) = 1;
end

function testSyncFromDeviceReadsWhatTheDevicePlays(S)
% syncFromDevice() reads every setting back, here after the object's own copy was changed with autoSync off, so that it
% differs from what the device holds. Assignments then plan from the levels read back.
S.frequency = 440.5;
S.configure(1, 'waveform', 'Triangle', 'peakToPeak', 4, 'meanVoltage', 1);
S.configure(2, 'waveform', 'Fixed Voltage', 'fixedVoltage', -2.5, 'meanVoltage', 6);
S.restingVoltage(3) = -1;
S.playDuration(4) = 0.25;
S.onRampDuration(1) = 0.01;
S.offRampDuration(2) = 0.02;
S.linkTriggerChannel2(3) = true;
S.triggerMode{2} = 'Toggle';
expected = S.exportParams();
S.autoSync = false;
cleanup = onCleanup(@() setProperty(S, 'autoSync', true)); % Also if the test fails
S.frequency = 1000;
S.configure(1, 'waveform', 'Sine', 'peakToPeak', 2, 'meanVoltage', 0);
S.restingVoltage(3) = 0;
S.playDuration(4) = 1;
S.linkTriggerChannel2(3) = false;
S.triggerMode{2} = 'Normal';
S.autoSync = true;
S.syncFromDevice();
assert(isequal(S.exportParams(), expected), 'the settings read back differ from those programmed');
% From a fixed voltage of -2.5 V with a mean of 6 V, straight to a sine wave of 20 V peak to peak
S.configure(2, 'waveform', 'Sine', 'peakToPeak', 20, 'meanVoltage', 0);
S.syncFromDevice();
assert(strcmp(S.waveform{2}, 'Sine') && S.peakToPeak(2) == 20 && S.meanVoltage(2) == 0, 'levels after read-back');
clear cleanup
S.setDefaultParams();
end

function testScreenSaverSettings(S)
% Op 99 takes the screen saver's state and timeout. Left on, with 1800 s: a new device's settings.
S.setScreenSaver(false);
S.setScreenSaver(true, 1800);
expectError(@() S.setScreenSaver(2));
expectError(@() S.setScreenSaver(true, 0));
expectError(@() S.setScreenSaver(true, 65536));
end

function testParamSyncStoresTheSet(S)
% While a trigger channel is in param sync mode, syncToDevice() stores the set for its next rising edge: the device
% plays on with its settings. Leaving the mode discards the set.
S.frequency = 1000;
setChannel(S, 1, 'Fixed Voltage', 1, 0);
S.playDuration(1) = 0.005;
S.triggerMode{2} = 'Param Sync';
S.autoSync = false;
cleanup = onCleanup(@() setProperty(S, 'autoSync', true)); %#ok<NASGU> Also if the test fails
S.fixedVoltage(1) = 3;
S.frequency = 2000;
S.syncToDevice();
assert(S.status().samplesPerCycle == 100, 'the stored set was applied');
S.trigger(1);
waitUntilStopped(S, 1, 1);
checkPlayed(S, 1, 500, 500*round(1/5*65536)); % Still the fixed voltage of 1 V, at 100 kHz
S.autoSync = true;
S.triggerMode{2} = 'Normal'; % Discards the stored set
S.triggerMode{2} = 'Param Sync';
assert(S.status().samplesPerCycle == 100, 'a discarded set was applied');
S.triggerMode{2} = 'Normal';
S.frequency = 1000; % The object's copy held the discarded set
setChannel(S, 1, 'Sine', 5, 0);
S.playDuration(1) = 1;
end

function testInvalidArgumentsAreRefused(S)
% Each must raise an error without changing the setting
setLevels(S, 1, 2, 9);
expectError(@() setProperty(S, 'frequency', 0.99));
expectError(@() setProperty(S, 'frequency', 20000.01));
expectError(@() setProperty(S, 'frequency', NaN));
expectError(@() setProperty(S, 'waveform', {'Ramp', 'Sine', 'Sine', 'Sine'}));
expectError(@() setProperty(S, 'waveform', {'Sine', 'Sine'}));
expectError(@() setProperty(S, 'peakToPeak', [3 1 1 1])); % 9 V + 1.5 V on channel 1
expectError(@() setProperty(S, 'peakToPeak', [5 5 5 20.1]));
expectError(@() setProperty(S, 'fixedVoltage', [5 5 5 -10.1]));
expectError(@() setProperty(S, 'meanVoltage', [-9.5 0 0 0])); % -9.5 V - 1 V on channel 1
expectError(@() setProperty(S, 'meanVoltage', [9 0 0 10.5]));
expectError(@() setProperty(S, 'restingVoltage', [9 0 0 10.5]));
expectError(@() setProperty(S, 'playDuration', [1 1 1 -1]));
expectError(@() setProperty(S, 'playDuration', [1 1 1 3600.5]));
expectError(@() setProperty(S, 'onRampDuration', [0 0 0 -0.001]));
expectError(@() setProperty(S, 'offRampDuration', [0 0 0 3600.5]));
expectError(@() setProperty(S, 'triggerMode', {'Master', 'Normal'}));
% A single value does not say which channels it is meant for
expectError(@() setProperty(S, 'waveform', 'Square'));
expectError(@() setProperty(S, 'playDuration', 1));
expectError(@() setProperty(S, 'triggerMode', 'Normal'));
expectError(@() setProperty(S, 'triggerMode', {'Normal', 'Normal', 'Normal', 'Normal'}));
expectError(@() setProperty(S, 'linkTriggerChannel1', [1 0 2 0]));
expectError(@() S.trigger([1 7]));
expectError(@() S.trigger([]));
expectError(@() setProperty(S, 'autoSync', 2));
expectError(@() setProperty(S, 'triggerMode', {'Sync', 'Normal'}));
S.autoSync = false;
expectError(@() setProperty(S, 'peakToPeak', [3 1 1 1])); % Checked with autoSync off too
S.autoSync = true;
assert(S.peakToPeak(1) == 2 && S.meanVoltage(1) == 9 && S.restingVoltage(1) == 9, 'a refused level was changed');
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

function setLevels(S, channel, peakToPeak, meanVoltage, restingVoltage)
% Sets a channel's peak to peak, mean and resting voltage (the mean voltage, unless given)
if nargin < 5
    restingVoltage = meanVoltage;
end
S.configure(channel, 'peakToPeak', peakToPeak, 'meanVoltage', meanVoltage, 'restingVoltage', restingVoltage);
end

function setChannel(S, channel, waveform, level, restingVoltage, meanVoltage)
% Sets a channel's waveform and levels. level is the peak to peak voltage of a periodic waveform, whose mean voltage is
% the resting voltage unless given, or the voltage of a fixed voltage.
if strcmp(waveform, 'Fixed Voltage')
    S.configure(channel, 'waveform', waveform, 'fixedVoltage', level, 'restingVoltage', restingVoltage);
else
    if nargin < 6
        meanVoltage = restingVoltage;
    end
    S.configure(channel, 'waveform', waveform, 'peakToPeak', level, 'meanVoltage', meanVoltage, ...
                'restingVoltage', restingVoltage);
end
end

function limits = rangeLimits(meanVoltage, amplitude, restingVoltage)
% The output range the device should choose, as in "Output ranges" in /Firmware/SynthPal/PROTOCOL.md: the first that
% holds the waveform and the resting voltage (the mean voltage, unless given)
if nargin < 3
    restingVoltage = meanVoltage;
end
low = min(restingVoltage, meanVoltage - amplitude/2);
high = max(restingVoltage, meanVoltage + amplitude/2);
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

function n = expectedSamples(S, duration, zeroIsZero)
% A play duration in samples at the current sampling rate, as the device rounds it. A nonzero duration lasts at least
% one sample. With zeroIsZero (for a ramp), 0 stays 0.
n = max(1, round(round(duration*1e6)*S.samplingRate/1e6));
if nargin > 2 && zeroIsZero && duration == 0
    n = 0;
end
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

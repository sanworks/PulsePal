% testWavePalDevice tests the WavePalDevice class on a connected Wave Pal.
%
% Usage: add /MATLAB to the MATLAB path, then run testWavePalDevice('COM3'), with the device's port.
% From a terminal: matlab -batch "addpath('MATLAB', 'MATLAB/tests'); testWavePalDevice('COM3')"
%
% The device must be a Pulse Pal 3 running Wave Pal firmware. The outputs play random waveforms of up to
% +/-10 V, so disconnect anything that should not receive them. Takes about 20 seconds.
%
% Every sample the device plays is checked: its firmware sums the DAC codes each channel plays, and each test
% compares the sums with the waveforms it loaded. The expected DAC codes are computed here, independently of
% WavePalDevice, so a mistake in its voltage encoding also fails. /Python/PulsePal/tests/wavepal_hardware_test.py
% tests the firmware itself more thoroughly.

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

function testWavePalDevice(portString)
tests = {@testConnectionAndDefaults, @testWaveformsPlayExactly, @testLoopDurationIsExactInSamples, ...
    @testSamplingRateResendsLoopDurations, @testTriggerModes, @testOutputRangeChangeReloadsWaveforms, ...
    @testEveryOutputRangePlays, @testFixedVoltageAndStop, @testInvalidArgumentsAreRefused, @testFullLengthWaveform};
rng(1);
W = WavePalDevice(portString);
nFailed = 0;
for i = 1:numel(tests)
    testName = func2str(tests{i});
    startTime = tic;
    try
        tests{i}(W);
        result = 'ok';
    catch err
        nFailed = nFailed + 1;
        result = ['FAILED: ' err.message];
        W.stop();
    end
    fprintf('%-42s %6.1f s  %s\n', testName, toc(startTime), result);
end
delete(W);
% This test needs the port to itself, so it runs last
startTime = tic;
try
    testPulsePalDeviceNamesTheFirmware(portString);
    result = 'ok';
catch err
    nFailed = nFailed + 1;
    result = ['FAILED: ' err.message];
end
fprintf('%-42s %6.1f s  %s\n', 'testPulsePalDeviceNamesTheFirmware', toc(startTime), result);
nTests = numel(tests) + 1;
% Leave the device without the test waveforms. A range change unloads every waveform from the device, and a new
% WavePalDevice has no waveforms of its own to load again.
W = WavePalDevice(portString);
W.outputRange = '-5V:5V';
W.outputRange = '-10V:10V';
delete(W);
fprintf('\n%d/%d tests passed\n', nTests - nFailed, nTests);
if nFailed > 0
    error('testWavePalDevice:failed', '%d test(s) failed.', nFailed);
end
end

function testConnectionAndDefaults(W)
assert(W.info.firmwareVersion == 1, 'firmware version');
assert(W.info.hardwareVersion == 3, 'hardware version');
assert(W.info.maxSamples == 1000000, 'maximum samples');
assert(W.samplingRate == 10000 && strcmp(W.outputRange, '-10V:10V'), 'default sampling rate or range');
assert(isequal(W.loopMode, false(1,4)) && isequal(W.loopDuration, zeros(1,4)), 'default loop settings');
assert(isequal(W.triggerMode, {'Normal', 'Normal', 'Normal', 'Normal'}), 'default trigger mode');
assert(isequal(W.linkTriggerChannel1, true(1,4)) && isequal(W.linkTriggerChannel2, false(1,4)), 'default links');
assert(isempty(W.status().playing), 'a channel is playing after connecting');
end

function testWaveformsPlayExactly(W)
% Lengths at and around the device's buffer size: short waveforms play from RAM, longer ones from the card
W.samplingRate = 100000;
bufferSamples = W.info.bufferSamples;
for nSamples = [1, bufferSamples, bufferSamples + 1, 3*bufferSamples + 7]
    codes = loadRandomWaveform(W, 1, nSamples);
    W.play(1);
    waitUntilStopped(W, 1, 5);
    checkPlayed(W, 1, codes, nSamples);
end
end

function testLoopDurationIsExactInSamples(W)
W.samplingRate = 100000;
codes = loadRandomWaveform(W, 2, 1000);
W.loopMode(2) = true;
W.loopDuration(2) = 0.12345;
cleanup = onCleanup(@() resetLoops(W));
W.play(2);
waitUntilStopped(W, 2, 2);
checkPlayed(W, 2, codes, 12345);
end

function testSamplingRateResendsLoopDurations(W)
% The device counts loop durations in samples, so the class sends them again when the rate changes
W.samplingRate = 20000;
codes = loadRandomWaveform(W, 3, 700);
W.loopMode(3) = true;
W.loopDuration(3) = 0.5;
cleanup = onCleanup(@() resetLoops(W));
W.samplingRate = 50000;
W.play(3);
waitUntilStopped(W, 3, 2);
checkPlayed(W, 3, codes, 25000);
assert(abs(W.actualSamplingRate - 50000) < 1e-9, 'actualSamplingRate at 50 kHz');
W.samplingRate = 44100;
assert(abs(W.actualSamplingRate - 24e6/544) < 1e-9, 'actualSamplingRate at 44.1 kHz');
end

function testTriggerModes(W)
W.samplingRate = 10000;
loadRandomWaveform(W, 1, 100000); % 10 s
cleanup = onCleanup(@() setNormalMode(W));
W.triggerMode{1} = 'normal'; % A second trigger is ignored. Names are not case sensitive.
assert(strcmp(W.triggerMode{1}, 'Normal'), 'trigger mode name not normalized');
W.play(1);
pause(0.5);
W.play(1);
pause(0.2);
samplesPlayed = W.playbackChecksums();
assert(samplesPlayed(1) > 6000, 'normal mode restarted the waveform');
W.triggerMode{1} = 'Master'; % A second trigger restarts it
W.play(1);
pause(0.2);
samplesPlayed = W.playbackChecksums();
assert(samplesPlayed(1) < 4000, 'master mode did not restart the waveform');
W.triggerMode = {'Toggle', 'Normal', 'Normal', 'Normal'}; % A second trigger stops it
W.play(1);
pause(0.05);
assert(~ismember(1, W.status().playing), 'toggle mode did not stop the waveform');
W.play(1);
pause(0.05);
assert(ismember(1, W.status().playing), 'toggle mode did not start the waveform');
W.stop(1);
assert(isempty(W.status().playing), 'stop(1) did not stop channel 1');
end

function testOutputRangeChangeReloadsWaveforms(W)
W.samplingRate = 100000;
lengths = [5000, 40000, 1, 20000];
for i = 1:4
    loadRandomWaveform(W, i, lengths(i), -4, 4); % Every waveform must fit the new range
end
W.outputRange = '-5V:5V';
cleanup = onCleanup(@() setRange(W, '-10V:10V'));
assert(isequal(W.status().samplesLoaded, lengths), 'the waveforms were not loaded again');
for i = 1:4
    codes = expectedCodes(W.waveforms{i}, [-5 5]);
    W.play(i);
    waitUntilStopped(W, i, 3);
    checkPlayed(W, i, codes, numel(codes));
end
end

function testEveryOutputRangePlays(W)
W.samplingRate = 100000;
for i = 1:4
    W.loadWaveform(i, 0); % 0 V fits every range, so no range change is refused
end
cleanup = onCleanup(@() setRange(W, '-10V:10V'));
for i = 1:4
    W.outputRange = W.info.outputRanges{i};
    limits = rangeLimits(W.outputRange);
    codes = loadRandomWaveform(W, 1, 20000, limits(1), limits(2));
    W.play(1);
    waitUntilStopped(W, 1, 3);
    checkPlayed(W, 1, codes, numel(codes));
    W.loadWaveform(1, 0);
end
end

function testFixedVoltageAndStop(W)
W.setFixedVoltage([1 3], 2.5);
assert(isempty(W.status().playing), 'a fixed voltage started playback');
W.stop();
end

function testInvalidArgumentsAreRefused(W)
% Each must raise an error without changing the setting
expectError(@() W.loadWaveform(0, [1 2]));
expectError(@() W.loadWaveform(5, [1 2]));
expectError(@() W.loadWaveform(1, []));
expectError(@() W.loadWaveform(1, [0 10.01])); % Outside -10 V to 10 V
expectError(@() W.loadWaveform(1, [0 NaN]));
expectError(@() W.play([1 7]));
expectError(@() W.play([]));
expectError(@() W.setFixedVoltage(1, 11));
expectError(@() setProperty(W, 'samplingRate', 100001));
expectError(@() setProperty(W, 'samplingRate', 44100.5));
expectError(@() setProperty(W, 'outputRange', '0V:12V'));
expectError(@() setProperty(W, 'loopMode', [true false]));
expectError(@() setProperty(W, 'loopMode', 2));
expectError(@() setProperty(W, 'loopDuration', -1));
expectError(@() setProperty(W, 'triggerMode', 'Restart'));
expectError(@() setProperty(W, 'linkTriggerChannel1', [1 0 2 0]));
assert(W.samplingRate ~= 100001 && ~any(W.loopMode) && strcmp(W.outputRange, '-10V:10V'), ...
    'a refused setting was changed');
% A loaded waveform that does not fit a new range is refused, and nothing changes
W.loadWaveform(2, [-1 7]);
expectError(@() setProperty(W, 'outputRange', '0V:10V'));
assert(strcmp(W.outputRange, '-10V:10V'), 'the range changed');
end

function testFullLengthWaveform(W)
W.samplingRate = 100000;
volts = -10 + 20*rand(1, W.info.maxSamples);
startTime = tic;
W.loadWaveform(1, volts);
loadTime = toc(startTime);
codes = expectedCodes(volts, [-10 10]);
underrunsBefore = W.status().underruns;
W.play(1);
waitUntilStopped(W, 1, 15);
checkPlayed(W, 1, codes, numel(codes));
assert(isequal(W.status().underruns, underrunsBefore), 'underruns during playback');
fprintf('    load 1M samples from MATLAB: %.2f s, %.2f MB/s\n', loadTime, 2*numel(volts)/loadTime/1e6);
end

function testPulsePalDeviceNamesTheFirmware(portString)
% PulsePalDevice, given this Wave Pal, must say that the device runs Wave Pal firmware
try
    P = PulsePalDevice(portString); %#ok<NASGU>
catch err
    assert(contains(err.message, 'runs Wave Pal firmware'), 'unexpected error: %s', err.message);
    return
end
error('PulsePalDevice connected to a Wave Pal');
end

% --- Helpers ---

function codes = loadRandomWaveform(W, channel, nSamples, low, high)
if nargin < 4
    low = -10;
    high = 10;
end
volts = low + (high - low)*rand(1, nSamples);
W.loadWaveform(channel, volts);
codes = expectedCodes(volts, rangeLimits(W.outputRange));
end

function limits = rangeLimits(rangeName)
% The limits of an output range in volts, as in /Firmware/WavePal/PROTOCOL.md
switch rangeName
    case '0V:5V'
        limits = [0 5];
    case '0V:10V'
        limits = [0 10];
    case '-5V:5V'
        limits = [-5 5];
    case '-10V:10V'
        limits = [-10 10];
end
end

function codes = expectedCodes(volts, limits)
% The DAC codes the device should hold for these voltages, computed independently of WavePalDevice
codes = round((volts - limits(1))/(limits(2) - limits(1))*65535);
end

function checkPlayed(W, channel, codes, nExpected)
[samplesPlayed, sums] = W.playbackChecksums();
assert(samplesPlayed(channel) == nExpected, 'channel %d played %d samples, expected %d', ...
    channel, samplesPlayed(channel), nExpected);
codes = double(codes);
nLoops = floor(nExpected/numel(codes));
remainder = mod(nExpected, numel(codes));
expectedSum = mod(nLoops*sum(codes) + sum(codes(1:remainder)), 2^32);
assert(sums(channel) == expectedSum, 'channel %d: the sum of the samples played is %d, expected %d', ...
    channel, sums(channel), expectedSum);
end

function waitUntilStopped(W, channels, timeout)
startTime = tic;
while any(ismember(channels, W.status().playing))
    if toc(startTime) > timeout
        error('channels %s still playing after %g s', mat2str(channels), timeout);
    end
    pause(0.05);
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

function setProperty(W, name, value)
W.(name) = value;
end

function resetLoops(W)
W.loopMode = false;
W.loopDuration = 0;
end

function setNormalMode(W)
W.stop();
W.triggerMode = 'Normal';
end

function setRange(W, rangeName)
for i = 1:4
    W.loadWaveform(i, 0); % 0 V fits every range, so the range change cannot be refused
end
W.outputRange = rangeName;
end

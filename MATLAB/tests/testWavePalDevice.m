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
% tests the firmware itself more thoroughly, including the trigger modes with TTL edges.

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
    @testSamplingRateResendsLoopDurations, @testHalfwayValuesRoundAsInPython, @testTriggerModes, @testOneWaveformOnSeveralChannels, ...
    @testCurrentWaveformChangesAtTheNextStart, @testParamSyncWaitsForAnEdge, ...
    @testOutputRangeChangeReloadsWaveforms, @testEveryOutputRangePlays, @testFixedVoltageAndStop, ...
    @testScreenSaverAndExportParams, @testImportParams, @testInvalidArgumentsAreRefused, @testFullLengthWaveform};
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
assert(W.info.circuitBoardRevision >= 0 && W.info.circuitBoardRevision <= 31, 'hardware revision');
assert(~isempty(W.info.serialNumber) && all(isstrprop(W.info.serialNumber, 'digit')), 'USB serial number');
assert(W.info.maxSamples == 1000000, 'maximum samples');
assert(W.info.nWaveforms == 4 && isequal(W.waveforms, cell(1,4)), 'number of waveforms');
assert(W.samplingRate == 10000 && strcmp(W.outputRange, '-10V:10V'), 'default sampling rate or range');
assert(isequal(W.currentWaveform, zeros(1,4)), 'default current waveforms');
assert(isequal(W.status().currentWaveform, zeros(1,4)), 'the device has current waveforms after connecting');
assert(isequal(W.loopMode, false(1,4)) && isequal(W.loopDuration, zeros(1,4)), 'default loop settings');
assert(isequal(W.triggerMode, {'Normal', 'Normal'}), 'default trigger mode');
assert(isequal(W.linkTriggerChannel1, false(1,4)) && isequal(W.linkTriggerChannel2, false(1,4)), 'default links');
assert(isempty(W.status().playing), 'a channel is playing after connecting');
end

function testWaveformsPlayExactly(W)
% Lengths at and around the device's buffer size: short waveforms play from RAM, longer ones from the card
W.samplingRate = 100000;
bufferSamples = W.info.bufferSamples;
for nSamples = [1, bufferSamples, bufferSamples + 1, 3*bufferSamples + 7]
    codes = loadRandomWaveform(W, 1, nSamples);
    W.trigger(1);
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
W.trigger(2);
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
W.trigger(3);
waitUntilStopped(W, 3, 2);
checkPlayed(W, 3, codes, 25000);
assert(abs(W.actualSamplingRate - 50000) < 1e-9, 'actualSamplingRate at 50 kHz');
W.samplingRate = 44100;
assert(abs(W.actualSamplingRate - 24e6/544) < 1e-9, 'actualSamplingRate at 44.1 kHz');
end

function testHalfwayValuesRoundAsInPython(W)
% DAC codes and loop durations exactly halfway between two round to the even one, as in the Python class; MATLAB's
% round() would take them up. The sample clock divider is the firmware's, which rounds up.
W.samplingRate = 10000;
W.outputRange = '-10V:10V';
volts = [-9.501792935072862 -9.501182574196994 -9.500572213321126]; % Encode to 1632.5, 1634.5 and 1636.5
codes = expectedCodes(volts, [-10 10]);
assert(isequal(codes, [1632 1634 1636]), 'expectedCodes() does not round halfway values to even');
W.loadWaveform(1, volts, 1);
W.trigger(1);
waitUntilStopped(W, 1, 1);
checkPlayed(W, 1, codes, 3);
cleanup = onCleanup(@() resetLoops(W));
W.loopMode(1) = true;
W.loopDuration(1) = 0.00025; % 2.5 samples at 10 kHz
W.trigger(1);
waitUntilStopped(W, 1, 1);
checkPlayed(W, 1, codes, 2);
W.samplingRate = 3072; % 24 MHz / 3072 Hz is 7812.5 cycles, which the firmware rounds up to 7813
assert(abs(W.actualSamplingRate - 24e6/7813) < 1e-9, 'actualSamplingRate at 3072 Hz');
end

function testTriggerModes(W)
% Trigger modes belong to the trigger channels, as in PulsePalDevice. Whatever the mode, a soft trigger starts an idle
% channel and is ignored by a playing one. The modes' effect on TTL edges needs a TTL source: see
% /Python/PulsePal/tests/wavepal_hardware_test.py --driver.
W.samplingRate = 10000;
loadRandomWaveform(W, 1, 100000); % 10 s
cleanup = onCleanup(@() setNormalMode(W));
W.triggerMode{1} = 'toggle'; % Names are not case sensitive
assert(isequal(W.triggerMode, {'Toggle', 'Normal'}), 'trigger mode name not normalized');
W.triggerMode = {'Gated', 'Master'};
assert(isequal(W.triggerMode, {'Gated', 'Master'}), 'trigger modes not set');
for mode = W.info.triggerModes
    W.triggerMode(:) = mode;
    W.trigger(1);
    pause(0.5);
    W.trigger(1);
    pause(0.2);
    samplesPlayed = W.playbackChecksums();
    assert(samplesPlayed(1) > 6000, 'a soft trigger in %s mode restarted the waveform', mode{1});
    assert(ismember(1, W.status().playing), 'a soft trigger in %s mode stopped the waveform', mode{1});
    W.stop(1);
    assert(isempty(W.status().playing), 'stop(1) did not stop channel 1');
end
end

function testOneWaveformOnSeveralChannels(W)
% A waveform loaded once plays on every channel it is given to
W.samplingRate = 100000;
codes = loadRandomWaveform(W, 2, 3*W.info.bufferSamples + 11, -10, 10, 1:4);
assert(isequal(W.currentWaveform, [2 2 2 2]), 'currentWaveform after loadWaveform with channels');
assert(isequal(W.status().currentWaveform, [2 2 2 2]), 'the device''s current waveforms');
W.trigger(1:4);
waitUntilStopped(W, 1:4, 3);
for i = 1:4
    checkPlayed(W, i, codes, numel(codes));
end
end

function testCurrentWaveformChangesAtTheNextStart(W)
% A channel that is playing finishes its waveform; the new current waveform plays from its next start. The playback
% buffers then hold chunks of the first waveform, which must not be played as the second's.
W.samplingRate = 100000;
codes1 = loadRandomWaveform(W, 1, 5*W.info.bufferSamples, -10, 10, 1); % 0.8 s
codes2 = loadRandomWaveform(W, 3, 4*W.info.bufferSamples + 3);
W.trigger(1);
pause(0.3);
W.currentWaveform(1) = 3;
waitUntilStopped(W, 1, 3);
checkPlayed(W, 1, codes1, numel(codes1));
W.trigger(1);
waitUntilStopped(W, 1, 3);
checkPlayed(W, 1, codes2, numel(codes2));
end

function testParamSyncWaitsForAnEdge(W)
% In param sync mode, a load waits for an edge on the param sync channel; with nothing connected to it, the edge
% never comes. Leaving the mode makes the stored load and current waveforms current, as an edge would.
W.samplingRate = 100000;
W.currentWaveform = zeros(1,4);
cleanup = onCleanup(@() setNormalMode(W));
oldCodes = loadRandomWaveform(W, 1, 3*W.info.bufferSamples, -10, 10, 1);
W.triggerMode{2} = 'Param Sync';
W.trigger(1);
newVolts = -10 + 20*rand(1, 2*W.info.bufferSamples + 5);
W.loadWaveform(1, newVolts, [1 2]);
newCodes = expectedCodes(newVolts, [-10 10]);
deviceStatus = W.status();
assert(isequal(deviceStatus.playing, 1), 'a param sync load stopped the channel playing the old waveform');
assert(isequal(deviceStatus.storedWaveforms, 1) && isequal(deviceStatus.storedCurrentWaveform, [1 1 0 0]), ...
    'the load and current waveforms were not stored for an edge');
assert(deviceStatus.samplesLoaded(1) == numel(oldCodes) && isequal(deviceStatus.currentWaveform, [1 0 0 0]), ...
    'the stored load or current waveforms were applied without an edge');
waitUntilStopped(W, 1, 3);
checkPlayed(W, 1, oldCodes, numel(oldCodes));
W.trigger(1);
waitUntilStopped(W, 1, 3);
checkPlayed(W, 1, oldCodes, numel(oldCodes)); % Until an edge, triggers play the old waveform
expectError(@() setProperty(W, 'outputRange', '-5V:5V')); % It would reload the waveforms, which would wait too
W.triggerMode{2} = 'Normal';
deviceStatus = W.status();
assert(isempty(deviceStatus.storedWaveforms) && isempty(deviceStatus.storedCurrentWaveform), ...
    'leaving param sync mode left a load stored');
assert(deviceStatus.samplesLoaded(1) == numel(newCodes) && isequal(deviceStatus.currentWaveform, [1 1 0 0]), ...
    'leaving param sync mode did not make the stored load current');
W.trigger([1 2]);
waitUntilStopped(W, [1 2], 3);
checkPlayed(W, 1, newCodes, numel(newCodes));
checkPlayed(W, 2, newCodes, numel(newCodes));
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
    W.trigger(i);
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
    W.trigger(1);
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

function testScreenSaverAndExportParams(W)
% Op 99 takes the screen saver's state and timeout, as on Pulse Pal and Synth Pal. Left on, with 1800 s: a new device's
% settings. exportParams() holds every setting.
W.setScreenSaver(false);
W.setScreenSaver(true, 1800);
expectError(@() W.setScreenSaver(2));
expectError(@() W.setScreenSaver(true, 0));
expectError(@() W.setScreenSaver(true, 65536));
W.loopMode(2) = true;
W.loopDuration(2) = 0.5;
params = W.exportParams();
assert(isequal(params.loopMode, [false true false false]) && params.loopDuration(2) == 0.5, 'exported loop settings');
assert(params.samplingRate == W.samplingRate && strcmp(params.outputRange, W.outputRange), 'exported device settings');
assert(isequal(fieldnames(params)', {'samplingRate', 'outputRange', 'currentWaveform', 'loopMode', 'loopDuration', ...
    'triggerMode', 'linkTriggerChannel1', 'linkTriggerChannel2'}), 'exported fields');
assert(isequal(params.currentWaveform, W.currentWaveform), 'exported current waveforms');
W.loopMode(2) = false;
W.loopDuration(2) = 0;
end

function testImportParams(W)
% importParams() programs what exportParams() returned, after checking every setting. Imported with a trigger channel
% entering param sync mode, the current waveforms still apply at once: the trigger modes are sent last. A setting that
% that is invalid (a waveform number out of range) changes and sends nothing.
cleanup = onCleanup(@() resetAfterImport(W)); % Also if a check fails
setNormalMode(W);
loadRandomWaveform(W, 2, 100, -1, 1, 2);
W.samplingRate = 25000;
W.loopMode(3) = true;
W.loopDuration(3) = 0.5;
W.currentWaveform = [0 2 2 0];
W.linkTriggerChannel2(4) = true;
W.triggerMode = {'Master', 'Param Sync'};
expected = W.exportParams();
setNormalMode(W);
W.samplingRate = 10000;
resetLoops(W);
W.currentWaveform = zeros(1,4);
W.linkTriggerChannel2(4) = false;
W.importParams(jsondecode(jsonencode(expected))); % As read from a .json file: columns
assert(isequal(W.exportParams(), expected), 'imported settings');
status = W.status();
assert(isequal(status.currentWaveform, [0 2 2 0]), 'the current waveforms waited for an edge');
expectError(@() W.importParams(struct('currentWaveform', [5 0 0 0], 'loopMode', [true true true true])));
assert(isequal(W.exportParams(), expected), 'a refused import changed the settings');
end

function resetAfterImport(W)
setNormalMode(W);
resetLoops(W);
W.samplingRate = 10000;
W.currentWaveform = zeros(1,4);
W.linkTriggerChannel2(4) = false;
end

function testInvalidArgumentsAreRefused(W)
% Each must raise an error without changing the setting
W.loadWaveform(1, [1 2], 1);
W.currentWaveform(2:4) = 0;
expectError(@() W.loadWaveform(0, [1 2]));
expectError(@() W.loadWaveform(5, [1 2]));
expectError(@() W.loadWaveform(1, []));
expectError(@() W.loadWaveform(1, [0 10.01])); % Outside -10 V to 10 V
expectError(@() W.loadWaveform(1, [0 NaN]));
expectError(@() W.loadWaveform(1, [1 2], [0 1]));
expectError(@() W.loadWaveform(1, [1 2], 5));
expectError(@() W.trigger([1 7]));
expectError(@() W.trigger([]));
expectError(@() W.trigger([1 2])); % Channel 2 has no waveform
expectError(@() W.setFixedVoltage(1, 11));
W.loadWaveform(4, 1);
W.outputRange = '-10V:10V'; % Reloads nothing: the same range
expectError(@() setProperty(W, 'currentWaveform', [1 5 0 0]));
expectError(@() setProperty(W, 'currentWaveform', [1 -1 0 0]));
expectError(@() setProperty(W, 'currentWaveform', [1 1.5 0 0]));
expectError(@() setProperty(W, 'currentWaveform', 1)); % A single value does not say which channels it is meant for
assert(isequal(W.currentWaveform, [1 0 0 0]) && isequal(W.status().currentWaveform, [1 0 0 0]), ...
    'a refused currentWaveform was changed');
expectError(@() setProperty(W, 'samplingRate', 100001));
expectError(@() setProperty(W, 'samplingRate', 44100.5));
expectError(@() setProperty(W, 'outputRange', '0V:12V'));
expectError(@() setProperty(W, 'loopMode', [true false]));
expectError(@() setProperty(W, 'loopMode', [2 0 0 0]));
expectError(@() setProperty(W, 'loopDuration', [-1 0 0 0]));
expectError(@() setProperty(W, 'triggerMode', {'Restart', 'Normal'}));
% A single value does not say which channels it is meant for
expectError(@() setProperty(W, 'loopMode', true));
expectError(@() setProperty(W, 'loopDuration', 1));
expectError(@() setProperty(W, 'triggerMode', 'Toggle'));
expectError(@() setProperty(W, 'triggerMode', {'Normal', 'Normal', 'Normal', 'Normal'})); % Two trigger channels
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
W.loadWaveform(1, volts, 1);
loadTime = toc(startTime);
codes = expectedCodes(volts, [-10 10]);
underrunsBefore = W.status().underruns;
W.trigger(1);
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

function codes = loadRandomWaveform(W, waveformNumber, nSamples, low, high, channels)
% Loads random voltages as a waveform, played by the channels given: by default, the channel of the same number
if nargin < 4
    low = -10;
    high = 10;
end
if nargin < 6
    channels = waveformNumber;
end
volts = low + (high - low)*rand(1, nSamples);
W.loadWaveform(waveformNumber, volts, channels);
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
% The DAC codes the device should hold for these voltages, computed independently of WavePalDevice. Halfway values
% round to the even code, as in the Python class (see /AGENTS.md).
exact = (volts - limits(1))/(limits(2) - limits(1))*65535;
codes = floor(exact + 0.5);
halfway = exact - floor(exact) == 0.5;
codes(halfway) = codes(halfway) - mod(codes(halfway), 2); % The odd code above becomes the even one below
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
W.loopMode = false(1,4);
W.loopDuration = zeros(1,4);
end

function setNormalMode(W)
W.stop();
W.triggerMode = {'Normal', 'Normal'};
end

function setRange(W, rangeName)
for i = 1:4
    W.loadWaveform(i, 0); % 0 V fits every range, so the range change cannot be refused
end
W.outputRange = rangeName;
end

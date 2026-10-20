% testPulsePalDeviceOffline tests the bytes the PulsePalDevice class sends, and the values it accepts, without a device:
% SimulatedPulsePalPort records each write and replies as Pulse Pal firmware does. It is the MATLAB counterpart of
% /Python/PulsePal/tests/test_protocol.py.
%
% Usage: from a terminal, at the root of the repository:
%   matlab -batch "addpath('MATLAB', 'MATLAB/tests'); testPulsePalDeviceOffline"
% Takes a few seconds. The expected bytes are built here from /Firmware/PROTOCOL.md, independently of the class.

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

function testPulsePalDeviceOffline()
tests = {@testConnectionProgramsTheDefaults, @testStaleBytesAreDiscarded, @testALateReplyIsSkipped, ...
    @testHandshakeFallsBackToOp72, @testATenMicrosecondCycleConvertsExactly, @testOtherFirmwareVersionsWarn, ...
    @testDefaultParametersMessage, @testSingleValuesAreRefused, @testAssignmentsSendTheChannelsTheyChange, ...
    @testTimesHoldTheValueTheDevicePlays, @testTimesAreAtMost9999s, @testMinimumPulseByHardwareVersion, @testCustomPulseTimesAreMultiplesOfTheMinimumPulse, ...
    @testSettingsFiles, @testCalibrationNeedsPulsePal3, @testFormatWithoutConfirmation, ...
    @testCustomBurstsNeedABurstDuration, @testAnEmptyCustomTrainIsRefused, @testANoReplyMentionsACommFailure, ...
    @testExportAndImportParams, @testProgramFiles, @testSyncFromDeviceStoresWhatTheDeviceHolds, ...
    @testPortAndInfoAreReadOnly, @testMethodsReturnNothing, @testStatus, @testPulsePal2AndFirmwareV21};
nFailed = 0;
for i = 1:numel(tests)
    try
        tests{i}();
        result = 'ok';
    catch err
        nFailed = nFailed + 1;
        result = ['FAILED: ' err.message];
    end
    fprintf('%-46s %s\n', func2str(tests{i}), result);
end
fprintf('\n%d/%d tests passed\n', numel(tests) - nFailed, numel(tests));
if nFailed > 0
    error('testPulsePalDeviceOffline:failed', '%d test(s) failed.', nFailed);
end
end

function testConnectionProgramsTheDefaults()
% Connecting reads the hardware info (op 94), the circuit board revision (op 100) and the USB serial number (op 102),
% takes both trigger channels out of param sync mode (Pulse Pal 3), then sends the defaults in one op 92
port = SimulatedPulsePalPort();
P = PulsePalDevice(port);
w = port.writes;
assert(isequal(w(1:6), {uint8([213 103]), uint8([213 94]), uint8([213 100]), uint8([213 102]), ...
                        uint8([213 89 double('MATLAB')]), uint8([213 91 128 0 0])}), 'connection commands');
assert(numel(w) == 7 && w{7}(2) == 92, 'the defaults are not one op 92');
assert(port.NumBytesAvailable == 0, 'a reply was left unread');
% The fields of the Python class's DeviceInfo, in the same order
assert(isequal(fieldnames(P.info)', {'outputParameterNames', 'triggerModes', ...
    'customTrainTargets', 'firmwareVersion', 'hardwareVersion', 'circuitBoardRevision', 'serialNumber', ...
    'maxCustomPulses', 'nCustomPulseTrains', 'cycleFrequency', 'cyclePeriod_us', 'minPulseWidth_us', 'maxTime'}), ...
    'info fields');
assert(P.info.hardwareVersion == 3 && P.info.circuitBoardRevision == 5, 'hardware revision');
assert(isequal(P.info.serialNumber, '20345420'), 'USB serial number');
assert(P.info.maxTime == 9999.9999 && P.info.minPulseWidth_us == 50, 'info limits');
assert(isequal(P.phase1Voltage, [5 5 5 5]) && isequal(P.triggerMode, {'Normal', 'Normal'}), 'default values');
delete(P);
assert(isequal(port.writes{end}, uint8([213 81])), 'disconnecting did not send op 81');
end

function testStaleBytesAreDiscarded()
% Bytes left unread by an earlier session would otherwise be read as the reply to the handshake
port = SimulatedPulsePalPort();
port.leaveUnread([1 1 0 0 0]);
P = PulsePalDevice(port);
assert(P.info.firmwareVersion == 22, 'stale bytes were read as the handshake');
end

function testALateReplyIsSkipped()
% A command an earlier session sent just before it closed can still be waiting on the device, which answers it after
% the constructor discarded the bytes waiting, and before the handshake. The handshake's reply is the last 5 bytes the
% device sends, so the late reply is skipped, also one that looks like a handshake.
op93Reply = [repmat([20 0 0 0], 1, 8) zeros(1, 146)]; % Starts with 20 cycles: 1 ms
for lateReply = {op93Reply, [75 99 0 0 0], 1}
    port = SimulatedPulsePalPort();
    port.lateReply = uint8(lateReply{1});
    P = PulsePalDevice(port);
    assert(P.info.firmwareVersion == 22 && P.info.hardwareVersion == 3, 'late reply %s', ...
           mat2str(lateReply{1}(1:min(5, end))));
    assert(isequal(port.writes{2}, uint8([213 94])) && port.NumBytesAvailable == 0, 'replies after the handshake');
    delete(P);
end
end

function testHandshakeFallsBackToOp72()
% The class connects with op 103, so that the session's times are in the device's own cycles (10 us on Pulse Pal 3).
% Firmware from before October 2026 ignores op 103, and its times are in 50 us cycles: the class then connects with
% op 72, and takes the cycle that op 94 reports. A 10 us device counts an op 72 session's times in 50 us cycles too.
for cyclePeriod = [50 10]
    port = SimulatedPulsePalPort();
    port.answersOp103 = false;
    port.cyclePeriod = cyclePeriod;
    P = PulsePalDevice(port);
    assert(isequal(port.writes(1:3), {uint8([213 103]), uint8([213 72]), uint8([213 94])}), 'op 72 after op 103');
    assert(P.info.cyclePeriod_us == 50 && P.info.cycleFrequency == 20000, 'the session''s unit is 50 us');
    port.clearWrites();
    P.phase1Duration(1) = 0.0002;
    assert(isequal(port.writes, {uint8([213 74 4 1 typecast(uint32(4), 'uint8')])}), '4 cycles of 50 us');
    delete(P);
end
end

function testATenMicrosecondCycleConvertsExactly()
% Pulse Pal 3 runs a 10 us cycle. The cycle rate is 1e6/period, exactly 100000: 1/(10/1e6) is 99999.99999999999, and
% 10 cycles read back from the device would be 0.00010000000000000002 s
port = SimulatedPulsePalPort();
port.cyclePeriod = 10;
P = PulsePalDevice(port);
assert(P.info.cycleFrequency == 100000 && P.info.cyclePeriod_us == 10, 'cycle rate');
port.clearWrites();
P.phase1Duration(1) = 0.000123; % Rounds to 12 cycles
assert(isequal(port.writes, {uint8([213 74 4 1 typecast(uint32(12), 'uint8')])}), '12 cycles of 10 us');
assert(P.phase1Duration(1) == 0.00012, 'the time the device plays');
P.syncFromDevice(); % Op 93: the last op 92's times (the simulated port does not apply op 74), back to seconds
assert(P.phase1Duration(1) == 0.0001 && P.interPulseInterval(1) == 0.001, 'times read back');
delete(P);
end

function testOtherFirmwareVersionsWarn()
% New firmware only adds commands, so a newer version connects, with a warning, as does v21, which is supported
port = SimulatedPulsePalPort(3, 23);
[id, message] = expectWarning(@() PulsePalDevice(port));
assert(strcmp(id, 'PulsePalDevice:newerFirmware') && contains(message, 'v23'), message);
assert(port.writes{end}(2) == 81, 'the class did not connect to firmware v23');
id = expectWarning(@() PulsePalDevice(SimulatedPulsePalPort(3, 21)));
assert(strcmp(id, 'PulsePalDevice:olderFirmware'), id);
expectError(@() PulsePalDevice(SimulatedPulsePalPort(3, 20)));
end

function testDefaultParametersMessage()
% The whole op 92 message, built from /Firmware/PROTOCOL.md: all four channels of each parameter in turn
[P, port] = connect();
P.syncToDevice();
times = kron([2 2 2 20 0 0 20000 0], ones(1,4)); % Timer cycles of 50 us: the firmware's defaults
voltages = kron([49151 16384 32768], ones(1,4)); % DAC codes of 5 V, -5 V and 0 V, halves to the even code
singleBytes = zeros(1,30); % ... continuous loop, links to trigger 1 and 2 (none by default), trigger modes
expected = uint8([213 92 typecast(uint32(times), 'uint8') typecast(uint16(voltages), 'uint8') singleBytes]);
assert(isequal(port.writes{1}, expected), 'op 92 message');
end

function testSingleValuesAreRefused()
% A single value does not say which channels it is meant for, so assigning a whole parameter takes one value per
% channel, in every class (see "One way to use all six" in /AGENTS.md)
[P, port] = connect();
message = expectError(@() setProperty(P, 'phase1Voltage', 5));
assert(contains(message, 'P.phase1Voltage(1) = 5') && contains(message, 'P.phase1Voltage(:) = 5'), message);
message = expectError(@() setProperty(P, 'triggerMode', 'Toggle'));
assert(contains(message, 'P.triggerMode{1} = ''Toggle'''), message);
expectError(@() setProperty(P, 'isBiphasic', true));
expectError(@() setProperty(P, 'customTrainTarget', 'Pulses'));
expectError(@() setProperty(P, 'triggerMode', 1));
expectError(@() setProperty(P, 'phase1Duration', [0.001 0.001]));
assert(isempty(port.writes), 'a refused value was sent');
P.phase1Voltage(:) = 2.5;
P.triggerMode(:) = {'Toggle'};
assert(isequal(port.writes, {uint8([213 91 2 typecast(uint16([40959 40959 40959 40959]), 'uint8')]), ...
                             uint8([213 91 128 1 1])}), 'P.name(:) = value');
end

function testAssignmentsSendTheChannelsTheyChange()
% An assignment sends the channels whose value it changes: op 74 for each, or op 91 when all of them change. The other
% channels keep what the device holds, e.g. values set with the joystick, which sending all four would undo.
[P, port] = connect();
P.phase1Voltage(2) = 7;
P.phase1Voltage = [6 7 5.00001 4]; % Channels 1 and 4: 5.00001 V is 5 V's DAC code
P.triggerMode{2} = 'Toggle';
P.isBiphasic(3) = true;
assert(isequal(port.writes, {uint8([213 74 2 2 typecast(uint16(55705), 'uint8')]), ...
                             uint8([213 74 2 1 typecast(uint16(52428), 'uint8')]), ...
                             uint8([213 74 2 4 typecast(uint16(45874), 'uint8')]), ...
                             uint8([213 74 128 2 1]), uint8([213 74 1 3 1])}), 'changed channels only');
assert(isequal(P.phase1Voltage, [6 7 5.00001 4]), 'the property does not hold the values assigned');
port.clearWrites();
P.phase1Voltage(2) = 7; % Values the channels already hold send nothing
P.triggerMode{2} = 'toggle';
P.isBiphasic = [false false true false];
assert(isempty(port.writes), 'an unchanged value was sent');
end

function testTimesHoldTheValueTheDevicePlays()
[P, port] = connect();
P.phase1Duration(1) = 0.00012; % 2.4 cycles: plays 100 us
P.pulseTrainDelay(2) = 0.000175; % 3.5 cycles: plays 200 us (halfway, to the even cycle)
assert(P.phase1Duration(1) == 0.0001 && P.pulseTrainDelay(2) == 0.0002, 'the property does not hold the time played');
% 0.00012 s plays as the default 0.0001 s, so it sends nothing
assert(isequal(port.writes{1}, uint8([213 74 11 2 typecast(uint32(4), 'uint8')])), 'op 74 message');
end

function testTimesAreAtMost9999s()
% The longest time the joystick menu shows: 199999998 cycles of 50 us, as in the Python and C++ classes
[P, port] = connect();
P.pulseTrainDuration(1) = 9999.9999;
assert(isequal(port.writes{1}, uint8([213 74 10 1 typecast(uint32(199999998), 'uint8')])), 'op 74 message');
expectError(@() setProperty(P, 'pulseTrainDuration', [10000 1 1 1]));
expectError(@() setProperty(P, 'pulseTrainDelay', [9999.99995 0 0 0])); % 199999999 cycles
assert(isscalar(port.writes), 'a refused time was sent');
end

function testMinimumPulseByHardwareVersion()
% As in the Python class and the joystick menu: phases, the inter-pulse interval and the train duration last at least
% 1 timer cycle (50 us) on Pulse Pal 3, whose trigger channels latch every edge, and 2 (100 us) on Pulse Pal 2, whose
% trigger channels read their inputs once per cycle. Times of 0 still mean "off" where they did.
names = {'phase1Duration', 'phase2Duration', 'interPulseInterval', 'pulseTrainDuration'};
[P, port] = connect(2, 22);
assert(P.info.minPulseWidth_us == 100, 'Pulse Pal 2 minimum');
for i = 1:numel(names)
    expectError(@() setProperty(P, names{i}, [0.00005 0.001 0.001 0.001]));
end
assert(isempty(port.writes), 'a refused time was sent');
P.burstDuration(1) = 0.00005; % Times that may be 0 are not limited
assert(isequal(port.writes{1}, uint8([213 74 8 1 typecast(uint32(1), 'uint8')])), 'Pulse Pal 2 burst duration');
[P, port] = connect();
for i = 1:numel(names)
    values = P.(names{i});
    values(3) = 0.00005;
    P.(names{i}) = values;
    assert(P.(names{i})(3) == 0.00005, '%s holds 50 us', names{i});
    expectError(@() setProperty(P, names{i}, [0 0.001 0.001 0.001]));
end
assert(isequal(port.writes{1}, uint8([213 74 4 3 typecast(uint32(1), 'uint8')])), 'Pulse Pal 3 phase 1 of 1 cycle');
assert(numel(port.writes) == numel(names), 'a refused time was sent');
end

function testCustomPulseTimesAreMultiplesOfTheMinimumPulse()
% As in the Python class: a custom pulse time between two steps of info.minPulseWidth_us (100 us on Pulse Pal 2, 50 us
% on Pulse Pal 3) is refused, not rounded
[P, port] = connect(2, 22);
expectError(@() P.sendCustomPulseTrain(1, [0 0.00012], [1 2]));
expectError(@() P.sendCustomPulseTrain(1, [0 0.00005], [1 2])); % One 50 us cycle
expectError(@() P.sendCustomPulseTrain(1, [0 0.2 0.2], [1 2 3])); % Times must increase
expectError(@() P.sendCustomPulseTrain(1, [0 10000], [1 2]));
expectError(@() P.sendCustomWaveform(1, 0.00015, [1 2 3]));
expectError(@() P.sendCustomWaveform(1, 1, ones(1,5001))); % More samples than Pulse Pal 2 holds
assert(isempty(port.writes), 'a refused train was sent');
P.sendCustomPulseTrain(1, (0:4)*0.0001 + 0.0003, 1:5); % Floating point error in the times is fine
P.sendCustomPulseTrain(2, [0 9999.9999], [1 2]);
P.sendCustomWaveform(1, 0.0002, [1 2 3]);
assert(isequal(port.writes{1}(1:27), uint8([213 95 0 typecast(uint32([5 6 8 10 12 14]), 'uint8')])), 'train 1');
assert(isequal(port.writes{2}(1:15), uint8([213 95 1 typecast(uint32([2 0 199999998]), 'uint8')])), 'train 2');
assert(isequal(port.writes{3}(1:19), uint8([213 95 0 typecast(uint32([3 0 4 8]), 'uint8')])), 'waveform');

[P, port] = connect();
expectError(@() P.sendCustomPulseTrain(1, [0 0.00012], [1 2]));
expectError(@() P.sendCustomPulseTrain(1, [0 0.000075], [1 2]));
expectError(@() P.sendCustomPulseTrain(1, [0 0.2 0.2], [1 2 3])); % Times must increase
expectError(@() P.sendCustomPulseTrain(1, [0 10000], [1 2]));
expectError(@() P.sendCustomWaveform(1, 0.000025, [1 2 3]));
expectError(@() P.sendCustomWaveform(1, 1, ones(1,10001))); % The last sample would start at 10000 s
assert(isempty(port.writes), 'a refused train was sent');
P.sendCustomPulseTrain(1, (0:4)*0.00005 + 0.00015, 1:5); % Floating point error in the times is fine
P.sendCustomPulseTrain(2, [0 9999.99985], [1 2]);
P.sendCustomWaveform(3, 0.00005, [1 2 3]);
assert(isequal(port.writes{1}(1:27), uint8([213 95 0 typecast(uint32([5 3 4 5 6 7]), 'uint8')])), 'train 1');
assert(isequal(port.writes{2}(1:15), uint8([213 95 1 typecast(uint32([2 0 199999997]), 'uint8')])), 'train 2');
assert(isequal(port.writes{3}(1:19), uint8([213 95 2 typecast(uint32([3 0 1 2]), 'uint8')])), 'train 3');
end

function testSettingsFiles()
[P, port] = connect();
P.saveProgramToMicroSD('Protocol1.pps');
P.loadProgramFromMicroSD('ABCDEFGHIJK.PPS'); % 15 characters
P.deleteProgramFromMicroSD("Protocol1.pps");
assert(isequal(port.writes, {uint8([213 90 1 13 double('Protocol1.pps')]), ...
    uint8([213 90 2 15 double('ABCDEFGHIJK.PPS')]), uint8([213 93]), ...
    uint8([213 90 3 13 double('Protocol1.pps')])}), 'op 90 messages');
port.clearWrites();
for name = {'Protocol1', 'Protocol1.pps.txt', 'ABCDEFGHIJKL.pps', '.pps', ['Pr' char(246) 'tocol.pps'], 5}
    expectError(@() P.saveProgramToMicroSD(name{1}));
end
assert(isempty(port.writes), 'a refused file name was sent');
% A refused load changes nothing on the device, so nothing is read back; a refused save or deletion says so
P.phase1Voltage(1) = 2.5;
port.clearWrites();
port.loadReply = 0;
message = expectError(@() P.loadProgramFromMicroSD('MISSING.pps'));
assert(contains(message, 'no such file on its microSD card, or it is not a valid Pulse Pal program'), message);
assert(isscalar(port.writes) && P.phase1Voltage(1) == 2.5, 'a refused load changed the properties');
port.ack = 0;
message = expectError(@() P.saveProgramToMicroSD('NEW.pps'));
assert(contains(message, 'could not save NEW.pps'), message);
message = expectError(@() P.deleteProgramFromMicroSD('MISSING.pps'));
assert(contains(message, 'could not delete MISSING.pps'), message);
% Pulse Pal 2 replies 2 while a channel plays: its microSD card shares the SPI bus with the DAC
[P, port] = connect(2, 22);
P.phase1Voltage(1) = 2.5;
port.ack = 2;
port.loadReply = 2;
port.clearWrites();
verbs = {'saved', 'loaded', 'deleted'};
calls = {@() P.saveProgramToMicroSD('NEW.pps'), @() P.loadProgramFromMicroSD('NEW.pps'), ...
         @() P.deleteProgramFromMicroSD('NEW.pps')};
for i = 1:3
    message = expectError(calls{i});
    assert(contains(message, 'while a channel is playing') && contains(message, ['NEW.pps was not ' verbs{i}]), message);
end
assert(numel(port.writes) == 3 && P.phase1Voltage(1) == 2.5, 'a refused operation read the parameters back');
end

function testCalibrationNeedsPulsePal3()
[P, port] = connect(2, 22);
expectError(@() P.setCalibration(1, 0.01));
assert(isempty(port.writes), 'calibration was sent to a Pulse Pal 2');
[P, port] = connect(); %#ok<ASGLU> P is used in evalc
evalc('P.setCalibration(2, -0.01)');
assert(isequal(port.writes{1}, uint8([213 96 1 typecast(int16(-33), 'uint8')])), 'op 96 message');
end

function testFormatWithoutConfirmation()
% A script run by an AI agent has no one to answer the prompt
[P, port] = connect();
evalc('formatted = P.formatMicroSD(''Confirm'', false);');
assert(formatted, 'formatMicroSD did not return true');
assert(isequal(port.writes{1}, uint8([213 97 double('WIPE')])), 'op 97 and its key');
assert(port.writes{end}(2) == 92 && port.NumBytesAvailable == 0, 'the defaults were not programmed after formatting');
expectError(@() P.formatMicroSD('Confirm', 2));
expectError(@() P.formatMicroSD('Ask', false));
P = connect(2, 22);
expectError(@() P.formatMicroSD('Confirm', false)); % Pulse Pal 2 has no microSD card
end

function testCustomBurstsNeedABurstDuration()
% A custom train whose times are burst onsets needs bursts that last. Assigned alone (autoSync on), each of the two
% parameters is checked against the other; with autoSync off, both together by syncToDevice(). Nothing is sent when one
% is refused.
[P, port] = connect();
message = expectError(@() setCell(P, 'customTrainTarget', 2, 'Bursts'));
assert(contains(message, 'Output channel 2') && contains(message, 'burstDuration must be above 0'), message);
P.burstDuration(2) = 0.05;
P.customTrainTarget{2} = 'Bursts';
expectError(@() setProperty(P, 'burstDuration', [0 0 0 0]));
assert(P.burstDuration(2) == 0.05 && numel(port.writes) == 2, 'a refused value was sent');
P.autoSync = false;
P.customTrainTarget{3} = 'Bursts';
expectError(@() P.syncToDevice());
P.burstDuration(3) = 0.05;
P.syncToDevice();
assert(numel(port.writes) == 3 && port.writes{3}(2) == 92, 'op 92');
end

function testAnEmptyCustomTrainIsRefused()
% The device refuses a train with no pulses too: it would have no pulse times to play
[P, port] = connect();
message = expectError(@() P.sendCustomPulseTrain(1, [], []));
assert(contains(message, 'at least one pulse'), message);
expectError(@() P.sendCustomWaveform(1, 0.001, []));
assert(isempty(port.writes), 'an empty train was sent');
end

function testANoReplyMentionsACommFailure()
% A device showing COMM. FAILURE! reads no command until its joystick is clicked, so every reply times out. The error
% says what to do. (The simulated port does not wait for its Timeout.)
[P, port] = connect();
port.silent = true;
message = expectError(@() quietly(@() setProperty(P, 'phase1Voltage', [1 1 1 1])));
assert(contains(message, 'COMM. FAILURE!') && contains(message, 'click its joystick'), message);
message = expectError(@() quietly(@() P.syncFromDevice()));
assert(contains(message, 'COMM. FAILURE!'), message);
message = expectError(@() quietly(@() PulsePalDevice(port)));
assert(contains(message, 'No reply to the handshake') && contains(message, 'COMM. FAILURE!'), message);
end

function testExportAndImportParams()
[P, port] = connect();
P.phase1Voltage(2) = 2.5;
P.triggerMode{1} = 'Toggle';
params = P.exportParams();
assert(isequal(fieldnames(params)', [P.info.outputParameterNames {'triggerMode'}]), 'exported fields');
assert(isequal(params.phase1Voltage, [5 2.5 5 5]) && isequal(params.triggerMode, {'Toggle', 'Normal'}), ...
    'exported values');
[P2, port2] = connect();
P2.importParams(jsondecode(jsonencode(params))); % A struct saved as text, with columns in place of rows
P.syncToDevice();
assert(isscalar(port2.writes) && isequal(port2.writes{1}, port.writes{end}), 'import is not one op 92');
assert(isequal(P2.exportParams(), params), 'imported values');
% A partial struct keeps the other parameters; a bad value changes and sends nothing
P2.importParams(struct('phase1Duration', [0.002 0.002 0.002 0.002]));
assert(isequal(P2.phase1Duration, [0.002 0.002 0.002 0.002]) && P2.phase1Voltage(2) == 2.5, 'partial import');
nWrites = numel(port2.writes);
expectError(@() P2.importParams(struct('phase1Voltage', [1 1 1 1], 'phase2Voltage', [11 0 0 0])));
assert(numel(port2.writes) == nWrites && isequal(P2.phase1Voltage, [5 2.5 5 5]), 'a refused import changed something');
% A field this class does not have, e.g. from a newer version, is skipped with a warning
id = expectWarning(@() P2.importParams(struct('phase1Voltage', [1 1 1 1], 'playbackModes', [0 0 0 0])));
assert(strcmp(id, 'PulsePalDevice:unknownParams') && isequal(P2.phase1Voltage, [1 1 1 1]), 'unknown field');
nWrites = numel(port2.writes);
% Sent whatever autoSync is, and autoSync is left as it is
P2.autoSync = false;
P2.importParams(struct('phase1Voltage', [1 1 1 1]));
assert(~P2.autoSync && numel(port2.writes) == nWrites + 1, 'autoSync');
end

function testProgramFiles()
% saveProgramToFile() writes the program file both GUIs open, in the Python GUI's format; loadProgramFromFile() reads
% it, and the .mat files saveParameters(), its earlier name, wrote before
P = connect();
P.phase1Voltage(2) = 2.5;
P.burstDuration(3) = 0.1;
P.customTrainTarget{3} = 'Bursts';
P.isBiphasic(4) = true;
P.triggerMode{1} = 'Toggle';
fileName = [tempname '.json'];
matName = [tempname '.mat'];
cleanup = onCleanup(@() delete(fileName));
P.saveProgramToFile(fileName);
program = jsondecode(fileread(fileName));
assert(program.format_version == 1, 'format_version');
assert(isequal(program.params.phase1_voltage', [5 2.5 5 5]) && ...
       isequal(program.params.is_biphasic', [false false false true]) && ...
       isequal(program.params.custom_train_target', {'Pulses' 'Pulses' 'Bursts' 'Pulses'}) && ...
       isequal(program.params.trigger_mode', {'Toggle' 'Normal'}), 'saved params');
assert(numel(fieldnames(program.params)) == 19, 'every parameter is saved');
assert(strcmp(program.device_info.output_parameter_names{1}, 'is_biphasic') && ...
       program.device_info.n_custom_pulse_trains == 4, 'device_info');
[P2, port2] = connect();
P2.loadProgramFromFile(fileName);
assert(isequal(P2.exportParams(), P.exportParams()), 'loaded params');
assert(isscalar(port2.writes) && port2.writes{1}(2) == 92, 'loading is not one op 92');
% A key this version does not know is skipped with a warning
text = regexprep(fileread(fileName), '"phase1_voltage"\s*:', '"future_param": [1, 1, 1, 1], "phase1_voltage":');
fileID = fopen(fileName, 'w');
fwrite(fileID, text, 'char');
fclose(fileID);
P2.setDefaultParams();
id = expectWarning(@() P2.loadProgramFromFile(fileName));
assert(strcmp(id, 'PulsePalDevice:unknownParams') && isequal(P2.exportParams(), P.exportParams()), 'unknown key');
% A .mat file saved by saveParameters() before it saved .json files
params = P.exportParams();
save(matName, 'params');
matCleanup = onCleanup(@() delete(matName));
P2.setDefaultParams();
P2.loadProgramFromFile(matName);
assert(isequal(P2.exportParams(), P.exportParams()), '.mat file');
expectError(@() P.saveProgramToFile('Program.mat'));
end

function testSyncFromDeviceStoresWhatTheDeviceHolds()
% The device can hold values an assignment would refuse, set by an older client. They are read back as they are. On
% Pulse Pal 2, whose shortest phase an assignment sets is 2 cycles.
[P, port] = connect(2, 22);
params = port.lastParams;
params(1:4) = typecast(uint32(1), 'uint8'); % phase1Duration on channel 1: one cycle
params(65:68) = typecast(uint32(300000000), 'uint8'); % burstDuration on channel 1: 15000 s
port.lastParams = params;
P.syncFromDevice();
assert(P.phase1Duration(1) == 0.00005 && P.burstDuration(1) == 15000, 'times read back');
assert(isequal(P.phase1Voltage, [5 5 5 5]), 'DAC code 49151 (4.99992 V) is not read back as 5 V, as in Python');
assert(islogical(P.isBiphasic) && iscell(P.customTrainTarget) && isequal(P.triggerMode, {'Normal', 'Normal'}), ...
    'types read back');
expectError(@() setProperty(P, 'phase1Duration', [0.00005 0.001 0.001 0.001])); % Assignments are checked again
% A refused import puts them back as they were, without the checks that would refuse them
message = expectError(@() P.importParams(struct('phase2Voltage', [11 0 0 0])));
assert(contains(message, 'phase2Voltage'), message);
assert(P.phase1Duration(1) == 0.00005 && P.burstDuration(1) == 15000, 'the values read back were not restored');
end

function testPortAndInfoAreReadOnly()
P = connect();
expectError(@() setProperty(P, 'info', struct));
expectError(@() setProperty(P, 'port', []));
end

function testMethodsReturnNothing()
% Failures raise an error, so a returned confirmation would always be true
methodList = ?PulsePalDevice;
for name = {'syncToDevice', 'syncFromDevice', 'saveProgramToMicroSD', 'loadProgramFromMicroSD', ...
            'deleteProgramFromMicroSD'}
    method = findobj(methodList.MethodList, 'Name', name{1});
    assert(isempty(method.OutputNames), '%s returns a value', name{1});
end
end

function testStatus()
% status() sends op 101 and reads its 29 bytes: the channels playing, then the counters since the last op 101 (rising
% edges per trigger channel, starts per output channel, comm failures)
[P, port] = connect();
s = P.status();
assert(isequal(port.writes, {uint8([213 101])}) && port.NumBytesAvailable == 0, 'op 101');
assert(isequal(s, struct('playing', [1 3], 'triggerEdges', [3 0], 'channelStarts', [2 0 1 0], 'commFailures', 1)), ...
    'status fields');
end

function testPulsePal2AndFirmwareV21()
port = SimulatedPulsePalPort(2, 22);
P = PulsePalDevice(port);
assert(numel(port.writes) == 6 && port.writes{6}(2) == 92, 'Pulse Pal 2 has no param sync mode to leave');
assert(isequal(P.info.triggerModes, {'Normal', 'Toggle', 'Gated'}), 'Pulse Pal 2 trigger modes');
assert(P.info.circuitBoardRevision == 0, 'Pulse Pal 2 hardware revision');
assert(isequal(P.info.serialNumber, ''), 'Pulse Pal 2 has no USB serial number');
port = SimulatedPulsePalPort(2, 21);
port.answersOp103 = false; % As firmware from before October 2026
P = []; % Assigned in evalc, which hides the warning that v22 is available
evalc('P = PulsePalDevice(port);');
assert(isequal(port.writes{end}(1:2), uint8([213 73])) && numel(port.writes{end}) == 2+128+24+16+8+2, ...
    'firmware v21 takes op 73');
assert(P.info.circuitBoardRevision == 0 && ~any(cellfun(@(w) isequal(w, uint8([213 100])), port.writes)), ...
    'firmware v21 has no op 100');
port.clearWrites();
P.restingVoltage(1) = 1;
P.phase1Voltage(:) = 1;
assert(numel(port.writes) == 5 && isequal(port.writes{1}(1:4), uint8([213 74 17 1])) && ...
    all(cellfun(@(w) isequal(w(1:3), uint8([213 74 2])), port.writes(2:5))), ...
    'firmware v21 takes op 74 per channel');
assert(port.NumBytesAvailable == 0, 'a confirm byte was left unread');
end

function [P, port] = connect(varargin)
% A PulsePalDevice connected to a simulated Pulse Pal (3, with firmware v22, unless given), with its writes cleared
port = SimulatedPulsePalPort(varargin{:});
P = PulsePalDevice(port);
port.clearWrites();
end

function [id, message] = expectWarning(func)
% Runs func, and returns the identifier and message of the last warning it raised, without showing it
lastwarn('', '');
evalc('func();');
[message, id] = lastwarn();
if isempty(id)
    error('no warning was raised by %s', func2str(func));
end
end

function message = expectError(func)
try
    func();
catch err
    message = err.message;
    return
end
error('no error was raised by %s', func2str(func));
end

function setProperty(P, name, value)
P.(name) = value;
end

function setCell(P, name, index, value)
P.(name){index} = value;
end

function quietly(func)
% Runs func without showing the warnings of the simulated port's short reads
state = warning('off', 'SimulatedPulsePalPort:ReadTimeout');
cleanup = onCleanup(@() warning(state));
func();
end

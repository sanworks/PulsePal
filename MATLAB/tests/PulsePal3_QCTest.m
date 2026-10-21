function results = PulsePal3_QCTest(Port, DriverPort, varargin)
% PulsePal3_QCTest runs the quality control panel on a newly built Pulse Pal 3, and calibrates the zero offset of its
% output channels. It checks that:
%   - the four output channels play the voltages and times they are sent, with the noise, settling and DAC write
%     feedthrough of a correctly built board (limits in LIMITS below), and play their channels together;
%   - the two trigger channels detect every 10 us TTL pulse and gap at 3.1 and 4.9 V, ignore 0.8 V, and start the
%     output channels linked to them;
%   - USB transfers and the custom train memory work: four 10000-sample custom trains are uploaded and every sample is
%     checked on the oscilloscope;
%   - programs saved on the microSD card load back exactly;
%   - the six LEDs work (the operator confirms them), and the joystick: the device runs its own joystick test (op 104,
%     firmware from October 2026), prompting on its screen for each direction and three clicks, and reports how far
%     the joystick went.
% The automated checks take about 30 s, the LED and joystick checks as long as the operator takes (about 20 s).
%
% Bench: output channels 1-4 connected to channels 1-4 of a Rigol MSO5000 oscilloscope on the network (SCOPE_ADDRESS
% below), with no load.
%   PulsePal3_QCTest('COM33')           The trigger channels are tested with the device's own output channels 1 and
%                                       2: near the end, the panel asks for them to be moved onto trigger channels 1
%                                       and 2 (they go back to the oscilloscope for the next unit).
%   PulsePal3_QCTest('COM33', 'COM10')  A second Pulse Pal 3 (here on COM10) drives the trigger channels: its output
%                                       channels 1 and 2 wired to trigger channels 1 and 2 of the device under test.
%                                       No cables are moved.
%   results = PulsePal3_QCTest(..., Name, Value)
% Options:
%   'Scope'         The oscilloscope's IP address (default: SCOPE_ADDRESS below)
%   'Manual'        false skips the checks that need the operator: the LEDs and the joystick, and without a driver
%                   the trigger channels. The panel then cannot pass. Default true
%   'RecordFolder'  Folder for each unit's record (a .mat file, and a line in PulsePal3_QC_log.csv), or '' for
%                   none. Default: PulsePal3_QC in MATLAB's userpath
% results: the device's identity, every check (status, name, detail), the zero calibration written to the device,
% and passed (true only if every check passed and none was skipped).
%
% The oscilloscope is fully configured over the network: probe ratio, coupling, bandwidth limit, timebase position
% and trigger. Its front panel settings do not matter, and it is left running, showing the outputs.
% Requires MATLAB R2020b or newer (tcpclient's writeline, readline and flush).

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

% Rigol MSO5074 on the bench. (The office DS1054Z, 192.168.0.197, is a DS1000Z: its SCPI commands are handled below,
% but this version of the panel has only been run on the MSO5074.)
SCOPE_ADDRESS = '192.168.50.47';

% Pass limits, with what the first unit tested (serial number 20885950) measured over nine runs: MSO5074, 20 MHz
% bandwidth limit, no load.
LIMITS = struct( ...
    'zeroOffset', 0.010, ... % V. Output at 0 V before calibration. Measured -1.3 to +1.2 mV
    'zeroCalibrated', 0.0004, ... % V. After calibration (in steps of 0.3 mV), on another scope scale. Measured < 0.11 mV
    'noise', 0.001, ... % V RMS at 0 V, scope noise included. Measured 0.46-0.49 mV
    'dcError', 0.020, ... % V at -10, -5, 5 and 10 V, the scope's own zero error on its scale taken out. Measured < 7 mV
    'smallStep', 0.001, ... % V. Error of 10 mV steps up and down from 0 V, against 0 V next to them. Measured -0.36 to
    ...                       % +0.36 mV: each step's own size (about one DAC code), the same within 0.1 mV run to run
    'stepTransient', 0.038, ... % V. A channel's own transients beyond the levels it steps between (DAC write
    ...                           % feedthrough, and the DAC's glitch at mid-scale), on a +/-40 mV screen. Measured 17-31 mV
    'feedthrough', 0.019, ... % V. Peak on a channel when another channel's DAC write leaves that channel's voltage
    ...                         % unchanged, averaged over 7 writes. Measured 13.1-16.7 mV. The analog quality test's
    ...                         % measure of the same: 9.5-16.4 mV on six built boards, 20.2-24.6 mV on a board built
    ...                         % without one of its capacitors (PCB 3.0.1 NOCAP)
    'crosstalk', 0.030, ... % V. Peak on a channel when another steps 10 mV (one step). Measured 18.0-23.1 mV
    'width', 0.1e-6, ... % s. Error of 10 us phases, at 50% of each step. Measured +6 to +61 ns
    'period', 0.05e-6, ... % s. Error of a 50 us pulse period. Measured -8 to +18 ns
    'skew', 0.1e-6, ... % s. Spread of the four channels' edges when all change in the same cycle (the DAC latches
    ...                   % them together; a write latched on its own would be about 1 us apart). Measured 23-49 ns
    'riseTime', 2e-6, ... % s. 10-90% of a 0 to 5 V step. Measured 1.53-1.58 us
    'overshoot', 0.05, ... % Fraction of a 5 V step. Measured 0.4-1.6%
    'plateau', 0.25, ... % V. The +/-5 V phases' amplitude, on a 2 V/div scale (DC accuracy is checked separately)
    'clockPPM', 50, ... % Timer clock against the scope's, over 100 ms. Measured -3 to -1 ppm
    'uploadSeconds', 0.25); % s. Upload of one 10000-sample custom train (60 kB). Measured 5-11 ms
TTL_LOW = 0.8; % V. Must not trigger: the TTL low limit. Inputs measured to switch at 1.6-1.8 V
TTL_HIGHS = [3.1 4.9]; % V. Must trigger. The inputs take TTL only (0 V low, 3 to 5 V high): never drive them outside
TTL_MAX = 4.9; % V. Highest voltage the panel ever sends to a trigger channel

if nargin < 2
    DriverPort = '';
end
DriverPort = char(DriverPort); % "" as well as '' means no driver
parser = inputParser;
parser.FunctionName = 'PulsePal3_QCTest';
addParameter(parser, 'Scope', SCOPE_ADDRESS);
addParameter(parser, 'Manual', true);
addParameter(parser, 'RecordFolder', defaultRecordFolder());
parse(parser, varargin{:});
scopeAddress = char(parser.Results.Scope);
manual = logical(parser.Results.Manual);
recordFolder = char(parser.Results.RecordFolder);
useDriver = ~isempty(DriverPort);

checks = struct('status', {}, 'name', {}, 'detail', {});
P = []; % The device under test
D = []; % The driver, if any
S = []; % The oscilloscope
dutDefaults = [];
driverProgram = [];
calibration = NaN(1, 4);
operatorTime = 0; % Seconds spent waiting for the operator, left out of the panel's own run time
panelTimer = tic;

fprintf('\n*** Pulse Pal 3 QC panel ***\n');
disp('Output channels 1-4 must be connected to oscilloscope channels 1-4.')
if useDriver
    fprintf('Driver Pulse Pal on %s: its output channels 1 and 2 drive trigger channels 1 and 2.\n', DriverPort);
end

% ---- Connections ----
beginSection('Device');
P = PulsePalDevice(Port);
% Each cleanup holds its own copy of the handle: a nested function's variables are gone by the time a cleanup runs
releaseP = onCleanup(@() releaseDevice(P));
info = P.info;
report(info.hardwareVersion == 3 && info.firmwareVersion >= 22, 'Hardware and firmware', ...
    sprintf('Pulse Pal %d, firmware v%d, circuit board revision %d, serial number %s', info.hardwareVersion, ...
    info.firmwareVersion, info.circuitBoardRevision, info.serialNumber));
if info.hardwareVersion ~= 3
    error('PulsePal3_QCTest tests Pulse Pal 3 only: the device on %s is a Pulse Pal %d.', Port, info.hardwareVersion);
end
report(info.cyclePeriod_us == 10, 'Timer cycle', sprintf('%g us (expected 10 us)', info.cyclePeriod_us));
report(info.nCustomPulseTrains == 4 && info.maxCustomPulses == 10000, 'Custom train memory', ...
    sprintf('%d trains of %d pulses (expected 4 of 10000)', info.nCustomPulseTrains, info.maxCustomPulses));
dutDefaults = P.exportParams();
P.status(); % Clears the device's counters: the comm failure check counts this session's only
if useDriver
    D = PulsePalDevice(DriverPort);
    releaseD = onCleanup(@() releaseDevice(D));
    if strcmp(D.info.serialNumber, info.serialNumber)
        error('The driver (%s) and the device under test (%s) are the same device.', DriverPort, Port);
    end
    if D.info.hardwareVersion ~= 3
        error('The driver must be a Pulse Pal 3, which plays the 10 us trigger pulses: %s is a Pulse Pal %d.', ...
            DriverPort, D.info.hardwareVersion);
    end
    D.stop();
    % Every output TTL safe from here on: monophasic pulses from a 0 V rest, linked to nothing
    driverProgram = D.exportParams();
    driverProgram.phase1Voltage(:) = 4;
    programTTL(D, driverProgram);
    report(true, 'Driver', sprintf('%s, firmware v%d, serial number %s', DriverPort, D.info.firmwareVersion, ...
        D.info.serialNumber));
end
S = scopeOpen(scopeAddress);
releaseS = onCleanup(@() releaseScope(S));
scopeErrorList = scopeInit(S);
report(true, 'Oscilloscope', S.idn);
if ~isempty(scopeErrorList)
    fprintf('        (oscilloscope did not take: %s)\n', strjoin(scopeErrorList, '; '));
end

% ---- Automated checks ----
if useDriver
    runSection('Trigger channels', @() testTriggerChannels(D, 'Driver output'));
end
runSection('Zero offset, noise and calibration', @testZeroOffset);
runSection('DC levels', @testDCLevels);
runSection('Small steps and DAC write feedthrough', @testSmallSteps);
runSection('Pulse timing and shape', @testPulseTiming);
runSection('USB transfer and custom train memory', @testCustomTrains);
runSection('microSD card', @testMicroSD);
automatedTime = toc(panelTimer);
scopeLiveView(S);

% ---- Checks with the operator ----
if ~useDriver
    if manual
        runSection('Trigger channels', @testTriggerChannelsLoopback);
    else
        beginSection('Trigger channels');
        skip('Trigger channels', 'needs output channels 1 and 2 moved onto the trigger channels');
    end
end
if manual
    runSection('LEDs', @testLEDs);
    runSection('Joystick', @testJoystick);
else
    beginSection('LEDs and joystick');
    skip('LEDs', 'Manual is false');
    skip('Joystick', 'Manual is false');
end

% ---- Summary ----
nFailed = sum(strcmp({checks.status}, 'FAIL'));
nSkipped = sum(strcmp({checks.status}, 'SKIP'));
passed = nFailed == 0 && nSkipped == 0;
results = struct();
results.passed = passed;
results.serialNumber = info.serialNumber;
results.circuitBoardRevision = info.circuitBoardRevision;
results.firmwareVersion = info.firmwareVersion;
results.port = Port;
results.driverPort = DriverPort;
results.date = char(datetime('now', 'Format', 'yyyy-MM-dd HH:mm:ss'));
results.calibration = calibration;
results.checks = checks;
results.scope = S.idn;
results.automatedSeconds = automatedTime;
results.totalSeconds = toc(panelTimer);
fprintf(['\nChecks: %d passed, %d failed, %d skipped. Automated checks %.0f s; %.0f s in all, %.0f s of it ' ...
    'waiting for the operator.\n'], sum(strcmp({checks.status}, 'PASS')), nFailed, nSkipped, automatedTime, ...
    results.totalSeconds, operatorTime);
if nFailed > 0
    disp('Failed:')
    failed = checks(strcmp({checks.status}, 'FAIL'));
    for iFailed = 1:numel(failed)
        fprintf('  %s: %s\n', failed(iFailed).name, failed(iFailed).detail);
    end
end
disp('************************************************');
fprintf('*  Pulse Pal 3, serial number %s: %s\n', info.serialNumber, ...
    conditional(passed, 'PASSED', conditional(nFailed == 0, 'INCOMPLETE (checks skipped)', 'FAILED')));
disp('************************************************');
saveRecord();
if ~useDriver && manual
    disp('Move output channels 1 and 2 back to oscilloscope channels 1 and 2 for the next unit.')
end

    % ==== Sections ====

    function testTriggerChannels(src, sourceName)
        % src drives trigger channels 1 and 2 from its output channels 1 and 2: the driver, or the device itself
        loopback = src == P;
        width = max(10e-6, src.info.minPulseWidth_us*1e-6); % The shortest pulse src plays: one 10 us cycle
        n = 1000;
        for ch = 1:2
            s = ttlBurst(src, ch, 4, 1e-3, 1e-3, 1);
            expected = [0 0];
            expected(ch) = 1;
            report(isequal(s.triggerEdges, expected), sprintf('%s %d -> trigger channel %d', sourceName, ch, ch), ...
                sprintf('rising edges counted: [%d %d], expected [%d %d]', s.triggerEdges, expected));
        end
        s = ttlBurst(src, [1 2], TTL_LOW, width, 4*width, n);
        report(all(s.triggerEdges == 0), sprintf('%g V pulses ignored', TTL_LOW), ...
            sprintf('%d and %d edges counted from %d pulses (expected none)', s.triggerEdges, n));
        for high = TTL_HIGHS
            s = ttlBurst(src, [1 2], high, width, 4*width, n);
            report(all(s.triggerEdges == n), sprintf('%g us pulses at %g V', width*1e6, high), ...
                sprintf('%d and %d of %d detected', s.triggerEdges, n));
        end
        s = ttlBurst(src, [1 2], TTL_HIGHS(1), 4*width, width, n);
        report(all(s.triggerEdges == n), sprintf('%g us gaps at %g V', width*1e6, TTL_HIGHS(1)), ...
            sprintf('%d and %d of %d detected', s.triggerEdges, n));
        % Each trigger channel starts the output channels linked to it, and no others
        if loopback
            links = logical([0 0 1 0; 0 0 0 1]); % Output channels 1 and 2 are the TTL sources
        else
            links = logical([1 1 0 0; 0 0 1 1]);
        end
        for ch = 1:2
            s = ttlBurst(src, ch, 4, 1e-3, 1e-3, 1, links);
            expected = double(links(ch, :));
            if loopback
                expected(ch) = 1; % The source output channel's own start
            end
            report(isequal(s.channelStarts, expected) && s.triggerEdges(ch) == 1, ...
                sprintf('Trigger %d starts %s', ch, conditional(sum(links(ch, :)) > 1, 'outputs ', 'output ') + ...
                strjoin(string(find(links(ch, :))), ' and ')), ...
                sprintf('output channel starts [%s], expected [%s]', strjoin(string(s.channelStarts), ' '), ...
                strjoin(string(expected), ' ')));
        end
        P.stop();
        P.setDefaultParams();
    end

    function testTriggerChannelsLoopback()
        % Without a driver: the device's output channels 1 and 2, moved onto its trigger channels, are the TTL sources
        P.stop();
        P.setDefaultParams();
        loopbackProgram = dutDefaults;
        loopbackProgram.phase1Voltage(1:2) = 4;
        programTTL(P, loopbackProgram);
        askOperator(['Move output channel 1 to trigger channel 1, and output channel 2 to trigger channel 2 ' ...
            '(outputs 3 and 4 stay on the oscilloscope). Press Enter when done > ']);
        testTriggerChannels(P, 'Output');
    end

    function testZeroOffset()
        P.stop();
        P.setDefaultParams(); % All outputs at rest, at 0 V
        for c = 1:4
            quietly(@() P.setCalibration(c, 0));
        end
        % Uncalibrated, on a scale that shows +/-20 mV. The trigger level is out of reach (see scopeStatic())
        scopeSetup(S, 0.005*[1 1 1 1], [0 0 0 0], 1e-3, 0, 1e5, 1, 0.015);
        rec = scopeStatic(S);
        offsets = arrayfun(@(r) mean(r.v), rec);
        for c = 1:4
            report(abs(offsets(c)) <= LIMITS.zeroOffset && ~rec(c).clipped, sprintf('Ch%d offset at 0 V', c), ...
                sprintf('%+.2f mV before calibration (limit +/-%g mV)%s', 1e3*offsets(c), 1e3*LIMITS.zeroOffset, ...
                clippedNote(rec(c))));
        end
        calibration = max(min(-offsets, 0.1), -0.1);
        for c = 1:4
            quietly(@() P.setCalibration(c, calibration(c)));
        end
        scopeSetup(S, 0.002*[1 1 1 1], [0 0 0 0], 1e-3, 0, 1e5, 1, 0.006);
        rec = scopeStatic(S);
        for c = 1:4
            residual = mean(rec(c).v);
            noise = std(rec(c).v);
            report(abs(residual) <= LIMITS.zeroCalibrated && ~rec(c).clipped, sprintf('Ch%d calibrated zero', c), ...
                sprintf('%+.2f mV with calibration %+.2f mV (limit +/-%g mV)%s', 1e3*residual, 1e3*calibration(c), ...
                1e3*LIMITS.zeroCalibrated, clippedNote(rec(c))));
            report(noise <= LIMITS.noise, sprintf('Ch%d noise at 0 V', c), sprintf(['%.2f mV RMS, 20 MHz ' ...
                'bandwidth (limit %g mV)'], 1e3*noise, 1e3*LIMITS.noise));
        end
    end

    function testDCLevels()
        % Each channel at -10, -5, 0, 5 and 10 V, with the oscilloscope at 0.1 V/div centred on each level. The five
        % captures give each channel every level, and every channel a different level from the others in each capture,
        % so that two channels shorted together pull each other off. The scope's own zero error on this scale (up to
        % 20 mV) is taken from the 0 V capture of each channel, a few seconds apart. A DS1000Z offsets only +/-2 V
        % below 0.5 V/div, so it measures at 0.5 V/div (20 mV per code, against 4 mV).
        levels = [-10 -5 0 5 10];
        vdiv = conditional(S.isMSO5000, 0.1, 0.5);
        measured = NaN(4, 5);
        clipped = false(4, 5);
        for r = 0:4
            index = mod((0:3) + r, 5) + 1;
            channelLevels = levels(index);
            for c = 1:4
                P.setFixedVoltage(c, channelLevels(c));
            end
            scopeSetup(S, vdiv*[1 1 1 1], -channelLevels, 1e-3, 0, 1e5, 1, channelLevels(1) + 3*vdiv);
            rec = scopeStatic(S);
            for c = 1:4
                measured(c, index(c)) = mean(rec(c).v);
                clipped(c, index(c)) = rec(c).clipped > 0;
            end
        end
        P.setFixedVoltage(1:4, 0);
        expected = dacVolts(levels);
        for c = 1:4
            scopeZero = measured(c, 3) - expected(3);
            err = measured(c, :) - expected - scopeZero;
            others = [1 2 4 5];
            ok = all(abs(err(others)) <= LIMITS.dcError) && ~any(clipped(c, :));
            note = '';
            if any(clipped(c, :))
                note = ' (clipped: far off)';
            end
            report(ok, sprintf('Ch%d DC levels', c), sprintf(['errors %s mV at -10, -5, 5, 10 V (limit +/-%g mV; ' ...
                'scope zero %+.1f mV)%s'], sprintf('%+.1f ', 1e3*err(others)), 1e3*LIMITS.dcError, 1e3*scopeZero, ...
                note));
        end
    end

    function testSmallSteps()
        % Each channel in turn, 450 us apart, plays +10 mV, 0, -10 mV and 0 for 40 us each, then six more samples of
        % 0 V 20 us apart, and rests at 0 V. Each step's level is the mean of 28 us, against 0 V just before the
        % channel starts and just after it ends (interpolated between the two). Against the level before the trigger,
        % up to 0.9 ms earlier, and over 5 us, steps read up to 0.76 mV off; this way their errors repeat within
        % 0.1 mV from run to run, and what is left is the size of each step itself.
        % The six 0 V samples and the return to rest rewrite the DAC with the voltage it already holds: what the other
        % channels show then is DAC write feedthrough alone, averaged over those seven writes and sampled every 10 ns,
        % as the analog quality test measures it (/Python/PulsePal/tests/analog_quality). A write's SPI transfer comes
        % about 6 us before the timer latches it (DAC_LATCH_US), and makes most of the feedthrough, so each write's
        % window starts 9 us before the change it makes.
        stepLength = 40e-6;
        spacing = 450e-6;
        onsets = [(0:3)*stepLength, 4*stepLength + (0:5)*20e-6];
        volts = [0.01 0 -0.01 0 0 0 0 0 0 0];
        for c = 1:4
            P.sendCustomPulseTrain(c, (c - 1)*spacing + onsets, volts);
        end
        prm = dutDefaults;
        prm.customTrainID = 1:4;
        prm.phase1Duration(:) = stepLength; % A sample whose next one starts within this follows on with no rest
        prm.pulseTrainDuration(:) = 2e-3;
        P.importParams(prm);
        scopeSetup(S, 0.01*[1 1 1 1], [0 0 0 0], 2e-4, 9.5e-4, 1e6, 1, 0.005);
        rec = scopeTriggered(S, @() P.trigger(1:4));
        P.setDefaultParams();
        % When channel 1 first changed: the scope can trigger on the feedthrough of the write before it. A 1 us running
        % median removes feedthrough spikes and keeps the 10 mV step.
        t1 = rec(1).t;
        base1 = mean(rec(1).v(t1 > -45e-6 & t1 < -15e-6));
        near = find(t1 > -20e-6 & t1 < 20e-6);
        smooth = movmedian(rec(1).v(near), round(1e-6/rec(1).dt));
        first = find(smooth > base1 + 0.005, 1);
        if isempty(first)
            error('Channel 1''s first 10 mV step was not found.');
        end
        tLatch = t1(near(first));
        after = [dacVolts(volts) - dacVolts(0), 0]; % Level after each write, as played (DAC codes), from the 0 V code
        before = [0, after(1:end - 1)];
        nWrites = numel(after);
        rewrites = 5:nWrites; % 0 V written over 0 V
        writeTimes = @(c) tLatch + (c - 1)*spacing + [onsets, onsets(end) + stepLength]; % The last: return to rest
        decimation = max(1, round(10e-9/rec(1).dt));
        for c = 1:4
            t = rec(c).t;
            v = rec(c).v;
            e = writeTimes(c);
            level = @(from, to) mean(v(t > e(1) + from & t < e(1) + to));
            zeroBefore = level(-40e-6, -9e-6);
            zeroAfter = level(e(end) - e(1) + 5e-6, e(end) - e(1) + 40e-6);
            references = interp1(e([1 end]) + [-24.5e-6 22.5e-6], [zeroBefore zeroAfter], e(1:4) + 19e-6);
            plateaus = arrayfun(@(k) level(e(k) - e(1) + 5e-6, e(k) - e(1) + 33e-6), 1:4);
            stepErrors = plateaus - references - after(1:4);
            % Peaks, every 10 ns, from the level before the trigger
            dt = decimation*rec(c).dt;
            t = t(1:decimation:end);
            x = v(1:decimation:end) - mean(v(rec(c).t > tLatch - 45e-6 & rec(c).t < tLatch - 10e-6));
            nWindow = round(15e-6/dt);
            transient = 0; % Beyond the two levels a write moves between
            for k = 1:nWrites
                w = t > e(k) - 9e-6 & t < e(k) + 8e-6;
                transient = max([transient, max(x(w)) - max(before(k), after(k)), min(before(k), after(k)) - min(x(w))]);
            end
            report(all(abs(stepErrors) <= LIMITS.smallStep) && ~rec(c).clipped, sprintf('Ch%d 10 mV steps', c), ...
                sprintf('+10 mV %+.2f, back to 0 %+.2f, -10 mV %+.2f, back to 0 %+.2f mV error (limit +/-%g mV)', ...
                1e3*stepErrors, 1e3*LIMITS.smallStep));
            report(transient <= LIMITS.stepTransient && ~rec(c).clipped, sprintf('Ch%d step transients', c), ...
                sprintf('%.1f mV beyond its levels (limit %g mV)%s', 1e3*transient, 1e3*LIMITS.stepTransient, ...
                clippedNote(rec(c))));
            % What this channel shows while the others write
            feedthrough = 0;
            crosstalk = 0;
            for w = setdiff(1:4, c)
                starts = round((writeTimes(w)' - 9e-6 - t(1))/dt) + 1;
                windows = x(starts + (0:nWindow - 1)); % One row per write
                feedthrough = max(feedthrough, max(abs(mean(windows(rewrites, :), 1))));
                crosstalk = max(crosstalk, max(max(abs(windows(1:4, :)))));
            end
            report(feedthrough <= LIMITS.feedthrough, sprintf('Ch%d DAC write feedthrough', c), sprintf(['%.1f mV ' ...
                'peak when another channel rewrites its voltage (limit %g mV)'], 1e3*feedthrough, ...
                1e3*LIMITS.feedthrough));
            report(crosstalk <= LIMITS.crosstalk, sprintf('Ch%d crosstalk', c), sprintf(['%.1f mV peak when ' ...
                'another channel steps 10 mV (limit %g mV)'], 1e3*crosstalk, 1e3*LIMITS.crosstalk));
        end
    end

    function testPulseTiming()
        % All four channels play the same biphasic train together: +5 V for 10 us (one timer cycle), 10 us at 0 V,
        % -5 V for 10 us, every 50 us, three times. Their edges must coincide (the DAC latches all four at once).
        prm = dutDefaults;
        prm.isBiphasic(:) = true;
        prm.phase1Voltage(:) = 5;
        prm.phase2Voltage(:) = -5;
        prm.phase1Duration(:) = 10e-6;
        prm.interPhaseInterval(:) = 10e-6;
        prm.phase2Duration(:) = 10e-6;
        prm.interPulseInterval(:) = 20e-6;
        prm.pulseTrainDuration(:) = 140e-6;
        P.importParams(prm);
        scopeSetup(S, 2*[1 1 1 1], [0 0 0 0], 20e-6, 90e-6, 1e5, 1, 2.5);
        rec = scopeTriggered(S, @() P.trigger(1:4));
        P.setDefaultParams();
        edgeTimes = NaN(4, 12);
        for c = 1:4
            t = rec(c).t;
            v = movmean(rec(c).v, 10); % 20 ns
            % Levels first, then the edges at 50% of each step: the scope's offset error on this scale (up to 0.3 V)
            % and its gain error would otherwise move every edge
            rest = mean(v(t > -8e-6 & t < -1e-6));
            [up, down, upNeg, downNeg] = phaseEdges(t, v, rest + 2.5, rest - 2.5);
            if numel(up) ~= 3 || numel(down) ~= 3 || numel(downNeg) ~= 3 || numel(upNeg) ~= 3
                report(false, sprintf('Ch%d pulses', c), sprintf(['found %d starts, %d ends of phase 1 and %d, %d ' ...
                    'of phase 2 (expected 3 of each)'], numel(up), numel(down), numel(downNeg), numel(upNeg)));
                continue
            end
            high = mean(v(t > up(1) + 5e-6 & t < down(1) - 1e-6));
            low = mean(v(t > downNeg(1) + 5e-6 & t < upNeg(1) - 1e-6));
            [up, down, upNeg, downNeg] = phaseEdges(t, v, (rest + high)/2, (rest + low)/2);
            edgeTimes(c, :) = [up' down' downNeg' upNeg'];
            widthErrors = [down - up; upNeg - downNeg] - 10e-6;
            periodErrors = diff(up) - 50e-6;
            report(all(abs(widthErrors) <= LIMITS.width) && all(abs(periodErrors) <= LIMITS.period), ...
                sprintf('Ch%d phase widths and period', c), sprintf(['10 us phases %+.0f to %+.0f ns, 50 us period ' ...
                '%+.0f to %+.0f ns (limits +/-%g, +/-%g ns)'], 1e9*min(widthErrors), 1e9*max(widthErrors), ...
                1e9*min(periodErrors), 1e9*max(periodErrors), 1e9*LIMITS.width, 1e9*LIMITS.period));
            t10 = crossings(t, v, rest + 0.1*(high - rest), 0.2);
            t90 = crossings(t, v, rest + 0.9*(high - rest), 0.2);
            riseTime = t90(1) - t10(1);
            overshoot = (max(v(t > up(1) & t < down(1))) - high)/(high - rest);
            report(abs(high - rest - 5) <= LIMITS.plateau && abs(low - rest + 5) <= LIMITS.plateau && ...
                riseTime <= LIMITS.riseTime && overshoot <= LIMITS.overshoot, sprintf('Ch%d pulse shape', c), ...
                sprintf(['phases %+.2f and %+.2f V from rest (limit +/-%g V), rise time %.2f us (limit %g), ' ...
                'overshoot %.1f%% (limit %g%%)'], high - rest, low - rest, LIMITS.plateau, 1e6*riseTime, ...
                1e6*LIMITS.riseTime, 100*overshoot, 100*LIMITS.overshoot));
        end
        if all(~isnan(edgeTimes(:)))
            skew = max(edgeTimes) - min(edgeTimes);
            report(all(skew <= LIMITS.skew), 'Channels change together', sprintf(['edges of the four channels ' ...
                'within %.0f ns (limit %g ns)'], 1e9*max(skew), 1e9*LIMITS.skew));
        end
    end

    function testCustomTrains()
        % Four custom trains of 10000 samples (the most a train holds), 10 us each: pseudo-random levels from -7 to
        % +7 V, 2 V apart, never the same twice in a row. Every sample of all four is checked on the oscilloscope.
        nSamples = info.maxCustomPulses;
        levels = -7:2:7;
        sequences = cell(1, 4);
        uploadTimes = NaN(1, 4);
        for c = 1:4
            stream = RandStream('mt19937ar', 'Seed', c);
            steps = randi(stream, 7, 1, nSamples - 1);
            index = [8, mod(7 + cumsum(steps), 8) + 1];
            % The first and last samples are +7 V: the scope triggers on the first one's edge, and the train's length
            % is timed between the two 7 V edges, which take the same time to reach their 50% points
            if index(end - 1) == 8
                index(end - 1) = find((1:7) ~= index(end - 2), 1);
            end
            index(end) = 8;
            sequences{c} = levels(index);
            uploadTimer = tic;
            P.sendCustomWaveform(c, 10e-6, sequences{c});
            uploadTimes(c) = toc(uploadTimer);
        end
        kBytes = (nSamples*6 + 7)/1000;
        report(all(uploadTimes <= LIMITS.uploadSeconds), 'USB uploads', sprintf(['4 trains of %.0f kB in %s ms ' ...
            '(%.1f MB/s; limit %g s each)'], kBytes, strjoin(string(round(1e3*uploadTimes)), ', '), ...
            4*kBytes/1000/sum(uploadTimes), LIMITS.uploadSeconds));
        prm = dutDefaults;
        prm.customTrainID = 1:4;
        prm.phase1Duration(:) = 10e-6;
        prm.pulseTrainDuration(:) = 0.11;
        P.importParams(prm);
        scopeSetup(S, 2*[1 1 1 1], [0 0 0 0], 20e-3, 95e-3, 1e6, 1, 1);
        rec = scopeTriggered(S, @() P.trigger(1:4));
        P.setDefaultParams();
        for c = 1:4
            t = rec(c).t;
            dt = rec(c).dt;
            % Levels from the rest before the train: the scope's offset on this scale is up to 0.3 V
            x = rec(c).v - median(rec(c).v(t < -50e-6));
            sequence = sequences{c};
            up = crossings(t, x, 3.5, 0.3);
            up = up(up > -20e-6 & up < 20e-6);
            [~, ends] = crossings(t, x, 3.5, 0.3);
            ends = ends(ends > nSamples*10e-6 - 50e-6 & ends < nSamples*10e-6 + 50e-6);
            if isempty(up) || isempty(ends)
                report(false, sprintf('Ch%d custom train', c), 'start or end of the train not found');
                continue
            end
            period = (ends(end) - up(1))/nSamples;
            ppm = 1e6*(period/10e-6 - 1);
            % Each sample's level: the median of 5 to 8.5 us after its start, once the largest step (14 V) has settled
            first = round((up(1) + (0:nSamples - 1)'*period + 5e-6 - t(1))/dt) + 1;
            window = first + (0:round(3.5e-6/dt) - 1);
            sampleLevels = median(x(window), 2);
            [~, nearest] = min(abs(sampleLevels - levels), [], 2);
            wrong = find(reshape(levels(nearest), 1, []) ~= sequence);
            restAfter = mean(x(t > ends(end) + 20e-6));
            detail = sprintf('%d of %d samples wrong', numel(wrong), nSamples);
            if ~isempty(wrong)
                detail = sprintf('%s, first at sample %d (%+.2f V, expected %+d V)', detail, wrong(1), ...
                    sampleLevels(wrong(1)), sequence(wrong(1)));
            end
            report(isempty(wrong) && abs(restAfter) < 0.2 && ~rec(c).clipped, sprintf('Ch%d custom train', c), ...
                sprintf('%s; back at rest after it (%+.2f V)%s', detail, restAfter, clippedNote(rec(c))));
            report(abs(ppm) <= LIMITS.clockPPM, sprintf('Ch%d timer clock', c), sprintf(['%+.0f ppm against the ' ...
                'oscilloscope over 100 ms (limit +/-%g ppm)'], ppm, LIMITS.clockPPM));
        end
    end

    function testMicroSD()
        % A program with a different value in every parameter is saved, the defaults loaded, and the program loaded
        % back from the card
        prm = dutDefaults;
        prm.isBiphasic = logical([1 0 1 0]);
        prm.phase1Voltage = [1.25 -2.5 3.75 -5];
        prm.phase2Voltage = [-1.25 2.5 -3.75 5];
        prm.restingVoltage = [0 0.5 -0.5 0];
        prm.phase1Duration = [0.00012 0.0005 0.00103 0.002];
        prm.interPhaseInterval = [0.00001 0.00002 0.00003 0.00004];
        prm.phase2Duration = [0.00013 0.0006 0.00104 0.003];
        prm.interPulseInterval = [0.00014 0.0007 0.00105 0.004];
        prm.burstDuration = [0 0.01 0 0.02];
        prm.interBurstInterval = [0 0.005 0 0.006];
        prm.pulseTrainDuration = [1 2 3.00001 4];
        prm.pulseTrainDelay = [0 0.00001 0.1 1];
        prm.linkTriggerChannel1 = logical([1 0 1 0]);
        prm.linkTriggerChannel2 = logical([0 1 1 0]);
        prm.customTrainID = [0 1 2 0];
        prm.customTrainTarget = {'Pulses', 'Bursts', 'Pulses', 'Pulses'};
        prm.customTrainLoop = logical([0 1 0 1]);
        prm.triggerMode = {'Toggle', 'Gated'};
        fileName = 'QCTEST.pps';
        P.importParams(prm);
        P.saveProgramToMicroSD(fileName);
        P.setDefaultParams();
        P.syncFromDevice();
        defaultsLoaded = isequal(P.phase1Voltage, dutDefaults.phase1Voltage);
        P.loadProgramFromMicroSD(fileName); % Reads the parameters back from the device
        loaded = P.exportParams();
        names = setdiff(fieldnames(prm), {'continuousLoop'}); % Not part of a program file
        different = {};
        for i = 1:numel(names)
            a = prm.(names{i});
            b = loaded.(names{i});
            if isnumeric(a) && ~islogical(a)
                same = all(abs(double(a) - double(b)) <= 1e-9 + 1e-4*contains(names{i}, 'Voltage'));
            else
                same = isequal(a, b);
            end
            if ~same
                different{end + 1} = names{i}; %#ok<AGROW>
            end
        end
        P.deleteProgramFromMicroSD(fileName);
        deleted = false;
        try
            P.loadProgramFromMicroSD(fileName);
        catch
            deleted = true; % The file is gone
        end
        P.setDefaultParams();
        report(defaultsLoaded && isempty(different), 'Program saved and loaded', ...
            conditional(isempty(different), 'every parameter loaded as saved', ...
            ['different after loading: ' strjoin(different, ', ')]));
        report(deleted, 'Program deleted', conditional(deleted, 'the deleted file no longer loads', ...
            'the deleted file still loads'));
        s = P.status();
        report(s.commFailures == 0, 'USB connection', sprintf('%d comm failures', s.commFailures));
    end

    function testLEDs()
        % All six LEDs blink together, twice a second: the trigger channels' LEDs follow their TTL inputs, and the
        % output channels' LEDs light while they play a pulse (gated by the trigger channels)
        P.stop();
        prm = dutDefaults;
        prm.triggerMode = {'Gated', 'Gated'};
        prm.phase1Duration(:) = 1;
        prm.interPulseInterval(:) = 1;
        prm.pulseTrainDuration(:) = 2;
        if useDriver
            prm.linkTriggerChannel1 = logical([1 1 0 0]);
            prm.linkTriggerChannel2 = logical([0 0 1 1]);
            P.importParams(prm);
            source = D;
            sourceProgram = driverProgram;
        else
            prm.linkTriggerChannel1 = logical([0 0 1 0]);
            prm.linkTriggerChannel2 = logical([0 0 0 1]);
            source = P;
            sourceProgram = prm;
        end
        sourceProgram.phase1Voltage(1:2) = 4;
        sourceProgram.phase1Duration(1:2) = 0.25;
        sourceProgram.interPulseInterval(1:2) = 0.25;
        sourceProgram.pulseTrainDuration(1:2) = 1;
        sourceProgram.continuousLoop(1:2) = true;
        programTTL(source, sourceProgram);
        source.trigger([1 2]);
        answer = askYesNo(['Are all six LEDs blinking together, twice a second (two above the trigger channels, ' ...
            'four above the output channels)? (y/n) > ']);
        source.stop();
        P.stop();
        if useDriver
            programTTL(D, driverProgram);
        end
        P.setDefaultParams();
        report(answer, 'All six LEDs blink', conditional(answer, 'confirmed by the operator', ...
            'not confirmed by the operator'));
    end

    function testJoystick()
        % The device runs its own joystick test (op 104, "Joystick test" in /Firmware/PROTOCOL.md): its screen asks for
        % the joystick to be left alone, pushed fully up, down, left and right, and clicked 3 times, and shows the
        % position as it moves. It reports the resting position, how far each direction went, and the clicks counted.
        stepTimeout = 20; % Seconds the device allows each step
        P.stop();
        while true
            flush(P.port);
            fprintf('  >> Joystick: follow the prompts on the device''s screen (%d s for each step).\n', stepTimeout);
            waitTimer = tic;
            P.port.write([213 104 stepTimeout], 'uint8');
            started = readReply(1, 1); % Sent at once
            if ~isequal(started, 1)
                report(false, 'Joystick', ['no reply to op 104: the device''s firmware has no joystick test (it came ' ...
                    'in October 2026)']);
                return
            end
            msg = readReply(23, 5*stepTimeout + 20);
            operatorTime = operatorTime + toc(waitTimer);
            if numel(msg) < 23
                report(false, 'Joystick', sprintf('the joystick test sent %d of its 23 result bytes', numel(msg)));
                return
            end
            values = double(typecast(uint8(msg(2:21)), 'uint16'));
            rest = values(1:2);
            reached = values(3:6);
            levels = values(7:10);
            bits = double(msg(22));
            clicks = double(msg(23));
            if bits == 31 && clicks == 3
                break
            end
            if ~askYesNo('The joystick test failed. Run it again? (y/n) > ')
                break
            end
        end
        directions = {'up', 'down', 'left', 'right'};
        comparisons = {'<=', '>=', '<=', '>='};
        parts = cell(1, 4);
        for k = 1:4
            parts{k} = sprintf('%s %d (%s %d)', directions{k}, reached(k), comparisons{k}, levels(k));
        end
        report(bitand(bits, 15) == 15, 'Joystick directions', [strjoin(parts, ', ') ', in counts of 0-1023']);
        report(bitget(bits, 5) == 1, 'Joystick resting position', sprintf(['X %d, Y %d (400-600, where the menu ' ...
            'sees no push)'], rest));
        report(clicks == 3, 'Joystick button', sprintf('%d of 3 clicks detected', clicks));
    end

    function bytes = readReply(n, timeout)
        % Up to n bytes from the device, waiting up to timeout seconds for them
        waitTimer = tic;
        while P.port.NumBytesAvailable < n && toc(waitTimer) < timeout
            pause(0.01);
        end
        bytes = [];
        if P.port.NumBytesAvailable > 0
            bytes = double(P.port.read(min(n, P.port.NumBytesAvailable), 'uint8'));
        end
    end

    % ==== Helpers that share the panel's state ====

    function s = ttlBurst(src, channels, high, width, gap, n, links)
        % Plays n TTL pulses of width and gap (s) at high (V) on output channels of src, which drive trigger channels
        % 1 and 2, and returns the device's counts (status()). links (2x4 logical) sets which output channels of the
        % device each trigger channel starts.
        if nargin < 7
            links = false(2, 4);
        end
        loopback = src == P;
        dutProgram = dutDefaults;
        dutProgram.linkTriggerChannel1 = links(1, :);
        dutProgram.linkTriggerChannel2 = links(2, :);
        dutProgram.pulseTrainDuration(:) = 1e-3; % Linked channels finish before the next burst
        if loopback
            if any(any(links(:, 1:2)))
                error('Output channels 1 and 2 drive the trigger channels: they cannot be started by them.');
            end
            prm = dutProgram;
        else
            P.importParams(dutProgram);
            prm = driverProgram;
        end
        prm.phase1Voltage(1:2) = 4;
        prm.phase1Voltage(channels) = high;
        prm.phase1Duration(channels) = width;
        prm.interPulseInterval(channels) = gap;
        prm.pulseTrainDuration(channels) = n*width + (n - 1)*gap;
        programTTL(src, prm);
        P.stop();
        P.status(); % Clears the counters
        src.trigger(channels);
        pause(n*(width + gap) + 0.02);
        s = P.status();
    end

    function programTTL(device, prm)
        % Programs a device whose output channels 1 and 2 drive trigger channels, checking that both stay TTL safe:
        % monophasic, from a 0 V rest to at most TTL_MAX, not a custom train, not started by a trigger channel. The
        % program is read back from the device and checked again.
        checkTTLSafe(prm);
        device.importParams(prm);
        device.syncFromDevice();
        checkTTLSafe(device.exportParams());
    end

    function checkTTLSafe(prm)
        for ch = 1:2
            if prm.isBiphasic(ch) || prm.restingVoltage(ch) ~= 0 || prm.phase1Voltage(ch) < 0 || ...
                    prm.phase1Voltage(ch) > TTL_MAX || prm.customTrainID(ch) ~= 0 || ...
                    prm.linkTriggerChannel1(ch) || prm.linkTriggerChannel2(ch)
                error('Output channel %d drives a trigger channel, and would not be TTL safe: refused.', ch);
            end
        end
    end

    function beginSection(title)
        fprintf('\n--- %s ---\n', title);
    end

    function runSection(title, fcn)
        beginSection(title);
        sectionTimer = tic;
        try
            fcn();
            fprintf('        (%.1f s)\n', toc(sectionTimer));
        catch err
            report(false, title, ['stopped by an error: ' err.message]);
            try
                P.stop(); % Leave the device idle for the next section
                P.setDefaultParams();
            catch
            end
        end
    end

    function report(ok, name, detail)
        if ok
            status = 'PASS';
        else
            status = 'FAIL';
        end
        checks(end + 1) = struct('status', status, 'name', name, 'detail', detail);
        fprintf('  %s  %-36s %s\n', status, name, detail);
    end

    function skip(name, reason)
        checks(end + 1) = struct('status', 'SKIP', 'name', name, 'detail', reason);
        fprintf('  SKIP  %-36s %s\n', name, reason);
    end

    function askOperator(prompt)
        waitTimer = tic;
        input(['  >> ' prompt], 's');
        operatorTime = operatorTime + toc(waitTimer);
    end

    function yes = askYesNo(prompt)
        waitTimer = tic;
        answer = '';
        while ~any(strcmpi(answer, {'y', 'n'}))
            answer = strtrim(input(['  >> ' prompt], 's'));
        end
        operatorTime = operatorTime + toc(waitTimer);
        yes = strcmpi(answer, 'y');
    end

    function saveRecord()
        if isempty(recordFolder)
            return
        end
        try
            if ~isfolder(recordFolder)
                mkdir(recordFolder);
            end
            stamp = char(datetime('now', 'Format', 'yyyyMMdd_HHmmss'));
            verdict = conditional(passed, 'PASS', conditional(nFailed == 0, 'INCOMPLETE', 'FAIL'));
            matFile = fullfile(recordFolder, sprintf('PulsePal3_QC_%s_%s_%s.mat', info.serialNumber, stamp, verdict));
            save(matFile, 'results');
            logFile = fullfile(recordFolder, 'PulsePal3_QC_log.csv');
            newLog = ~isfile(logFile);
            fid = fopen(logFile, 'a');
            if newLog
                fprintf(fid, 'date,serial_number,circuit_board_revision,firmware,result,failed_checks,calibration_mV\n');
            end
            failedNames = strjoin({checks(strcmp({checks.status}, 'FAIL')).name}, '; ');
            fprintf(fid, '%s,%s,%d,%d,%s,"%s",%s\n', results.date, info.serialNumber, info.circuitBoardRevision, ...
                info.firmwareVersion, verdict, failedNames, strtrim(sprintf('%.2f ', 1e3*calibration)));
            fclose(fid);
            fprintf('Record saved: %s\n', matFile);
        catch err
            warning('PulsePal3_QCTest:record', 'The record was not saved: %s', err.message);
        end
    end

end

function releaseDevice(device)
% Stops every output and disconnects: the device shows its own screen again
if isvalid(device)
    try
        device.stop();
    catch
    end
    delete(device);
end
end

function releaseScope(S)
% Leaves the scope running, showing the outputs, and closes the connection
try
    scopeLiveView(S);
catch
end
delete(S.client);
end

% ==== Oscilloscope ====
% Raw SCPI over TCP (port 5555), for a Rigol MSO5000 (tested on an MSO5074) or DS1000Z. Settings are written only when
% they change (S.memo). The MSO5074 takes about 30 ms to answer each query, and 0.14 s to send each channel's record
% whatever its length, so the panel queries as little as it can. Its traps:
% - It silently ignores scale and offset commands for a hidden channel, so all four are shown first.
% - After a scale, offset or memory depth change, a free-running capture can return a record acquired before it,
%   with a preamble that describes the new settings (seen once 0.6 s after a change from 2 V/div to 5 mV/div). Likely
%   what made the earlier version of this panel fail with "Baseline mean out of range". So every capture is a single
%   acquisition armed after the settings (scopeStatic(), scopeTriggered()), and a record identical to the channel's
%   previous one is refused.
% - RAW memory can only be read while stopped, and right after a stop the first read can be an empty block.

function S = scopeOpen(address)
try
    client = tcpclient(address, 5555, 'Timeout', 10, 'ConnectTimeout', 5, 'EnableTransferDelay', false);
catch err
    error('PulsePal3_QCTest:scope', 'Could not connect to the oscilloscope at %s: %s', address, err.message);
end
configureTerminator(client, 'LF');
S = struct('client', client, 'memo', containers.Map(), 'idn', '', 'isMSO5000', true);
S.idn = scopeQuery(S, '*IDN?');
S.isMSO5000 = ~isempty(regexp(upper(S.idn), ',MSO5\d', 'once'));
end

function errorList = scopeInit(S)
% Sets everything a capture depends on, whatever the front panel was left at
scopeWrite(S, '*CLS');
scopeWrite(S, ':RUN');
for ch = 1:4
    p = sprintf(':CHANnel%d', ch);
    scopeWrite(S, [p ':DISPlay ON']); % First: the MSO5000 ignores scale and offset for hidden channels
    scopeWrite(S, [p ':COUPling DC']);
    scopeWrite(S, [p ':PROBe 1']);
    scopeWrite(S, [p ':INVert OFF']);
    scopeWrite(S, [p ':VERNier OFF']);
    scopeWrite(S, [p ':BWLimit 20M']);
end
scopeWrite(S, ':ACQuire:TYPE NORMal');
scopeWrite(S, ':TIMebase:MODE MAIN');
scopeWrite(S, ':TIMebase:DELay:ENABle OFF');
scopeWrite(S, ':TRIGger:MODE EDGE');
scopeWrite(S, ':TRIGger:EDGE:SLOPe POSitive');
scopeWrite(S, ':TRIGger:COUPling DC');
scopeWrite(S, ':TRIGger:NREJect OFF'); % Noise reject delays and can miss the small steps' trigger
scopeQuery(S, '*OPC?');
remove(S.memo, keys(S.memo));
errorList = scopeErrors(S);
end

function scopeSetup(S, vdiv, offset, tpd, tOffset, points, triggerSource, triggerLevel)
% vdiv, offset: 1x4, V/div and V. tpd: s/div; tOffset: time of the screen's centre after the trigger. points: memory
% depth on the MSO5000 (10 divisions); the DS1000Z takes the nearest of its own depths for 12 divisions.
for ch = 1:4
    p = sprintf(':CHANnel%d', ch);
    if scopeSet(S, [p ':SCALe'], vdiv(ch)) && isKey(S.memo, [p ':OFFSet'])
        remove(S.memo, [p ':OFFSet']); % The offset's range depends on the scale: set it again
    end
    scopeSet(S, [p ':OFFSet'], offset(ch));
end
if scopeSet(S, ':TIMebase:MAIN:SCALe', tpd) && isKey(S.memo, ':TIMebase:MAIN:OFFSet')
    remove(S.memo, ':TIMebase:MAIN:OFFSet');
end
scopeSet(S, ':TIMebase:MAIN:OFFSet', tOffset);
if S.isMSO5000
    depths = [1e3 1e4 1e5 1e6 1e7];
    tokens = {'1k', '10k', '100k', '1M', '10M'};
    [~, i] = min(abs(log(depths/points)));
else
    depths = [3e3 3e4 3e5 3e6]; % With all four channels on
    tokens = {'3000', '30000', '300000', '3000000'};
    [~, i] = min(abs(log(depths/(1.2*points))));
end
if ~isKey(S.memo, ':ACQuire:MDEPth') || ~strcmp(S.memo(':ACQuire:MDEPth'), tokens{i})
    scopeWrite(S, ':RUN'); % The memory depth can only be set while running
    scopeSet(S, ':ACQuire:MDEPth', tokens{i});
end
S.memo('points') = depths(i);
S.memo('tpd') = tpd;
if scopeSet(S, ':TRIGger:EDGE:SOURce', sprintf('CHANnel%d', triggerSource)) && isKey(S.memo, ':TRIGger:EDGE:LEVel')
    remove(S.memo, ':TRIGger:EDGE:LEVel'); % The MSO5000 keeps a level for each source
end
scopeSet(S, ':TRIGger:EDGE:LEVel', triggerLevel);
scopeQuery(S, '*OPC?');
end

function changed = scopeSet(S, command, value)
% Writes a setting if it differs from what this panel last wrote
if isnumeric(value)
    value = sprintf('%.6g', value);
end
changed = ~isKey(S.memo, command) || ~strcmp(S.memo(command), value);
if changed
    scopeWrite(S, [command ' ' value]);
    S.memo(command) = value;
end
end

function rec = scopeStatic(S)
% Captures static levels: one acquisition, armed once every setting has been applied (scopeSetup() ends with *OPC?),
% and triggered by force. The record of a free-running capture can predate the last setting change: one stopped 0.6 s
% after a change from 2 V/div to 5 mV/div held the 2 V/div record, with a preamble for 5 mV/div. The trigger level
% must be out of the signal's reach, so that only the forced trigger completes the acquisition.
scopeWrite(S, ':STOP');
scopeWaitStatus(S, 'STOP', 5);
scopeWrite(S, ':SINGle');
if ~scopeWaitStatus(S, 'WAIT', 5)
    error('PulsePal3_QCTest:scope', 'The oscilloscope did not arm.');
end
scopeWrite(S, ':TFORce');
if ~scopeWaitStatus(S, 'STOP', 2 + 2*recordLength(S))
    error('PulsePal3_QCTest:scope', 'The oscilloscope did not complete a forced acquisition.');
end
rec = scopeRead(S);
end

function rec = scopeTriggered(S, fire)
% Arms a single acquisition, calls fire() to make the device play, and reads the record once the scope has triggered
scopeWrite(S, ':STOP');
scopeWaitStatus(S, 'STOP', 5);
scopeWrite(S, ':SINGle');
if ~scopeWaitStatus(S, 'WAIT', 5)
    error('PulsePal3_QCTest:scope', 'The oscilloscope did not arm.');
end
fire();
if ~scopeWaitStatus(S, 'STOP', 2 + 2*recordLength(S))
    scopeWrite(S, ':STOP');
    error('PulsePal3_QCTest:scope', ['The oscilloscope did not trigger (%s rising through %s V): that output ' ...
        'channel did not play, or plays far off.'], S.memo(':TRIGger:EDGE:SOURce'), S.memo(':TRIGger:EDGE:LEVel'));
end
rec = scopeRead(S);
end

function seconds = recordLength(S)
if S.isMSO5000
    seconds = 10*S.memo('tpd');
else
    seconds = 12*S.memo('tpd');
end
end

function rec = scopeRead(S)
% Reads all four channels from RAW memory. rec(ch): t (s from the trigger), v (V), dt, clipped (fraction of samples
% at the ends of the 8-bit range).
points = S.memo('points');
if S.isMSO5000
    chunk = 4e6;
else
    chunk = 250000;
end
rec = struct('t', {}, 'v', {}, 'dt', {}, 'clipped', {});
scopeWrite(S, ':WAVeform:MODE RAW');
scopeWrite(S, ':WAVeform:FORMat BYTE');
for ch = 1:4
    scopeWrite(S, sprintf(':WAVeform:SOURce CHANnel%d', ch));
    pre = sscanf(strrep(scopeQuery(S, ':WAVeform:PREamble?'), ',', ' '), '%f');
    if numel(pre) ~= 10
        error('PulsePal3_QCTest:scope', 'The oscilloscope sent an invalid waveform preamble.');
    end
    [xInc, xOrigin, xRef, yInc, yOrigin, yRef] = deal(pre(5), pre(6), pre(7), pre(8), pre(9), pre(10));
    codesPerDiv = str2double(S.memo(sprintf(':CHANnel%d:SCALe', ch)))/yInc;
    if codesPerDiv < 10 || codesPerDiv > 60
        error('PulsePal3_QCTest:scope', 'Channel %d: the scope is not at the scale it was set to.', ch);
    end
    if S.isMSO5000 && abs(xInc*points/(10*S.memo('tpd')) - 1) > 0.01
        error('PulsePal3_QCTest:scope', 'Channel %d: the scope is not at the timebase it was set to.', ch);
    end
    codes = zeros(points, 1, 'uint8');
    for first = 1:chunk:points
        last = min(points, first + chunk - 1);
        scopeWrite(S, sprintf(':WAVeform:STARt %d', first));
        scopeWrite(S, sprintf(':WAVeform:STOP %d', last));
        block = scopeReadBlock(S, ':WAVeform:DATA?');
        if numel(block) ~= last - first + 1
            error('PulsePal3_QCTest:scope', 'Channel %d: asked for %d points, received %d.', ch, ...
                last - first + 1, numel(block));
        end
        codes(first:last) = block;
    end
    previousKey = sprintf('codes%d', ch);
    if isKey(S.memo, previousKey) && min(codes) ~= max(codes) && isequal(S.memo(previousKey), codes)
        error('PulsePal3_QCTest:scope', ['Channel %d: the oscilloscope returned the same record as the last ' ...
            'capture: it did not acquire a new one.'], ch);
    end
    S.memo(previousKey) = codes;
    rec(ch).v = (double(codes) - yOrigin - yRef)*yInc;
    rec(ch).t = ((0:points - 1)' - xRef)*xInc + xOrigin;
    rec(ch).dt = xInc;
    rec(ch).clipped = mean(codes == 0 | codes == 255);
end
end

function data = scopeReadBlock(S, command)
% Reads an IEEE 488.2 definite-length block. Right after a stop, the MSO5000 can send a block's header followed by
% nothing but its line feed: that is retried.
c = S.client;
for attempt = 1:6
    flush(c, 'input');
    writeline(c, command);
    first = readBytes(c, 1);
    while first == 10 || first == 13
        first = readBytes(c, 1);
    end
    if first ~= '#'
        error('PulsePal3_QCTest:scope', 'Expected a data block from the oscilloscope.');
    end
    nDigits = str2double(char(readBytes(c, 1)'));
    nBytes = str2double(char(readBytes(c, nDigits)'));
    waitTimer = tic;
    while c.NumBytesAvailable < 2 && toc(waitTimer) < 0.5
        pause(0.002);
    end
    if c.NumBytesAvailable == 1
        lone = readBytes(c, 1);
        if lone == 10 % Empty block
            pause(0.3);
            continue
        end
        data = [lone; readBytes(c, nBytes - 1)];
    else
        data = readBytes(c, nBytes);
    end
    waitTimer = tic;
    while c.NumBytesAvailable < 1 && toc(waitTimer) < 0.1
        pause(0.001);
    end
    if c.NumBytesAvailable > 0
        readBytes(c, 1); % The block's line feed
    end
    return
end
error('PulsePal3_QCTest:scope', 'The oscilloscope sent no waveform data.');
end

function bytes = readBytes(c, n)
bytes = uint8(read(c, n, 'uint8'));
bytes = bytes(:);
if numel(bytes) < n
    error('PulsePal3_QCTest:scope', 'The oscilloscope did not reply in time.');
end
end

function scopeWrite(S, command)
writeline(S.client, command);
end

function reply = scopeQuery(S, command)
flush(S.client, 'input');
writeline(S.client, command);
reply = readline(S.client);
if isempty(reply)
    error('PulsePal3_QCTest:scope', 'The oscilloscope did not reply to %s.', command);
end
reply = strtrim(char(reply));
end

function reached = scopeWaitStatus(S, status, timeout)
waitTimer = tic;
reached = false;
while toc(waitTimer) < timeout
    if strcmp(scopeQuery(S, ':TRIGger:STATus?'), status)
        reached = true;
        return
    end
    pause(0.01);
end
end

function errorList = scopeErrors(S)
errorList = {};
for i = 1:20
    reply = scopeQuery(S, ':SYSTem:ERRor?');
    if startsWith(reply, '0,') || startsWith(reply, '+0,')
        return
    end
    errorList{end + 1} = reply; %#ok<AGROW>
end
end

function scopeLiveView(S)
% Leaves the scope running, showing all four outputs
scopeSetup(S, 2*[1 1 1 1], [0 0 0 0], 1e-3, 0, 1e5, 1, 1);
scopeWrite(S, ':TRIGger:SWEep AUTO');
scopeWrite(S, ':RUN');
end

% ==== Analysis ====

function [tUp, tDown] = crossings(t, v, level, hysteresis)
% Times at which v crosses level upwards and downwards, interpolated between samples. A crossing counts once v has
% gone past level by hysteresis, so noise around the level makes no extra crossings.
t = t(:);
v = v(:);
state = zeros(size(v));
state(v > level + hysteresis) = 1;
state(v < level - hysteresis) = -1;
index = find(state ~= 0);
s = state(index);
flips = find(diff(s) ~= 0);
tUp = zeros(0, 1);
tDown = zeros(0, 1);
for f = flips'
    a = index(f);
    seg = v(a:index(f + 1)) - level;
    if s(f + 1) > 0
        j = find(seg(1:end - 1) < 0 & seg(2:end) >= 0, 1, 'last');
    else
        j = find(seg(1:end - 1) > 0 & seg(2:end) <= 0, 1, 'last');
    end
    j = a + j - 1;
    tc = t(j) + (level - v(j))*(t(j + 1) - t(j))/(v(j + 1) - v(j));
    if s(f + 1) > 0
        tUp(end + 1, 1) = tc; %#ok<AGROW>
    else
        tDown(end + 1, 1) = tc; %#ok<AGROW>
    end
end
end

function [up, down, upNeg, downNeg] = phaseEdges(t, v, highLevel, lowLevel)
% Edges of biphasic pulses: into (up) and out of (down) phase 1 through highLevel, into (downNeg) and out of (upNeg)
% phase 2 through lowLevel
[up, down] = crossings(t, v, highLevel, 0.4);
[upNeg, downNeg] = crossings(t, v, lowLevel, 0.4);
end

function volts = dacVolts(v)
% The voltage the device plays for v: its DAC code, rounded as PulsePalDevice rounds it (halves to even)
code = (v + 10)/20*65535;
rounded = round(code);
halfway = abs(code - fix(code)) == 0.5;
rounded(halfway) = 2*round(code(halfway)/2);
volts = min(max(rounded, 0), 65535)/65535*20 - 10;
end

% ==== Small helpers ====

function note = clippedNote(rec)
note = '';
if rec.clipped > 0
    note = ' (off the scope''s screen)';
end
end

function out = conditional(condition, a, b)
if condition
    out = a;
else
    out = b;
end
end

function quietly(f)
evalc('f()');
end

function folder = defaultRecordFolder()
folder = userpath;
if isempty(folder)
    folder = pwd;
end
folder = fullfile(folder, 'PulsePal3_QC');
end

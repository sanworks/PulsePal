/*
----------------------------------------------------------------------------

This file is part of the Pulse Pal Project
Copyright (C) 2026 Sanworks LLC, Rochester, NY, USA

----------------------------------------------------------------------------

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, version 3.

This program is distributed  WITHOUT ANY WARRANTY and without even the
implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/

// Offline tests for the bytes PulsePal sends to the device.
//
// These tests need no Pulse Pal: a fake serial port records what the class writes, and replies to its reads. They
// check that the class still matches the firmware's serial protocol, documented in /Firmware/PROTOCOL.md, and that
// op 92 and op 73 messages match those sent by the Python class (/Python/PulsePal/PulsePal.py).
//
// Build and run them with CMake (see README.md):
//   ctest --test-dir build -C Release --output-on-failure

#include <cmath>
#include <deque>
#include <initializer_list>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "PulsePal.h"

typedef std::vector<uint8_t> Bytes;

// Records writes, and replies to reads with the bytes in `replies`. Once those run out, each read returns `ack`,
// unless `silent` is set, in which case it times out.
class FakeSerialPort : public SerialPort
{
public:
    FakeSerialPort() : portOpen(false), ack(1), silent(false) {}

    bool open(const std::string& portName) override { openedName = portName; portOpen = true; return true; }
    void close() override { portOpen = false; }
    bool isOpen() const override { return portOpen; }
    void discardInput() override {}
    bool write(const uint8_t* data, size_t nBytes) override
    {
        writes.push_back(Bytes(data, data + nBytes));
        return true;
    }
    size_t read(uint8_t* data, size_t nBytes, unsigned int) override
    {
        reads.push_back(nBytes);
        for (size_t i = 0; i < nBytes; i++) {
            if (!replies.empty()) {
                data[i] = replies.front();
                replies.pop_front();
            } else if (silent) {
                return i;
            } else {
                data[i] = ack;
            }
        }
        return nBytes;
    }
    std::string lastError() const override { return "fake error"; }

    // Queues the handshake reply, and the op 94 reply on firmware v22
    void queueConnectReplies(uint32_t firmwareVersion, uint8_t hardwareVersion)
    {
        uint8_t handshake[5] = {75, (uint8_t)firmwareVersion, 0, 0, 0};
        replies.insert(replies.end(), handshake, handshake + 5);
        if (firmwareVersion > 21) {
            uint32_t maxPulses = (hardwareVersion > 2) ? 10000 : 5000;
            uint8_t info[10] = {hardwareVersion, 50, 0, 0, 0, (uint8_t)((hardwareVersion > 2) ? 4 : 2),
                                (uint8_t)maxPulses, (uint8_t)(maxPulses >> 8), 0, 0};
            replies.insert(replies.end(), info, info + 10);
        }
    }

    std::string openedName;
    bool portOpen;
    uint8_t ack;
    bool silent;
    std::deque<uint8_t> replies;
    std::vector<Bytes> writes;
    std::vector<size_t> reads;
};

// Captures what PulsePal prints to std::cerr, so that expected errors do not clutter the output and can be checked
class CaptureErrors
{
public:
    CaptureErrors() : previous(std::cerr.rdbuf(captured.rdbuf())) {}
    ~CaptureErrors() { std::cerr.rdbuf(previous); }
    std::string text() const { return captured.str(); }
private:
    std::ostringstream captured;
    std::streambuf* previous;
};

static int nChecks = 0;
static int nFailures = 0;

#define CHECK(condition) check((condition), #condition, __FILE__, __LINE__)

static void check(bool condition, const char* text, const char* file, int line)
{
    nChecks++;
    if (!condition) {
        nFailures++;
        std::cout << "  FAILED: " << text << " (" << file << ":" << line << ")" << std::endl;
    }
}

static std::string toString(const Bytes& bytes)
{
    std::ostringstream text;
    text << "{";
    for (size_t i = 0; i < bytes.size(); i++) {
        text << (i ? ", " : "") << (int)bytes[i];
    }
    text << "}";
    return text.str();
}

#define CHECK_BYTES(actual, expected) checkBytes((actual), (expected), __FILE__, __LINE__)

static void checkBytes(const Bytes& actual, const Bytes& expected, const char* file, int line)
{
    nChecks++;
    if (actual != expected) {
        nFailures++;
        std::cout << "  FAILED: bytes differ (" << file << ":" << line << ")" << std::endl
                  << "    expected " << toString(expected) << std::endl
                  << "    actual   " << toString(actual) << std::endl;
    }
}

static Bytes bytes(std::initializer_list<int> values)
{
    Bytes result;
    for (int value : values) {
        result.push_back((uint8_t)value);
    }
    return result;
}

static bool contains(const std::string& text, const std::string& part)
{
    return text.find(part) != std::string::npos;
}

// Connects a PulsePal to a fake port, and clears the record of the connect sequence
static void connect(PulsePal& pulsePal, FakeSerialPort& port, uint32_t firmwareVersion, uint8_t hardwareVersion)
{
    port.queueConnectReplies(firmwareVersion, hardwareVersion);
    bool connected = pulsePal.initialize("COM1");
    CHECK(connected);
    port.writes.clear();
    port.reads.clear();
}

// ---------------------------------------------------------------------------------------------------------------
// Reference messages from the Python class, for a parameter set in which every parameter and channel has a distinct
// value (see fillDistinctParams()). Generated with the fake port in /Python/PulsePal/tests/test_protocol.py.
// ---------------------------------------------------------------------------------------------------------------
static const uint8_t PythonOp92[184] = {
    213, 92, 20, 0, 0, 0, 40, 0, 0, 0, 60, 0, 0, 0, 80, 0, 0, 0, 40, 0, 0, 0, 80, 0,
    0, 0, 120, 0, 0, 0, 160, 0, 0, 0, 60, 0, 0, 0, 120, 0, 0, 0, 180, 0, 0, 0, 240, 0,
    0, 0, 80, 0, 0, 0, 160, 0, 0, 0, 240, 0, 0, 0, 64, 1, 0, 0, 100, 0, 0, 0, 200, 0,
    0, 0, 44, 1, 0, 0, 144, 1, 0, 0, 120, 0, 0, 0, 240, 0, 0, 0, 104, 1, 0, 0, 224, 1,
    0, 0, 208, 7, 0, 0, 160, 15, 0, 0, 112, 23, 0, 0, 64, 31, 0, 0, 140, 0, 0, 0, 24, 1,
    0, 0, 164, 1, 0, 0, 48, 2, 0, 0, 204, 140, 153, 153, 102, 166, 50, 179, 51, 115, 102, 102, 153, 89,
    204, 76, 102, 134, 204, 140, 51, 147, 153, 153, 1, 0, 1, 0, 0, 1, 2, 3, 0, 1, 0, 1, 0, 0,
    1, 1, 0, 0, 1, 0, 1, 0, 1, 0, 0, 1, 0, 1, 1, 2,
};
static const uint8_t PythonOp73[180] = {
    213, 73, 20, 0, 0, 0, 40, 0, 0, 0, 60, 0, 0, 0, 80, 0, 0, 0, 100, 0, 0, 0, 120, 0,
    0, 0, 208, 7, 0, 0, 140, 0, 0, 0, 40, 0, 0, 0, 80, 0, 0, 0, 120, 0, 0, 0, 160, 0,
    0, 0, 200, 0, 0, 0, 240, 0, 0, 0, 160, 15, 0, 0, 24, 1, 0, 0, 60, 0, 0, 0, 120, 0,
    0, 0, 180, 0, 0, 0, 240, 0, 0, 0, 44, 1, 0, 0, 104, 1, 0, 0, 112, 23, 0, 0, 164, 1,
    0, 0, 80, 0, 0, 0, 160, 0, 0, 0, 240, 0, 0, 0, 64, 1, 0, 0, 144, 1, 0, 0, 224, 1,
    0, 0, 64, 31, 0, 0, 48, 2, 0, 0, 204, 140, 51, 115, 102, 134, 153, 153, 102, 102, 204, 140, 102, 166,
    153, 89, 51, 147, 50, 179, 204, 76, 153, 153, 1, 0, 0, 0, 0, 1, 1, 0, 1, 2, 0, 1, 0, 0,
    1, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 2,
};
static const uint8_t PythonOp92Defaults[184] = {
    213, 92, 20, 0, 0, 0, 20, 0, 0, 0, 20, 0, 0, 0, 20, 0, 0, 0, 20, 0, 0, 0, 20, 0,
    0, 0, 20, 0, 0, 0, 20, 0, 0, 0, 20, 0, 0, 0, 20, 0, 0, 0, 20, 0, 0, 0, 20, 0,
    0, 0, 200, 0, 0, 0, 200, 0, 0, 0, 200, 0, 0, 0, 200, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 32, 78, 0, 0, 32, 78, 0, 0, 32, 78, 0, 0, 32, 78, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 255, 191, 255, 191, 255, 191, 255, 191, 0, 64, 0, 64, 0, 64,
    0, 64, 0, 128, 0, 128, 0, 128, 0, 128, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0,
};

// The parameter set used to generate PythonOp92 and PythonOp73. Continuous loop mode (on channel 3) is set
// separately, with setContinuousLoop(). Channel 4's +4V and -4V are exactly halfway between two DAC codes, so they
// check that halfway values are rounded as in Python.
static void fillDistinctParams(PulsePal& pulsePal, bool firmwareV21)
{
    for (int ch = 1; ch < 5; ch++) {
        PulsePal::OutputParams& p = pulsePal.currentOutputParams[ch];
        p.isBiphasic = ch % 2;
        p.phase1Voltage = (float)ch;
        p.phase2Voltage = (float)-ch;
        p.restingVoltage = (float)(0.5 * ch);
        p.phase1Duration = (float)(0.001 * ch);
        p.interPhaseInterval = (float)(0.002 * ch);
        p.phase2Duration = (float)(0.003 * ch);
        p.interPulseInterval = (float)(0.004 * ch);
        p.burstDuration = (float)(0.005 * ch);
        p.interBurstInterval = (float)(0.006 * ch);
        p.pulseTrainDuration = (float)(0.1 * ch);
        p.pulseTrainDelay = (float)(0.007 * ch);
        p.linkTriggerChannel1 = ch % 2;
        p.linkTriggerChannel2 = 1 - (ch % 2);
        p.customTrainID = firmwareV21 ? ((ch - 1) % 3) : (ch - 1);
        p.customTrainTarget = ((ch == 2) || (ch == 4)) ? 1 : 0;
        p.customTrainLoop = (ch > 2) ? 1 : 0;
    }
    pulsePal.currentInputParams[1].triggerMode = 1;
    pulsePal.currentInputParams[2].triggerMode = 2;
}

// ---------------------------------------------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------------------------------------------

static void test_connect_to_pulse_pal_3()
{
    FakeSerialPort port;
    PulsePal pulsePal(&port);
    port.queueConnectReplies(22, 3);
    CHECK(pulsePal.initialize("COM7"));
    CHECK(port.openedName == "COM7");
    CHECK(pulsePal.getFirmwareVersion() == 22);
    CHECK(pulsePal.getHardwareVersion() == 3);
    // Handshake, hardware info, both trigger channels out of param sync mode (op 91), then defaults. No client name
    // (op 89): the program sets that with setClientIDString()
    CHECK(port.writes.size() == 4);
    if (port.writes.size() == 4) {
        CHECK_BYTES(port.writes[0], bytes({213, 72}));
        CHECK_BYTES(port.writes[1], bytes({213, 94}));
        CHECK_BYTES(port.writes[2], bytes({213, 91, 128, 0, 0}));
        CHECK_BYTES(port.writes[3], Bytes(PythonOp92Defaults, PythonOp92Defaults + 184));
    }
    // Handshake reply, hardware info, and the confirm bytes of ops 91 and 92
    CHECK((port.reads == std::vector<size_t>{5, 10, 1, 1}));
    CHECK(port.replies.empty());
}

static void test_connect_to_pulse_pal_2_on_firmware_v22()
{
    FakeSerialPort port;
    PulsePal pulsePal(&port);
    port.queueConnectReplies(22, 2);
    CHECK(pulsePal.initialize("COM1"));
    CHECK(pulsePal.getHardwareVersion() == 2);
    // Pulse Pal 2 has no param sync mode, so there is no op 91
    CHECK(port.writes.size() == 3);
    if (port.writes.size() == 3) {
        CHECK_BYTES(port.writes[1], bytes({213, 94}));
        CHECK(port.writes[2][1] == 92);
    }
}

static void test_connect_to_firmware_v21()
{
    FakeSerialPort port;
    PulsePal pulsePal(&port);
    port.queueConnectReplies(21, 2);
    std::ostringstream notice;
    std::streambuf* previous = std::cout.rdbuf(notice.rdbuf());
    bool connected = pulsePal.initialize("COM1");
    std::cout.rdbuf(previous);
    CHECK(connected);
    CHECK(contains(notice.str(), "Update to v22"));
    CHECK(pulsePal.getFirmwareVersion() == 21);
    CHECK(pulsePal.getHardwareVersion() == 2);
    // No op 94 on firmware v21, and parameters are programmed with op 73
    CHECK(port.writes.size() == 2);
    if (port.writes.size() == 2) {
        CHECK_BYTES(port.writes[0], bytes({213, 72}));
        CHECK(port.writes[1][1] == 73);
        CHECK(port.writes[1].size() == 180);
    }
}

static void test_connect_fails_on_a_bad_handshake_or_unsupported_firmware()
{
    struct Case { uint8_t replyByte; uint32_t firmwareVersion; const char* message; };
    const Case cases[] = {
        {74, 22, "did not return the Pulse Pal handshake"},
        {87, 1, "runs Wave Pal firmware (v1)"}, // Wave Pal's reply to the same op
        {75, 19, "Pulse Pal 1"},
        {75, 20, "update the firmware"},
        {75, 23, "supports firmware up to v22"},
    };
    for (const Case& c : cases) {
        FakeSerialPort port;
        PulsePal pulsePal(&port);
        uint8_t reply[5] = {c.replyByte, (uint8_t)c.firmwareVersion, 0, 0, 0};
        port.replies.insert(port.replies.end(), reply, reply + 5);
        CaptureErrors errors;
        CHECK(!pulsePal.initialize("COM1"));
        CHECK(contains(errors.text(), c.message));
        CHECK(!port.isOpen());
        CHECK(port.writes.size() == 1); // Only the handshake
        CHECK(pulsePal.getFirmwareVersion() == 0);
    }

    // No reply at all
    FakeSerialPort port;
    port.silent = true;
    PulsePal pulsePal(&port);
    CaptureErrors errors;
    CHECK(!pulsePal.initialize("COM1"));
    CHECK(contains(errors.text(), "did not return the Pulse Pal handshake"));
    CHECK(!port.isOpen());
}

static void test_sync_all_params_matches_python_on_firmware_v22()
{
    FakeSerialPort port;
    PulsePal pulsePal(&port);
    connect(pulsePal, port, 22, 3);
    CHECK(pulsePal.setContinuousLoop(3, 1));
    CHECK_BYTES(port.writes[0], bytes({213, 82, 3, 1}));
    fillDistinctParams(pulsePal, false);
    CHECK(pulsePal.syncAllParams());
    CHECK(port.writes.size() == 2);
    CHECK_BYTES(port.writes.back(), Bytes(PythonOp92, PythonOp92 + 184));
    CHECK((port.reads == std::vector<size_t>{1, 1}));
}

static void test_sync_all_params_matches_python_on_firmware_v21()
{
    FakeSerialPort port;
    PulsePal pulsePal(&port);
    std::streambuf* previous = std::cout.rdbuf(NULL); // Hide the firmware update notice
    connect(pulsePal, port, 21, 2);
    std::cout.rdbuf(previous);
    fillDistinctParams(pulsePal, true);
    CHECK(pulsePal.syncAllParams());
    CHECK(port.writes.size() == 1);
    CHECK_BYTES(port.writes.back(), Bytes(PythonOp73, PythonOp73 + 180));
}

static void test_sync_all_params_sends_nothing_if_a_field_is_out_of_range()
{
    FakeSerialPort port;
    PulsePal pulsePal(&port);
    connect(pulsePal, port, 22, 2);
    struct Case { void (*edit)(PulsePal&); const char* message; };
    const Case cases[] = {
        {[](PulsePal& p) { p.currentOutputParams[2].phase1Voltage = 10.5f; }, "phase1Voltage on output channel 2"},
        {[](PulsePal& p) { p.currentOutputParams[4].pulseTrainDelay = -1; }, "pulseTrainDelay on output channel 4"},
        {[](PulsePal& p) { p.currentOutputParams[1].phase1Duration = 0.00005f; }, "phase1Duration"},
        {[](PulsePal& p) { p.currentOutputParams[1].interPulseInterval = NAN; }, "interPulseInterval"},
        {[](PulsePal& p) { p.currentOutputParams[3].customTrainID = 3; }, "customTrainID on output channel 3"},
        {[](PulsePal& p) { p.currentOutputParams[1].isBiphasic = 2; }, "isBiphasic"},
        {[](PulsePal& p) { p.currentInputParams[2].triggerMode = 3; }, "triggerMode on trigger channel 2"},
        {[](PulsePal& p) { p.currentOutputParams[1].customTrainTarget = 1; }, "burstDuration must be above 0"},
    };
    for (const Case& c : cases) {
        pulsePal.setDefaultParameters();
        c.edit(pulsePal);
        CaptureErrors errors;
        CHECK(!pulsePal.syncAllParams());
        CHECK(contains(errors.text(), c.message));
    }
    CHECK(port.writes.empty());
}

static void test_single_parameters_use_op_74()
{
    FakeSerialPort port;
    PulsePal pulsePal(&port);
    connect(pulsePal, port, 22, 3);

    CHECK(pulsePal.setBiphasic(4, true));
    CHECK(pulsePal.setPhase1Voltage(2, 5));            // DAC code 49151
    CHECK(pulsePal.setPhase2Voltage(1, -10));          // 0
    CHECK(pulsePal.setRestingVoltage(3, 0));           // 32768
    CHECK(pulsePal.setPhase1Duration(1, 0.0015f));     // 30 cycles
    CHECK(pulsePal.setInterPhaseInterval(2, 0));
    CHECK(pulsePal.setPhase2Duration(3, 0.0001f));     // 2 cycles, the minimum
    CHECK(pulsePal.setInterPulseInterval(4, 0.25f));   // 5000 cycles
    CHECK(pulsePal.setBurstDuration(1, 1));
    CHECK(pulsePal.setBurstInterval(2, 3600));         // 72000000 cycles, the maximum
    CHECK(pulsePal.setPulseTrainDuration(3, 2));
    CHECK(pulsePal.setPulseTrainDelay(4, 0));
    CHECK(pulsePal.setTrigger1Link(1, 0));
    CHECK(pulsePal.setTrigger2Link(2, 1));
    CHECK(pulsePal.setCustomTrainID(3, 4));            // Custom train 4 exists on Pulse Pal 3
    CHECK(pulsePal.setCustomTrainTarget(4, 1));
    CHECK(pulsePal.setCustomTrainLoop(1, 1));
    CHECK(pulsePal.setTriggerMode(2, 3));              // Param sync mode exists on Pulse Pal 3

    const Bytes expected[] = {
        bytes({213, 74, 1, 4, 1}),
        bytes({213, 74, 2, 2, 255, 191}),
        bytes({213, 74, 3, 1, 0, 0}),
        bytes({213, 74, 17, 3, 0, 128}),
        bytes({213, 74, 4, 1, 30, 0, 0, 0}),
        bytes({213, 74, 5, 2, 0, 0, 0, 0}),
        bytes({213, 74, 6, 3, 2, 0, 0, 0}),
        bytes({213, 74, 7, 4, 136, 19, 0, 0}),
        bytes({213, 74, 8, 1, 32, 78, 0, 0}),
        bytes({213, 74, 9, 2, 0, 162, 74, 4}),
        bytes({213, 74, 10, 3, 64, 156, 0, 0}),
        bytes({213, 74, 11, 4, 0, 0, 0, 0}),
        bytes({213, 74, 12, 1, 0}),
        bytes({213, 74, 13, 2, 1}),
        bytes({213, 74, 14, 3, 4}),
        bytes({213, 74, 15, 4, 1}),
        bytes({213, 74, 16, 1, 1}),
        bytes({213, 74, 128, 2, 3}),
    };
    const size_t nExpected = sizeof(expected) / sizeof(expected[0]);
    CHECK(port.writes.size() == nExpected);
    for (size_t i = 0; (i < nExpected) && (i < port.writes.size()); i++) {
        CHECK_BYTES(port.writes[i], expected[i]);
    }
    CHECK(port.reads == std::vector<size_t>(nExpected, 1)); // One confirm byte per command

    // The local copy follows each change
    CHECK(pulsePal.currentOutputParams[4].isBiphasic == 1);
    CHECK(pulsePal.currentOutputParams[2].phase1Voltage == 5);
    CHECK(pulsePal.currentOutputParams[4].interPulseInterval == 0.25f);
    CHECK(pulsePal.currentOutputParams[4].interPhaseInterval == 0.001f); // The legacy class set this one by mistake
    CHECK(pulsePal.currentOutputParams[2].interBurstInterval == 3600);
    CHECK(pulsePal.currentOutputParams[3].customTrainID == 4);
    CHECK(pulsePal.currentInputParams[2].triggerMode == 3);
}

static void test_out_of_range_arguments_send_nothing()
{
    FakeSerialPort port;
    PulsePal pulsePal(&port);
    connect(pulsePal, port, 22, 2);
    CaptureErrors errors;
    CHECK(!pulsePal.setPhase1Voltage(1, 10.01f));
    CHECK(!pulsePal.setRestingVoltage(1, NAN));
    CHECK(!pulsePal.setPhase1Duration(1, 0.00005f));
    CHECK(!pulsePal.setPulseTrainDuration(1, 3601));
    CHECK(!pulsePal.setPulseTrainDelay(1, -0.001f));
    CHECK(!pulsePal.setBurstDuration(1, INFINITY));
    CHECK(!pulsePal.setPhase1Voltage(0, 5));
    CHECK(!pulsePal.setPhase1Voltage(5, 5));
    CHECK(!pulsePal.setTrigger1Link(1, 2));
    CHECK(!pulsePal.setCustomTrainID(1, 3));      // Pulse Pal 2 has custom trains 1 and 2
    CHECK(!pulsePal.setTriggerMode(1, 3));        // Param sync mode is Pulse Pal 3 only
    CHECK(!pulsePal.setTriggerMode(3, 0));
    CHECK(!pulsePal.setContinuousLoop(1, 2));
    CHECK(!pulsePal.setFixedVoltage(1, -11));
    CHECK(!pulsePal.triggerChannel(5));
    CHECK(!pulsePal.setClientIDString("TooLongName"));
    CHECK(!pulsePal.updateDisplay(std::string(200, 'a'), std::string(55, 'b')));
    CHECK(port.writes.empty());
    CHECK(contains(errors.text(), "setCustomTrainID(): customTrainID on output channel 1 was 3. It must be an integer from 0 to 2"));
    CHECK(contains(errors.text(), "setTriggerMode(): trigger channel 3 does not exist"));
    CHECK(pulsePal.currentOutputParams[1].phase1Voltage == 5); // Unchanged
}

static void test_a_rejected_or_unanswered_command_returns_false()
{
    FakeSerialPort port;
    PulsePal pulsePal(&port);
    connect(pulsePal, port, 22, 3);

    port.ack = 0;
    {
        CaptureErrors errors;
        CHECK(!pulsePal.setPhase1Voltage(1, 2));
        CHECK(contains(errors.text(), "rejected"));
    }
    CHECK(pulsePal.currentOutputParams[1].phase1Voltage == 5); // Not changed, because the device rejected it

    port.silent = true;
    {
        CaptureErrors errors;
        CHECK(!pulsePal.syncAllParams());
        CHECK(contains(errors.text(), "COMM. FAILURE!"));
    }
}

static void test_commands_without_replies()
{
    FakeSerialPort port;
    PulsePal pulsePal(&port);
    connect(pulsePal, port, 22, 3);
    CHECK(pulsePal.triggerChannel(3));
    CHECK(pulsePal.triggerChannels(1, 0, 1, 1));
    CHECK(pulsePal.abortPulseTrains());
    CHECK(pulsePal.updateDisplay("Hi", "There"));
    CHECK(pulsePal.setClientIDString("MyApp1"));
    CHECK(port.writes.size() == 5);
    if (port.writes.size() == 5) {
        CHECK_BYTES(port.writes[0], bytes({213, 77, 4}));
        CHECK_BYTES(port.writes[1], bytes({213, 77, 13}));
        CHECK_BYTES(port.writes[2], bytes({213, 80}));
        CHECK_BYTES(port.writes[3], bytes({213, 78, 8, 'H', 'i', 254, 'T', 'h', 'e', 'r', 'e'}));
        CHECK_BYTES(port.writes[4], bytes({213, 89, 'M', 'y', 'A', 'p', 'p', '1'}));
    }
    CHECK(port.reads.empty());
}

static void test_fixed_voltage_and_continuous_loop_read_a_confirm_byte()
{
    FakeSerialPort port;
    PulsePal pulsePal(&port);
    connect(pulsePal, port, 22, 3);
    CHECK(pulsePal.setFixedVoltage(1, -10));
    CHECK(pulsePal.setFixedVoltage(4, 10));
    CHECK(pulsePal.setContinuousLoop(2, 1));
    CHECK(port.writes.size() == 3);
    if (port.writes.size() == 3) {
        CHECK_BYTES(port.writes[0], bytes({213, 79, 1, 0, 0}));
        CHECK_BYTES(port.writes[1], bytes({213, 79, 4, 255, 255}));
        CHECK_BYTES(port.writes[2], bytes({213, 82, 2, 1}));
    }
    CHECK((port.reads == std::vector<size_t>{1, 1, 1}));

    // Op 92 keeps channel 2 looping, instead of stopping it
    port.writes.clear();
    CHECK(pulsePal.syncAllParams());
    const Bytes& sync = port.writes.back();
    const size_t continuousLoopOffset = 2 + 128 + 24 + 16; // Op, times, voltages, 4 byte arrays
    CHECK_BYTES(Bytes(sync.begin() + continuousLoopOffset, sync.begin() + continuousLoopOffset + 4), bytes({0, 1, 0, 0}));
}

static void test_custom_trains_use_op_95_with_a_zero_based_index()
{
    FakeSerialPort port;
    PulsePal pulsePal(&port);
    connect(pulsePal, port, 22, 3);
    const float times[3] = {0, 0.001f, 0.0025f};
    const float voltages[3] = {10, -10, 0};
    CHECK(pulsePal.sendCustomPulseTrain(3, 3, times, voltages));
    CHECK_BYTES(port.writes.back(), bytes({213, 95, 2, 3, 0, 0, 0,
                                           0, 0, 0, 0, 20, 0, 0, 0, 50, 0, 0, 0,
                                           255, 255, 0, 0, 0, 128}));
    CHECK((port.reads == std::vector<size_t>{1}));

    // A full-size train on Pulse Pal 3 is sent in one write
    std::vector<float> longTimes(10000), longVoltages(10000, 5);
    for (int i = 0; i < 10000; i++) {
        longTimes[i] = (float)i * 0.0001f;
    }
    CHECK(pulsePal.sendCustomPulseTrain(4, 10000, longTimes.data(), longVoltages.data()));
    CHECK(port.writes.back().size() == 3 + 4 + (6 * 10000));
    CHECK(port.writes.back()[2] == 3);
}

static void test_custom_trains_use_ops_75_and_76_on_firmware_v21()
{
    FakeSerialPort port;
    PulsePal pulsePal(&port);
    std::streambuf* previous = std::cout.rdbuf(NULL);
    connect(pulsePal, port, 21, 2);
    std::cout.rdbuf(previous);
    const float times[1] = {0.5f};
    const float voltages[1] = {5};
    CHECK(pulsePal.sendCustomPulseTrain(1, 1, times, voltages));
    CHECK(pulsePal.sendCustomPulseTrain(2, 1, times, voltages));
    CHECK(port.writes.size() == 2);
    if (port.writes.size() == 2) {
        CHECK_BYTES(port.writes[0], bytes({213, 75, 1, 0, 0, 0, 16, 39, 0, 0, 255, 191}));
        CHECK_BYTES(port.writes[1], bytes({213, 76, 1, 0, 0, 0, 16, 39, 0, 0, 255, 191}));
    }
}

static void test_custom_trains_reject_bad_ids_sizes_and_times()
{
    FakeSerialPort port;
    PulsePal pulsePal(&port);
    connect(pulsePal, port, 22, 2);
    const float times[2] = {0, 0.001f};
    const float voltages[2] = {1, 2};
    CaptureErrors errors;
    CHECK(!pulsePal.sendCustomPulseTrain(0, 2, times, voltages));
    CHECK(!pulsePal.sendCustomPulseTrain(3, 2, times, voltages)); // Pulse Pal 2 has custom trains 1 and 2
    CHECK(contains(errors.text(), "custom trains 1 to 2"));
    CHECK(!pulsePal.sendCustomPulseTrain(1, 0, times, voltages));

    std::vector<float> longTimes(5001), longVoltages(5001, 0);
    for (int i = 0; i < 5001; i++) {
        longTimes[i] = (float)i * 0.001f;
    }
    CHECK(!pulsePal.sendCustomPulseTrain(1, 5001, longTimes.data(), longVoltages.data()));
    CHECK(contains(errors.text(), "up to 5000 pulses"));

    const float sameCycle[2] = {0.001f, 0.00101f}; // Both round to 20 cycles
    CHECK(!pulsePal.sendCustomPulseTrain(1, 2, sameCycle, voltages));
    CHECK(contains(errors.text(), "pulse times must increase"));
    const float decreasing[2] = {0.002f, 0.001f};
    CHECK(!pulsePal.sendCustomPulseTrain(1, 2, decreasing, voltages));
    const float negative[2] = {-0.001f, 0.001f};
    CHECK(!pulsePal.sendCustomPulseTrain(1, 2, negative, voltages));
    const float tooLoud[2] = {1, 10.5f};
    CHECK(!pulsePal.sendCustomPulseTrain(1, 2, times, tooLoud));
    CHECK(port.writes.empty());
}

static void test_end_disconnects_and_closes_the_port()
{
    FakeSerialPort port;
    PulsePal pulsePal(&port);
    connect(pulsePal, port, 22, 3);
    pulsePal.end();
    CHECK(port.writes.size() == 1);
    if (port.writes.size() == 1) {
        CHECK_BYTES(port.writes[0], bytes({213, 81}));
    }
    CHECK(!port.isOpen());

    // Commands after end() report an error and send nothing
    CaptureErrors errors;
    CHECK(!pulsePal.triggerChannel(1));
    CHECK(contains(errors.text(), "not connected"));
    CHECK(port.writes.size() == 1);
    pulsePal.end(); // A second end() does nothing
    CHECK(port.writes.size() == 1);
}

static void test_commands_before_initialize_send_nothing()
{
    FakeSerialPort port;
    PulsePal pulsePal(&port);
    CaptureErrors errors;
    CHECK(!pulsePal.syncAllParams());
    CHECK(!pulsePal.setPhase1Voltage(1, 5));
    pulsePal.disconnectClient();
    CHECK(port.writes.empty());
}

int main()
{
    struct Test { const char* name; void (*run)(); };
    const Test tests[] = {
        {"connect to Pulse Pal 3", test_connect_to_pulse_pal_3},
        {"connect to Pulse Pal 2 on firmware v22", test_connect_to_pulse_pal_2_on_firmware_v22},
        {"connect to firmware v21", test_connect_to_firmware_v21},
        {"connect fails on a bad handshake or unsupported firmware", test_connect_fails_on_a_bad_handshake_or_unsupported_firmware},
        {"syncAllParams matches Python on firmware v22", test_sync_all_params_matches_python_on_firmware_v22},
        {"syncAllParams matches Python on firmware v21", test_sync_all_params_matches_python_on_firmware_v21},
        {"syncAllParams sends nothing if a field is out of range", test_sync_all_params_sends_nothing_if_a_field_is_out_of_range},
        {"single parameters use op 74", test_single_parameters_use_op_74},
        {"out of range arguments send nothing", test_out_of_range_arguments_send_nothing},
        {"a rejected or unanswered command returns false", test_a_rejected_or_unanswered_command_returns_false},
        {"commands without replies", test_commands_without_replies},
        {"fixed voltage and continuous loop read a confirm byte", test_fixed_voltage_and_continuous_loop_read_a_confirm_byte},
        {"custom trains use op 95 with a zero-based index", test_custom_trains_use_op_95_with_a_zero_based_index},
        {"custom trains use ops 75 and 76 on firmware v21", test_custom_trains_use_ops_75_and_76_on_firmware_v21},
        {"custom trains reject bad IDs, sizes and times", test_custom_trains_reject_bad_ids_sizes_and_times},
        {"end() disconnects and closes the port", test_end_disconnects_and_closes_the_port},
        {"commands before initialize() send nothing", test_commands_before_initialize_send_nothing},
    };
    for (const Test& test : tests) {
        int failuresBefore = nFailures;
        test.run();
        std::cout << ((nFailures == failuresBefore) ? "ok     " : "FAILED ") << test.name << std::endl;
    }
    std::cout << std::endl << nChecks << " checks, " << nFailures << " failed" << std::endl;
    return (nFailures == 0) ? 0 : 1;
}

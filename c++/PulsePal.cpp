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
// Originally programmed by Josh Seigle as part of the Open Ephys GUI, <http://open-ephys.org>
// Updated for Pulse Pal 3 and firmware v22 by Sanworks LLC. See PulsePal.h for usage.
//
// Note on replies: ops 73, 74, 75, 76, 79, 82, 91, 92 and 95 reply with a confirm byte (1 = executed, 0 = rejected).
// Each is read before the method returns. A confirm byte left unread would be taken as the reply to the next
// command, and every reply after that would be read one command late.

#include "PulsePal.h"

#include <cmath>
#include <iostream>
#include <sstream>
#include <vector>

static const uint8_t OpMenuByte = 213; // First byte of every command
static const uint8_t HandshakeReply = 75; // 'K'
static const uint8_t WavePalHandshakeReply = 87; // 'W': the device runs Wave Pal firmware (/Firmware/WavePal)
static const uint32_t OldestFirmwareSupported = 21;
static const uint32_t CurrentFirmwareVersion = 22;

// How long to wait for a reply. The device replies as soon as it has read and executed a command, which takes
// milliseconds. A missing reply usually means that the device is showing COMM. FAILURE! (see readConfirm()).
static const unsigned int ReplyTimeoutMs = 2000;

// Parameter ranges, as in the MATLAB class
static const double MaxVoltage = 10;         // Volts. Voltages are -MaxVoltage to +MaxVoltage
static const double MinPulseTime = 0.0001;   // Seconds. For phase durations, inter-pulse interval and train duration
static const double MaxTime = 3600;          // Seconds
static const uint16_t DACMax = 65535;        // DAC code for +10V. 0 is -10V
static const uint8_t TriggerModeParamSync = 3; // Pulse Pal 3 only

// Time parameters in the order ops 73 and 92 send them
static const uint8_t TimeParamCodes[8] = {4, 5, 6, 7, 8, 9, 10, 11};

// Number of bytes in a parameter's value in op 74, as in paramValueBytes() in the firmware
static uint8_t paramValueBytes(uint8_t paramCode)
{
    if ((paramCode == 2) || (paramCode == 3) || (paramCode == 17)) {
        return 2; // Voltages, as DAC codes
    }
    if ((paramCode >= 4) && (paramCode <= 11)) {
        return 4; // Times, in hardware timer cycles
    }
    return 1;
}

static bool isTimeParam(uint8_t paramCode)
{
    return (paramCode >= 4) && (paramCode <= 11);
}

static bool isVoltageParam(uint8_t paramCode)
{
    return (paramCode == 2) || (paramCode == 3) || (paramCode == 17);
}

// Field name for a parameter code, for error messages
static const char* paramName(uint8_t paramCode)
{
    switch (paramCode) {
        case 1: return "isBiphasic";
        case 2: return "phase1Voltage";
        case 3: return "phase2Voltage";
        case 4: return "phase1Duration";
        case 5: return "interPhaseInterval";
        case 6: return "phase2Duration";
        case 7: return "interPulseInterval";
        case 8: return "burstDuration";
        case 9: return "interBurstInterval";
        case 10: return "pulseTrainDuration";
        case 11: return "pulseTrainDelay";
        case 12: return "linkTriggerChannel1";
        case 13: return "linkTriggerChannel2";
        case 14: return "customTrainID";
        case 15: return "customTrainTarget";
        case 16: return "customTrainLoop";
        case 17: return "restingVoltage";
        case 18: return "continuous loop";
        case 128: return "triggerMode";
    }
    return "unknown parameter";
}

// Value of an output parameter in the local copy, by parameter code
static float outputParamValue(const PulsePal::OutputParams& params, uint8_t paramCode)
{
    switch (paramCode) {
        case 1: return (float)params.isBiphasic;
        case 2: return params.phase1Voltage;
        case 3: return params.phase2Voltage;
        case 4: return params.phase1Duration;
        case 5: return params.interPhaseInterval;
        case 6: return params.phase2Duration;
        case 7: return params.interPulseInterval;
        case 8: return params.burstDuration;
        case 9: return params.interBurstInterval;
        case 10: return params.pulseTrainDuration;
        case 11: return params.pulseTrainDelay;
        case 12: return (float)params.linkTriggerChannel1;
        case 13: return (float)params.linkTriggerChannel2;
        case 14: return (float)params.customTrainID;
        case 15: return (float)params.customTrainTarget;
        case 16: return (float)params.customTrainLoop;
        case 17: return params.restingVoltage;
    }
    return 0;
}

// Multi-byte values are little-endian
static void appendUint16(std::vector<uint8_t>& message, uint16_t value)
{
    message.push_back((uint8_t)(value));
    message.push_back((uint8_t)(value >> 8));
}

static void appendUint32(std::vector<uint8_t>& message, uint32_t value)
{
    message.push_back((uint8_t)(value));
    message.push_back((uint8_t)(value >> 8));
    message.push_back((uint8_t)(value >> 16));
    message.push_back((uint8_t)(value >> 24));
}

// Rounds to the nearest integer, and a value exactly halfway between two integers to the even one, as Python's
// round() does, so that this class sends the same DAC codes and cycle counts as the Python class. Halfway values
// are common: +4V is DAC code 45874.5. (MATLAB's round() takes the odd one in that case, 1 DAC code = 0.3mV higher.)
static double roundHalfEven(double value)
{
    double rounded = std::floor(value + 0.5);
    if (((rounded - value) == 0.5) && (std::fmod(rounded, 2.0) != 0)) {
        rounded -= 1;
    }
    return rounded;
}

static uint32_t readUint32(const uint8_t* bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

PulsePal::PulsePal() :
    serial(NULL), ownedSerial(NULL), connected(false), firmwareVersion(0), hardwareVersion(0),
    cycleFrequency(20000), nCustomTrains(0), maxCustomPulses(0)
{
    ownedSerial = new USBSerialPort();
    serial = ownedSerial;
    setDefaultParameters();
}

PulsePal::PulsePal(SerialPort* port) :
    serial(port), ownedSerial(NULL), connected(false), firmwareVersion(0), hardwareVersion(0),
    cycleFrequency(20000), nCustomTrains(0), maxCustomPulses(0)
{
    setDefaultParameters();
}

PulsePal::~PulsePal()
{
    end();
    delete ownedSerial;
}

void PulsePal::setDefaultParameters()
{
    for (int i = 0; i < 5; i++)
    {
        // Channel 0 is unused, but is set to the defaults too so that it is never uninitialized
        currentOutputParams[i].isBiphasic = 0;
        currentOutputParams[i].phase1Voltage = 5;
        currentOutputParams[i].phase2Voltage = -5;
        currentOutputParams[i].restingVoltage = 0;
        currentOutputParams[i].phase1Duration = 0.001f;
        currentOutputParams[i].interPhaseInterval = 0.001f;
        currentOutputParams[i].phase2Duration = 0.001f;
        currentOutputParams[i].interPulseInterval = 0.01f;
        currentOutputParams[i].burstDuration = 0;
        currentOutputParams[i].interBurstInterval = 0;
        currentOutputParams[i].pulseTrainDuration = 1;
        currentOutputParams[i].pulseTrainDelay = 0;
        currentOutputParams[i].linkTriggerChannel1 = 1;
        currentOutputParams[i].linkTriggerChannel2 = 0;
        currentOutputParams[i].customTrainID = 0;
        currentOutputParams[i].customTrainTarget = 0;
        currentOutputParams[i].customTrainLoop = 0;
        continuousLoop[i] = 0;
    }
    for (int i = 0; i < 3; i++)
    {
        currentInputParams[i].triggerMode = 0;
    }
}

bool PulsePal::initialize(std::string portString)
{
    end(); // In case this object is already connected
    firmwareVersion = 0;
    hardwareVersion = 0;

    if (!serial->open(portString)) {
        reportError(serial->lastError() + ". Check the port name, and that no other program is using the port.");
        return false;
    }
    serial->discardInput(); // Anything left from an earlier session would be read as the handshake reply

    // Op 72: handshake. The reply is 'K', then the firmware version (uint32)
    const uint8_t handshake[2] = {OpMenuByte, OP_HANDSHAKE};
    uint8_t reply[5] = {0};
    if (!serial->write(handshake, 2) || (serial->read(reply, 5, ReplyTimeoutMs) < 5) || (reply[0] != HandshakeReply)) {
        if (reply[0] == WavePalHandshakeReply) {
            std::ostringstream wavePalError;
            wavePalError << "The device on port " << portString << " runs Wave Pal firmware (v" << readUint32(reply + 1)
                         << "), not Pulse Pal firmware. To use it as a Pulse Pal, load Pulse Pal firmware onto it (see "
                         << "/Firmware/Readme.txt). To use it as a Wave Pal, use its Python or MATLAB class.";
            reportError(wavePalError.str());
        } else {
            reportError("The device on port " + portString + " did not return the Pulse Pal handshake. It may not be a Pulse Pal.");
        }
        serial->close();
        return false;
    }
    uint32_t version = readUint32(reply + 1);
    std::ostringstream versionError;
    if (version < 20) {
        versionError << "Pulse Pal 1 was found on port " << portString << ". Use the C++ class in /c++/legacy/.";
    } else if (version < OldestFirmwareSupported) {
        versionError << "Pulse Pal firmware v" << version << " was found on port " << portString << ". Please update "
                     << "the firmware: https://sites.google.com/site/pulsepalwiki/updating-firmware";
    } else if (version > CurrentFirmwareVersion) {
        versionError << "Pulse Pal firmware v" << version << " was found on port " << portString << ". This C++ class "
                     << "supports firmware up to v" << CurrentFirmwareVersion << ". Please update it, or downgrade "
                     << "the firmware to v" << CurrentFirmwareVersion << ".";
    }
    if (!versionError.str().empty()) {
        reportError(versionError.str());
        serial->close();
        return false;
    }
    if (version < CurrentFirmwareVersion) {
        std::cout << "PulsePal: Firmware v" << version << " detected. This firmware is supported. Update to v"
                  << CurrentFirmwareVersion << " is available." << std::endl;
    }

    // Op 94: hardware version, timer period (us, uint32), number of custom trains, maximum pulses per train (uint32).
    // Firmware v21 does not have op 94, and runs only on Pulse Pal 2.
    if (version > 21) {
        const uint8_t request[2] = {OpMenuByte, OP_SEND_HARDWARE_INFO};
        uint8_t info[10] = {0};
        if (!serial->write(request, 2) || (serial->read(info, 10, ReplyTimeoutMs) < 10)) {
            reportError("Pulse Pal on port " + portString + " did not return its hardware information.");
            serial->close();
            return false;
        }
        uint32_t cyclePeriod = readUint32(info + 1);
        if (cyclePeriod == 0) {
            reportError("Pulse Pal on port " + portString + " reported a timer period of 0.");
            serial->close();
            return false;
        }
        hardwareVersion = info[0];
        cycleFrequency = 1000000.0 / cyclePeriod;
        nCustomTrains = info[5];
        maxCustomPulses = readUint32(info + 6);
    } else {
        hardwareVersion = 2;
        cycleFrequency = 20000;
        nCustomTrains = 2;
        maxCustomPulses = 5000;
    }
    firmwareVersion = version;
    connected = true;

    // Program the defaults, so that the device and the local copy of its parameters agree
    setDefaultParameters();
    if (hardwareVersion > 2) {
        // A device left in param sync mode by an earlier session would store the syncAllParams() below instead of
        // applying it, and keep its old parameters until a TTL arrived. Op 91 is not deferred that way, so it takes
        // both trigger channels out of param sync mode first. See "Param sync mode" in /Firmware/PROTOCOL.md.
        const uint8_t normalMode[5] = {OpMenuByte, OP_PROGRAM_PARAM_ALL_CHANNELS, PARAM_TRIGGER_MODE, 0, 0};
        if (!sendCommand(normalMode, 5, "initialize()") || !readConfirm("initialize()")) {
            end();
            return false;
        }
    }
    if (!syncAllParams()) {
        end();
        return false;
    }
    return true;
}

void PulsePal::end()
{
    if (connected) {
        disconnectClient();
    }
    serial->close();
    connected = false;
}

uint32_t PulsePal::getFirmwareVersion()
{
    return firmwareVersion;
}

uint32_t PulsePal::getHardwareVersion()
{
    return hardwareVersion;
}

void PulsePal::disconnectClient()
{
    if (!connected) {
        return;
    }
    const uint8_t message[2] = {OpMenuByte, OP_DISCONNECT};
    sendCommand(message, 2, "disconnectClient()");
}

bool PulsePal::setBiphasic(uint8_t channel, bool isBiphasic)
{
    uint8_t command = isBiphasic ? 1 : 0;
    if (!setOutputParam(channel, PARAM_IS_BIPHASIC, command, "setBiphasic()")) {
        return false;
    }
    currentOutputParams[channel].isBiphasic = command;
    return true;
}

bool PulsePal::setPhase1Voltage(uint8_t channel, float voltage)
{
    if (!setOutputParam(channel, PARAM_PHASE1_VOLTAGE, voltage, "setPhase1Voltage()")) {
        return false;
    }
    currentOutputParams[channel].phase1Voltage = voltage;
    return true;
}

bool PulsePal::setPhase2Voltage(uint8_t channel, float voltage)
{
    if (!setOutputParam(channel, PARAM_PHASE2_VOLTAGE, voltage, "setPhase2Voltage()")) {
        return false;
    }
    currentOutputParams[channel].phase2Voltage = voltage;
    return true;
}

bool PulsePal::setRestingVoltage(uint8_t channel, float voltage)
{
    if (!setOutputParam(channel, PARAM_RESTING_VOLTAGE, voltage, "setRestingVoltage()")) {
        return false;
    }
    currentOutputParams[channel].restingVoltage = voltage;
    return true;
}

bool PulsePal::setPhase1Duration(uint8_t channel, float timeInSeconds)
{
    if (!setOutputParam(channel, PARAM_PHASE1_DURATION, timeInSeconds, "setPhase1Duration()")) {
        return false;
    }
    currentOutputParams[channel].phase1Duration = timeInSeconds;
    return true;
}

bool PulsePal::setInterPhaseInterval(uint8_t channel, float timeInSeconds)
{
    if (!setOutputParam(channel, PARAM_INTER_PHASE_INTERVAL, timeInSeconds, "setInterPhaseInterval()")) {
        return false;
    }
    currentOutputParams[channel].interPhaseInterval = timeInSeconds;
    return true;
}

bool PulsePal::setPhase2Duration(uint8_t channel, float timeInSeconds)
{
    if (!setOutputParam(channel, PARAM_PHASE2_DURATION, timeInSeconds, "setPhase2Duration()")) {
        return false;
    }
    currentOutputParams[channel].phase2Duration = timeInSeconds;
    return true;
}

bool PulsePal::setInterPulseInterval(uint8_t channel, float timeInSeconds)
{
    if (!setOutputParam(channel, PARAM_INTER_PULSE_INTERVAL, timeInSeconds, "setInterPulseInterval()")) {
        return false;
    }
    currentOutputParams[channel].interPulseInterval = timeInSeconds;
    return true;
}

bool PulsePal::setBurstDuration(uint8_t channel, float timeInSeconds)
{
    if (!setOutputParam(channel, PARAM_BURST_DURATION, timeInSeconds, "setBurstDuration()")) {
        return false;
    }
    currentOutputParams[channel].burstDuration = timeInSeconds;
    return true;
}

bool PulsePal::setBurstInterval(uint8_t channel, float timeInSeconds)
{
    if (!setOutputParam(channel, PARAM_BURST_INTERVAL, timeInSeconds, "setBurstInterval()")) {
        return false;
    }
    currentOutputParams[channel].interBurstInterval = timeInSeconds;
    return true;
}

bool PulsePal::setPulseTrainDuration(uint8_t channel, float timeInSeconds)
{
    if (!setOutputParam(channel, PARAM_PULSE_TRAIN_DURATION, timeInSeconds, "setPulseTrainDuration()")) {
        return false;
    }
    currentOutputParams[channel].pulseTrainDuration = timeInSeconds;
    return true;
}

bool PulsePal::setPulseTrainDelay(uint8_t channel, float timeInSeconds)
{
    if (!setOutputParam(channel, PARAM_PULSE_TRAIN_DELAY, timeInSeconds, "setPulseTrainDelay()")) {
        return false;
    }
    currentOutputParams[channel].pulseTrainDelay = timeInSeconds;
    return true;
}

bool PulsePal::setTrigger1Link(uint8_t channel, uint8_t link_state)
{
    if (!setOutputParam(channel, PARAM_LINK_TRIGGER1, link_state, "setTrigger1Link()")) {
        return false;
    }
    currentOutputParams[channel].linkTriggerChannel1 = link_state;
    return true;
}

bool PulsePal::setTrigger2Link(uint8_t channel, uint8_t link_state)
{
    if (!setOutputParam(channel, PARAM_LINK_TRIGGER2, link_state, "setTrigger2Link()")) {
        return false;
    }
    currentOutputParams[channel].linkTriggerChannel2 = link_state;
    return true;
}

bool PulsePal::setCustomTrainID(uint8_t channel, uint8_t ID)
{
    if (!setOutputParam(channel, PARAM_CUSTOM_TRAIN_ID, ID, "setCustomTrainID()")) {
        return false;
    }
    currentOutputParams[channel].customTrainID = ID;
    return true;
}

bool PulsePal::setCustomTrainTarget(uint8_t channel, uint8_t target)
{
    if (!setOutputParam(channel, PARAM_CUSTOM_TRAIN_TARGET, target, "setCustomTrainTarget()")) {
        return false;
    }
    currentOutputParams[channel].customTrainTarget = target;
    return true;
}

bool PulsePal::setCustomTrainLoop(uint8_t channel, uint8_t loop_state)
{
    if (!setOutputParam(channel, PARAM_CUSTOM_TRAIN_LOOP, loop_state, "setCustomTrainLoop()")) {
        return false;
    }
    currentOutputParams[channel].customTrainLoop = loop_state;
    return true;
}

bool PulsePal::setTriggerMode(uint8_t channel, uint8_t mode)
{
    const char* context = "setTriggerMode()";
    if (!checkConnected(context)) {
        return false;
    }
    if ((channel < 1) || (channel > 2)) {
        std::ostringstream error;
        error << context << ": trigger channel " << (int)channel << " does not exist. Use 1 or 2.";
        reportError(error.str());
        return false;
    }
    if (!checkParam(PARAM_TRIGGER_MODE, mode, channel, context) || !program(channel, PARAM_TRIGGER_MODE, mode, context)) {
        return false;
    }
    currentInputParams[channel].triggerMode = mode;
    return true;
}

bool PulsePal::triggerChannel(uint8_t channel)
{
    const char* context = "triggerChannel()";
    if (!checkConnected(context) || !checkOutputChannel(channel, context)) {
        return false;
    }
    // Op 77. One bit per output channel (bit 0 = channel 1)
    const uint8_t message[3] = {OpMenuByte, OP_SOFT_TRIGGER, (uint8_t)(1 << (channel - 1))};
    return sendCommand(message, 3, context);
}

bool PulsePal::triggerChannels(uint8_t channel1, uint8_t channel2, uint8_t channel3, uint8_t channel4)
{
    const char* context = "triggerChannels()";
    if (!checkConnected(context)) {
        return false;
    }
    uint8_t code = 0;
    code |= (channel1 != 0) ? 1 : 0;
    code |= (channel2 != 0) ? 2 : 0;
    code |= (channel3 != 0) ? 4 : 0;
    code |= (channel4 != 0) ? 8 : 0;
    const uint8_t message[3] = {OpMenuByte, OP_SOFT_TRIGGER, code};
    return sendCommand(message, 3, context);
}

bool PulsePal::updateDisplay(const std::string& line1, const std::string& line2)
{
    const char* context = "updateDisplay()";
    if (!checkConnected(context)) {
        return false;
    }
    // Op 78: length byte, then the characters. A character of 254 moves to the second line.
    size_t messageLength = line1.size() + 1 + line2.size();
    if (messageLength > 255) {
        reportError(std::string(context) + ": the two lines are too long. They must total 254 characters or fewer.");
        return false;
    }
    std::vector<uint8_t> message;
    message.push_back(OpMenuByte);
    message.push_back(OP_DISPLAY_MESSAGE);
    message.push_back((uint8_t)messageLength);
    message.insert(message.end(), line1.begin(), line1.end());
    message.push_back(254);
    message.insert(message.end(), line2.begin(), line2.end());
    return sendCommand(message.data(), message.size(), context);
}

bool PulsePal::setClientIDString(const std::string& idString)
{
    const char* context = "setClientIDString()";
    if (!checkConnected(context)) {
        return false;
    }
    if (idString.size() != 6) {
        reportError(std::string(context) + ": the client ID must be 6 characters. Client ID NOT set.");
        return false;
    }
    std::vector<uint8_t> message;
    message.push_back(OpMenuByte);
    message.push_back(OP_SET_CLIENT_NAME);
    message.insert(message.end(), idString.begin(), idString.end());
    return sendCommand(message.data(), message.size(), context);
}

bool PulsePal::setFixedVoltage(uint8_t channel, float voltage)
{
    const char* context = "setFixedVoltage()";
    if (!checkConnected(context) || !checkOutputChannel(channel, context)) {
        return false;
    }
    if (!((voltage >= -MaxVoltage) && (voltage <= MaxVoltage))) { // Written this way to also reject NaN
        std::ostringstream error;
        error << context << ": the voltage was " << voltage << " V. It must be -10 to 10 V.";
        reportError(error.str());
        return false;
    }
    uint16_t voltageBits = voltageToInt16(voltage);
    const uint8_t message[5] = {OpMenuByte, OP_SET_FIXED_VOLTAGE, channel, (uint8_t)(voltageBits), (uint8_t)(voltageBits >> 8)};
    return sendCommand(message, 5, context) && readConfirm(context);
}

bool PulsePal::abortPulseTrains()
{
    const char* context = "abortPulseTrains()";
    if (!checkConnected(context)) {
        return false;
    }
    const uint8_t message[2] = {OpMenuByte, OP_ABORT_ALL};
    return sendCommand(message, 2, context);
}

bool PulsePal::setContinuousLoop(uint8_t channel, uint8_t state)
{
    const char* context = "setContinuousLoop()";
    if (!checkConnected(context) || !checkOutputChannel(channel, context)
        || !checkParam(PARAM_CONTINUOUS_LOOP, state, channel, context)) {
        return false;
    }
    const uint8_t message[4] = {OpMenuByte, OP_SET_CONTINUOUS_LOOP, channel, state};
    if (!sendCommand(message, 4, context) || !readConfirm(context)) {
        return false;
    }
    continuousLoop[channel] = state;
    return true;
}

bool PulsePal::sendCustomPulseTrain(uint8_t ID, uint16_t nPulses, const float customPulseTimes[], const float customVoltages[])
{
    const char* context = "sendCustomPulseTrain()";
    if (!checkConnected(context)) {
        return false;
    }
    std::ostringstream error;
    error << context << ": ";
    if ((ID < 1) || (ID > nCustomTrains)) {
        error << "custom train " << (int)ID << " does not exist. This Pulse Pal has custom trains 1 to " << (int)nCustomTrains << ".";
        reportError(error.str());
        return false;
    }
    if ((nPulses == 0) || (customPulseTimes == NULL) || (customVoltages == NULL)) {
        error << "the train must have at least one pulse.";
        reportError(error.str());
        return false;
    }
    if (nPulses > maxCustomPulses) {
        error << nPulses << " pulses were given. This Pulse Pal can store up to " << maxCustomPulses << " pulses per custom train.";
        reportError(error.str());
        return false;
    }

    // Check and convert every pulse before sending anything
    std::vector<uint32_t> pulseCycles(nPulses);
    for (int i = 0; i < nPulses; i++) {
        float pulseTime = customPulseTimes[i];
        if (!((pulseTime >= 0) && (pulseTime <= MaxTime))) { // Written this way to also reject NaN
            error << "pulse " << (i + 1) << " is at " << pulseTime << " s. Pulse times must be 0 to 3600 s.";
            reportError(error.str());
            return false;
        }
        pulseCycles[i] = timeToCycles(pulseTime);
        // The device plays each pulse until the next one's time, so a time that is not later than the one before it
        // would freeze the output for the rest of the train. The check is on the times the device will receive,
        // after rounding to its timer cycles.
        if ((i > 0) && (pulseCycles[i] <= pulseCycles[i - 1])) {
            error << "pulse times must increase, by at least one " << (1000000.0 / cycleFrequency) << " us timer cycle. "
                  << "Pulse " << (i + 1) << " is at " << pulseTime << " s, and pulse " << i << " is at "
                  << customPulseTimes[i - 1] << " s.";
            reportError(error.str());
            return false;
        }
        float voltage = customVoltages[i];
        if (!((voltage >= -MaxVoltage) && (voltage <= MaxVoltage))) {
            error << "pulse " << (i + 1) << " is " << voltage << " V. Voltages must be -10 to 10 V.";
            reportError(error.str());
            return false;
        }
    }

    // Firmware v22: op 95, train index (0 = train 1). Firmware v21: op 75 for train 1, op 76 for train 2. The legacy
    // op codes are named, not calculated from ID: op 74 + 3 would be a different command.
    std::vector<uint8_t> message;
    message.reserve(7 + (6 * (size_t)nPulses));
    message.push_back(OpMenuByte);
    if (firmwareVersion > 21) {
        message.push_back(OP_LOAD_CUSTOM_TRAIN);
        message.push_back(ID - 1);
    } else {
        message.push_back((ID == 1) ? OP_LOAD_CUSTOM_TRAIN1_LEGACY : OP_LOAD_CUSTOM_TRAIN2_LEGACY);
    }
    appendUint32(message, nPulses);
    for (int i = 0; i < nPulses; i++) {
        appendUint32(message, pulseCycles[i]);
    }
    for (int i = 0; i < nPulses; i++) {
        appendUint16(message, voltageToInt16(customVoltages[i]));
    }
    return sendCommand(message.data(), message.size(), context) && readConfirm(context);
}

bool PulsePal::syncAllParams()
{
    const char* context = "syncAllParams()";
    if (!checkConnected(context)) {
        return false;
    }

    // Check every parameter first, so that nothing is sent if any is out of range
    for (int channel = 1; channel < 5; channel++) {
        for (uint8_t paramCode = PARAM_IS_BIPHASIC; paramCode <= PARAM_RESTING_VOLTAGE; paramCode++) {
            if (!checkParam(paramCode, outputParamValue(currentOutputParams[channel], paramCode), channel, context)) {
                return false;
            }
        }
        if ((currentOutputParams[channel].customTrainTarget == 1) && (timeToCycles(currentOutputParams[channel].burstDuration) == 0)) {
            std::ostringstream error;
            error << context << ": output channel " << channel << " has customTrainTarget = 1, so its custom train "
                  << "times are burst onsets. Its burstDuration must be above 0.";
            reportError(error.str());
            return false;
        }
    }
    for (int channel = 1; channel < 3; channel++) {
        if (!checkParam(PARAM_TRIGGER_MODE, (float)currentInputParams[channel].triggerMode, channel, context)) {
            return false;
        }
    }

    std::vector<uint8_t> message;
    message.push_back(OpMenuByte);
    if (firmwareVersion > 21) {
        // Op 92: grouped by parameter. Each group holds one parameter for output channels 1-4
        message.push_back(OP_PROGRAM_ALL_PARAMS);
        for (int i = 0; i < 8; i++) {
            for (int channel = 1; channel < 5; channel++) {
                appendUint32(message, timeToCycles(outputParamValue(currentOutputParams[channel], TimeParamCodes[i])));
            }
        }
        for (int channel = 1; channel < 5; channel++) {
            appendUint16(message, voltageToInt16(currentOutputParams[channel].phase1Voltage));
        }
        for (int channel = 1; channel < 5; channel++) {
            appendUint16(message, voltageToInt16(currentOutputParams[channel].phase2Voltage));
        }
        for (int channel = 1; channel < 5; channel++) {
            appendUint16(message, voltageToInt16(currentOutputParams[channel].restingVoltage));
        }
        for (int channel = 1; channel < 5; channel++) {
            message.push_back((uint8_t)currentOutputParams[channel].isBiphasic);
        }
        for (int channel = 1; channel < 5; channel++) {
            message.push_back((uint8_t)currentOutputParams[channel].customTrainID);
        }
        for (int channel = 1; channel < 5; channel++) {
            message.push_back((uint8_t)currentOutputParams[channel].customTrainTarget);
        }
        for (int channel = 1; channel < 5; channel++) {
            message.push_back((uint8_t)currentOutputParams[channel].customTrainLoop);
        }
        for (int channel = 1; channel < 5; channel++) {
            // Op 92 sets continuous loop mode, and stops a channel whose mode it turns off, so it sends the mode
            // last set by setContinuousLoop() rather than 0
            message.push_back(continuousLoop[channel]);
        }
    } else {
        // Op 73 (firmware v21): grouped by channel. It does not set continuous loop mode
        message.push_back(OP_PROGRAM_ALL_PARAMS_LEGACY);
        for (int channel = 1; channel < 5; channel++) {
            for (int i = 0; i < 8; i++) {
                appendUint32(message, timeToCycles(outputParamValue(currentOutputParams[channel], TimeParamCodes[i])));
            }
        }
        for (int channel = 1; channel < 5; channel++) {
            appendUint16(message, voltageToInt16(currentOutputParams[channel].phase1Voltage));
            appendUint16(message, voltageToInt16(currentOutputParams[channel].phase2Voltage));
            appendUint16(message, voltageToInt16(currentOutputParams[channel].restingVoltage));
        }
        for (int channel = 1; channel < 5; channel++) {
            message.push_back((uint8_t)currentOutputParams[channel].isBiphasic);
            message.push_back((uint8_t)currentOutputParams[channel].customTrainID);
            message.push_back((uint8_t)currentOutputParams[channel].customTrainTarget);
            message.push_back((uint8_t)currentOutputParams[channel].customTrainLoop);
        }
    }
    // Both ops end with the trigger links (trigger channel 1 to output channels 1-4, then trigger channel 2), then
    // the two trigger modes
    for (int channel = 1; channel < 5; channel++) {
        message.push_back((uint8_t)currentOutputParams[channel].linkTriggerChannel1);
    }
    for (int channel = 1; channel < 5; channel++) {
        message.push_back((uint8_t)currentOutputParams[channel].linkTriggerChannel2);
    }
    message.push_back((uint8_t)currentInputParams[1].triggerMode);
    message.push_back((uint8_t)currentInputParams[2].triggerMode);

    return sendCommand(message.data(), message.size(), context) && readConfirm(context);
}

// Checks, encodes and programs one output channel parameter with op 74
bool PulsePal::setOutputParam(uint8_t channel, uint8_t paramCode, float value, const char* context)
{
    if (!checkConnected(context) || !checkOutputChannel(channel, context) || !checkParam(paramCode, value, channel, context)) {
        return false;
    }
    return program(channel, paramCode, encodeParam(paramCode, value), context);
}

// Op 74: parameter code, channel, then the value in 1, 2 or 4 bytes. paramValue is already in device units.
bool PulsePal::program(uint8_t channel, uint8_t paramCode, uint32_t paramValue, const char* context)
{
    uint8_t nValueBytes = paramValueBytes(paramCode);
    uint8_t message[8] = {OpMenuByte, OP_PROGRAM_ONE_PARAM, paramCode, channel, 0, 0, 0, 0};
    for (int i = 0; i < nValueBytes; i++) {
        message[4 + i] = (uint8_t)(paramValue >> (8 * i)); // Little-endian
    }
    return sendCommand(message, 4 + nValueBytes, context) && readConfirm(context);
}

// Checks that a parameter value is in range for the connected device. channel is only used in the error message.
bool PulsePal::checkParam(uint8_t paramCode, float value, int channel, const char* context)
{
    std::ostringstream error;
    error << context << ": " << paramName(paramCode) << " on " << ((paramCode == PARAM_TRIGGER_MODE) ? "trigger" : "output")
          << " channel " << channel << " was " << value;
    if (isVoltageParam(paramCode)) {
        if ((value >= -MaxVoltage) && (value <= MaxVoltage)) { // Written this way to also reject NaN
            return true;
        }
        error << " V. It must be -10 to 10 V.";
    } else if (isTimeParam(paramCode)) {
        bool isMinPulseTime = (paramCode == PARAM_PHASE1_DURATION) || (paramCode == PARAM_PHASE2_DURATION)
                              || (paramCode == PARAM_INTER_PULSE_INTERVAL) || (paramCode == PARAM_PULSE_TRAIN_DURATION);
        double minTime = isMinPulseTime ? MinPulseTime : 0;
        if (std::isfinite(value)) {
            // Compared in timer cycles, as the device will receive it, so that float rounding cannot reject 0.0001
            double cycles = roundHalfEven((double)value * cycleFrequency);
            if ((cycles >= roundHalfEven(minTime * cycleFrequency)) && (cycles <= (MaxTime * cycleFrequency))) {
                return true;
            }
        }
        error << " s. It must be " << minTime << " to " << MaxTime << " s.";
    } else {
        int maxValue = 1;
        if (paramCode == PARAM_CUSTOM_TRAIN_ID) {
            maxValue = nCustomTrains;
        } else if (paramCode == PARAM_TRIGGER_MODE) {
            maxValue = (hardwareVersion > 2) ? TriggerModeParamSync : 2; // Param sync mode is Pulse Pal 3 only
        }
        if ((value >= 0) && (value <= (float)maxValue) && (value == std::floor(value))) {
            return true;
        }
        error << ". It must be an integer from 0 to " << maxValue << " on this Pulse Pal.";
    }
    reportError(error.str());
    return false;
}

// Converts a checked parameter value to the units the device reads: DAC codes, timer cycles, or a byte
uint32_t PulsePal::encodeParam(uint8_t paramCode, float value)
{
    if (isVoltageParam(paramCode)) {
        return voltageToInt16(value);
    }
    if (isTimeParam(paramCode)) {
        return timeToCycles(value);
    }
    return (uint32_t)value;
}

// Seconds to hardware timer cycles, rounded to the nearest cycle. The value must already be checked.
uint32_t PulsePal::timeToCycles(float timeInSeconds)
{
    return (uint32_t)roundHalfEven((double)timeInSeconds * cycleFrequency);
}

uint16_t PulsePal::voltageToInt16(float voltage)
{
    // input: -10 to 10 V
    // output: 0-65535, rounded to the nearest code as in the Python class
    double code = roundHalfEven((((double)voltage + MaxVoltage) / (2 * MaxVoltage)) * DACMax);
    if (code < 0) {
        code = 0;
    }
    if (code > DACMax) {
        code = DACMax;
    }
    return (uint16_t)code;
}

bool PulsePal::checkConnected(const char* context)
{
    if (!connected) {
        reportError(std::string(context) + ": Pulse Pal is not connected. Call initialize() first.");
        return false;
    }
    return true;
}

bool PulsePal::checkOutputChannel(uint8_t channel, const char* context)
{
    if ((channel < 1) || (channel > 4)) {
        std::ostringstream error;
        error << context << ": output channel " << (int)channel << " does not exist. Use 1 to 4.";
        reportError(error.str());
        return false;
    }
    return true;
}

// Sends a whole command in one write, so that a pause in this program cannot split it. The device gives up on a
// command if its bytes stop arriving for 100ms.
bool PulsePal::sendCommand(const uint8_t* message, size_t nBytes, const char* context)
{
    if (!serial->write(message, nBytes)) {
        reportError(std::string(context) + ": " + serial->lastError());
        return false;
    }
    return true;
}

bool PulsePal::readConfirm(const char* context)
{
    uint8_t confirmByte = 0;
    if (serial->read(&confirmByte, 1, ReplyTimeoutMs) < 1) {
        // After a comm failure (an incomplete command), the device reads no commands until its joystick is clicked
        reportError(std::string(context) + ": Pulse Pal did not confirm the command. If its screen shows "
                    "COMM. FAILURE!, click its joystick and call initialize() to reconnect.");
        return false;
    }
    if (confirmByte != 1) {
        reportError(std::string(context) + ": Pulse Pal rejected the command. A channel number or value was out of "
                    "range for this device.");
        return false;
    }
    return true;
}

void PulsePal::reportError(const std::string& message)
{
    std::cerr << "PulsePal: " << message << std::endl;
}

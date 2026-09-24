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
// Updated for Pulse Pal 3 and firmware v22 by Sanworks LLC. The previous version, which also supports Pulse Pal 1,
// is in /c++/legacy/.

// C++ interface for Pulse Pal 2 and Pulse Pal 3, with firmware v21 or v22.
//
// Usage:
//
//   PulsePal pulsePal;
//   if (!pulsePal.initialize("COM3")) { return 1; }  // "/dev/cu.usbmodem..." on macOS, "/dev/ttyACM0" on Linux
//   pulsePal.setPhase1Voltage(1, 5);                  // 5V pulses on output channel 1
//   pulsePal.setPulseTrainDuration(1, 2);             // for 2 seconds
//   pulsePal.triggerChannel(1);
//   pulsePal.end();
//
// Units: voltages are in volts, -10 to +10. Times are in seconds, and are rounded to the nearest cycle of the
// device's hardware timer (50us). Output channels are numbered 1-4 and trigger channels 1-2.
//
// Errors: methods that send a command return true if it was sent and, for commands that the device confirms, the
// device confirmed it. They return false, and print the reason to std::cerr, if the device is not connected, an
// argument is out of range (in which case nothing is sent), or the device rejected the command or did not reply.
//
// Serial protocol: /Firmware/PROTOCOL.md. The Python and MATLAB classes in this repository use the same protocol.

#ifndef PULSEPAL_PULSEPAL_H
#define PULSEPAL_PULSEPAL_H

#include <cstdint>
#include <string>

#include "SerialPort.h"

class PulsePal
{
public:

    // Initialization and termination
    PulsePal();
    explicit PulsePal(SerialPort* port); // Talk to the device through port, which the caller owns. For testing
    ~PulsePal();
    PulsePal(const PulsePal&) = delete;
    PulsePal& operator=(const PulsePal&) = delete;

    // Opens the serial port, checks that the device is a Pulse Pal with supported firmware, reads its hardware
    // version, and programs it with the default parameters (see setDefaultParameters()). On Pulse Pal 3, it first
    // takes both trigger channels out of param sync mode, so that the defaults are applied instead of stored.
    bool initialize(std::string portString);
    void end(); // Disconnects (see disconnectClient()) and closes the serial port. Also called by the destructor
    uint32_t getFirmwareVersion(); // e.g. 22. Read by initialize(), and 0 before it has connected
    uint32_t getHardwareVersion(); // 2 for Pulse Pal 2, 3 for Pulse Pal 3. Read by initialize(), and 0 before it has connected
    void disconnectClient(); // Stops all channels and restores the device's own screen. The port stays open

    // Resets the local copy of the parameters (currentOutputParams and currentInputParams) to the defaults: a 1ms,
    // +5V monophasic pulse every 10ms for 1 second on every output channel, linked to trigger channel 1, which is in
    // normal mode. Does not program the device; call syncAllParams() to do that.
    void setDefaultParameters();

    // Program single parameter. Each programs one parameter on one output channel (1-4) at once, and updates the
    // local copy in currentOutputParams if the device confirms it.
    bool setBiphasic(uint8_t channel, bool isBiphasic);
    bool setPhase1Voltage(uint8_t channel, float voltage);
    bool setPhase2Voltage(uint8_t channel, float voltage);
    bool setRestingVoltage(uint8_t channel, float voltage);
    bool setPhase1Duration(uint8_t channel, float timeInSeconds);      // 0.0001 to 3600
    bool setInterPhaseInterval(uint8_t channel, float timeInSeconds);  // 0 to 3600
    bool setPhase2Duration(uint8_t channel, float timeInSeconds);      // 0.0001 to 3600
    bool setInterPulseInterval(uint8_t channel, float timeInSeconds);  // 0.0001 to 3600
    bool setBurstDuration(uint8_t channel, float timeInSeconds);       // 0 to 3600. 0 = no bursts
    bool setBurstInterval(uint8_t channel, float timeInSeconds);       // 0 to 3600
    bool setPulseTrainDuration(uint8_t channel, float timeInSeconds);  // 0.0001 to 3600
    bool setPulseTrainDelay(uint8_t channel, float timeInSeconds);     // 0 to 3600
    bool setTrigger1Link(uint8_t channel, uint8_t link_state); // 1: trigger channel 1 starts this output channel
    bool setTrigger2Link(uint8_t channel, uint8_t link_state);
    bool setCustomTrainID(uint8_t channel, uint8_t ID); // ID = 0: no custom train. ID = 1-2 (Pulse Pal 2) or 1-4 (Pulse Pal 3): that custom train
    bool setCustomTrainTarget(uint8_t channel, uint8_t target); // target = 0: Custom times define pulses Target = 1: They define bursts
    bool setCustomTrainLoop(uint8_t channel, uint8_t loop_state); // loop_state = 0: No loop 1: loop

    // Program all parameters from object fields. Sends currentOutputParams and currentInputParams to the device in a
    // single command. Every field is checked first, and nothing is sent if any is out of range.
    //
    // On Pulse Pal 3, while either trigger channel is in param sync mode (trigger mode 3), the device stores the
    // parameters instead of applying them, and applies them at the next rising edge on that trigger channel. This is
    // how the next trial's parameters can be sent during the current trial. Only syncAllParams() is held back this
    // way: the single-parameter methods above and setTriggerMode() take effect at once, so use setTriggerMode() to
    // leave param sync mode. See "Param sync mode" in /Firmware/PROTOCOL.md.
    bool syncAllParams();

    // Upload a custom pulse train. ID is 1-2 on Pulse Pal 2, or 1-4 on Pulse Pal 3. customPulseTimes are pulse onset
    // times in seconds, relative to the start of the train, and must increase by at least one timer cycle (50us)
    // from each pulse to the next. customVoltages are in volts. nPulses is at most 5000 on Pulse Pal 2, or 10000 on
    // Pulse Pal 3. Set an output channel's custom train ID to play the train there.
    bool sendCustomPulseTrain(uint8_t ID, uint16_t nPulses, const float customPulseTimes[], const float customVoltages[]);

    // Operations and settings
    bool triggerChannel(uint8_t channel); // Starts the pulse train on one output channel
    bool triggerChannels(uint8_t channel1, uint8_t channel2, uint8_t channel3, uint8_t channel4); // 1 = start that channel. Channels start together
    bool updateDisplay(const std::string& line1, const std::string& line2); // The screen shows 16 characters per line
    bool setFixedVoltage(uint8_t channel, float voltage); // Held until the channel is triggered or set again
    bool abortPulseTrains(); // Stops all output channels, which return to their resting voltages
    bool setContinuousLoop(uint8_t channel, uint8_t state); // state = 1: the pulse train plays until aborted. 0: it plays for its duration
    bool setTriggerMode(uint8_t channel, uint8_t mode); // Trigger channel 1-2. mode = 0: normal, 1: toggle, 2: pulse gated, 3: param sync (Pulse Pal 3 only)
    bool setClientIDString(const std::string& idString); // Exactly 6 characters, shown on the device's screen as "xxxxxx Connected"

    // Fields: the local copy of the device's parameters. Units and ranges are as for the methods above.
    // Editing a field does not program the device; call syncAllParams() to send them all.
    struct OutputParams {
        int isBiphasic;
        float phase1Voltage;
        float phase2Voltage;
        float phase1Duration;
        float interPhaseInterval;
        float phase2Duration;
        float interPulseInterval;
        float burstDuration;
        float interBurstInterval;
        float pulseTrainDuration;
        float pulseTrainDelay;
        int linkTriggerChannel1;
        int linkTriggerChannel2;
        int customTrainID;
        int customTrainTarget;
        int customTrainLoop;
        float restingVoltage;
    } currentOutputParams[5]; // Use 1-indexing for the channels (output channels 1-4 = currentOutputParams[1]-currentOutputParams[4])
    struct InputParams {
        int triggerMode;
    } currentInputParams[3]; // Use 1-indexing for the trigger channels

private:
    // Op codes and parameter codes are fixed by the serial protocol (/Firmware/PROTOCOL.md) and match the OpCode and
    // ParamID enums in /Firmware/PulsePal3/PulsePal3.ino. They are listed explicitly, never derived from a position.
    enum OpCode {
        OP_HANDSHAKE = 72,
        OP_PROGRAM_ALL_PARAMS_LEGACY = 73,  // Firmware v21. Replaced by op 92
        OP_PROGRAM_ONE_PARAM = 74,
        OP_LOAD_CUSTOM_TRAIN1_LEGACY = 75,  // Firmware v21. Replaced by op 95
        OP_LOAD_CUSTOM_TRAIN2_LEGACY = 76,  // Firmware v21. Replaced by op 95
        OP_SOFT_TRIGGER = 77,
        OP_DISPLAY_MESSAGE = 78,
        OP_SET_FIXED_VOLTAGE = 79,
        OP_ABORT_ALL = 80,
        OP_DISCONNECT = 81,
        OP_SET_CONTINUOUS_LOOP = 82,
        OP_SET_CLIENT_NAME = 89,
        OP_PROGRAM_PARAM_ALL_CHANNELS = 91, // Firmware v22
        OP_PROGRAM_ALL_PARAMS = 92,         // Firmware v22
        OP_SEND_HARDWARE_INFO = 94,         // Firmware v22
        OP_LOAD_CUSTOM_TRAIN = 95           // Firmware v22
    };
    enum ParamCode {
        PARAM_IS_BIPHASIC = 1,
        PARAM_PHASE1_VOLTAGE = 2,
        PARAM_PHASE2_VOLTAGE = 3,
        PARAM_PHASE1_DURATION = 4,
        PARAM_INTER_PHASE_INTERVAL = 5,
        PARAM_PHASE2_DURATION = 6,
        PARAM_INTER_PULSE_INTERVAL = 7,
        PARAM_BURST_DURATION = 8,
        PARAM_BURST_INTERVAL = 9,
        PARAM_PULSE_TRAIN_DURATION = 10,
        PARAM_PULSE_TRAIN_DELAY = 11,
        PARAM_LINK_TRIGGER1 = 12,
        PARAM_LINK_TRIGGER2 = 13,
        PARAM_CUSTOM_TRAIN_ID = 14,
        PARAM_CUSTOM_TRAIN_TARGET = 15,
        PARAM_CUSTOM_TRAIN_LOOP = 16,
        PARAM_RESTING_VOLTAGE = 17,
        PARAM_CONTINUOUS_LOOP = 18,
        PARAM_TRIGGER_MODE = 128           // Applies to trigger channels, not output channels
    };

    bool setOutputParam(uint8_t channel, uint8_t paramCode, float value, const char* context);
    bool program(uint8_t channel, uint8_t paramCode, uint32_t paramValue, const char* context);
    bool checkParam(uint8_t paramCode, float value, int channel, const char* context);
    uint32_t encodeParam(uint8_t paramCode, float value);
    uint32_t timeToCycles(float timeInSeconds);
    uint16_t voltageToInt16(float voltage);
    bool checkConnected(const char* context);
    bool checkOutputChannel(uint8_t channel, const char* context);
    bool sendCommand(const uint8_t* message, size_t nBytes, const char* context);
    bool readConfirm(const char* context);
    void reportError(const std::string& message);

    SerialPort* serial;
    SerialPort* ownedSerial; // The port created by PulsePal(), deleted by ~PulsePal(). NULL if the caller owns it
    bool connected;
    uint32_t firmwareVersion;
    uint32_t hardwareVersion;
    double cycleFrequency;   // Hardware timer cycles per second
    uint8_t nCustomTrains;
    uint32_t maxCustomPulses;
    uint8_t continuousLoop[5]; // Continuous loop state set by setContinuousLoop(), which op 92 also sends. 1-indexed
};

#endif // PULSEPAL_PULSEPAL_H

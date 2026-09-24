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

// Example program that calls Pulse Pal API functions. It needs a connected Pulse Pal 2 or 3.
//
// Build it with CMake (see README.md), then run it with Pulse Pal's serial port:
//   Windows: PulsePalExample COM3
//   macOS:   ./PulsePalExample /dev/cu.usbmodem14101
//   Linux:   ./PulsePalExample /dev/ttyACM0

#include <chrono>
#include <iostream>
#include <thread>

#include "PulsePal.h"

static void wait(int milliseconds)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

int main(int argc, char* argv[])
{
    if (argc < 2) {
        std::cout << "Usage: PulsePalExample <serial port>, e.g. PulsePalExample COM3" << std::endl;
        return 1;
    }

    // Initialize
    PulsePal PulsePalObject;
    if (!PulsePalObject.initialize(argv[1])) {
        return 1;
    }
    std::cout << "Connected to Pulse Pal " << PulsePalObject.getHardwareVersion()
              << " with firmware v" << PulsePalObject.getFirmwareVersion() << std::endl;
    PulsePalObject.setClientIDString("C++App"); // A 6-character string specifying your app's name, that tops PulsePal's screen

    // Set parameters for channels 1 and 2
    PulsePalObject.setPhase1Voltage(1, 5); PulsePalObject.setPhase1Voltage(2, 5); // Set voltage to 5V on output channels 1 and 2
    PulsePalObject.setPhase1Duration(1, .001f); PulsePalObject.setPhase1Duration(2, .001f); // Set duration to 1ms
    PulsePalObject.setInterPulseInterval(1, .1f); PulsePalObject.setInterPulseInterval(2, .1f); // Set interval to 100ms
    PulsePalObject.setPulseTrainDuration(1, 5); PulsePalObject.setPulseTrainDuration(2, 5); // Set train duration to 5s

    // Examples of software-triggering
    PulsePalObject.triggerChannel(1); PulsePalObject.triggerChannel(3); // Channel-wise trigger function
    PulsePalObject.triggerChannels(1, 1, 0, 0); // Function allowing simultaneous triggering. Arguments are 1 (stimulate) or 0 (not) for channels 1, 2, 3, 4.
    wait(2000); // Wait for stimulation to play for 2 seconds
    PulsePalObject.abortPulseTrains(); // Aborts the 5-second pulse trains after 2 seconds.

    // Setting an output channel to a fixed voltage
    PulsePalObject.setFixedVoltage(3, 10); // Sets the voltage on output channel 3 to 10V

    // Example of programming a custom pulse train on output channel 2
    uint16_t nPulses = 4;
    float customVoltages[4] = { 10, 2.5f, -2.5f, -10 };
    float customPulseTimes[4] = { 0, 0.001f, 0.002f, 0.005f };
    PulsePalObject.sendCustomPulseTrain(1, nPulses, customPulseTimes, customVoltages); // Program custom pulse train 1
    PulsePalObject.setCustomTrainID(2, 1); // Set output channel 2 to use custom train 1
    PulsePalObject.setCustomTrainLoop(2, 1); // Set output channel 2 to loop its custom pulse train until pulseTrainDuration seconds.
    PulsePalObject.setPulseTrainDuration(2, 2); // Set output channel 2 to play (the loop) for 2 seconds

    // Set a different custom pulse train on output channel 1
    nPulses = 2;
    float customVoltages2[2] = { 5, 10 };
    float customPulseTimes2[2] = { 0, 0.005f };
    PulsePalObject.sendCustomPulseTrain(2, nPulses, customPulseTimes2, customVoltages2);
    PulsePalObject.setCustomTrainID(1, 2); // Set output channel 1 to use custom train 2
    PulsePalObject.setCustomTrainLoop(1, 0); // Do not loop this one (default)

    PulsePalObject.triggerChannels(1, 1, 0, 0); // Trigger channels 1 and 2
    wait(3000); // Wait for stimulation to play for 3 seconds

    // Pulse Pal 3 has custom trains 3 and 4 as well
    if (PulsePalObject.getHardwareVersion() > 2) {
        float customVoltages3[3] = { 2, 4, 6 };
        float customPulseTimes3[3] = { 0, 0.01f, 0.02f };
        PulsePalObject.sendCustomPulseTrain(4, 3, customPulseTimes3, customVoltages3);
        PulsePalObject.setCustomTrainID(4, 4); // Set output channel 4 to use custom train 4
    }

    // An alternate method for programming, using the Pulse Pal object's parameter fields (sends all parameters to Pulse Pal at once)
    PulsePalObject.currentOutputParams[1].phase1Voltage = 5.1f; // set output channel 1 phase voltage to 5.1V
    PulsePalObject.currentOutputParams[3].phase1Duration = .001f; // set output channel 3 phase duration to 1ms
    PulsePalObject.currentOutputParams[1].interPulseInterval = .2f; // set output channel 1 pulse interval to 200ms
    PulsePalObject.currentOutputParams[1].pulseTrainDuration = 2; // set output channel 1 train to 2 sec
    PulsePalObject.currentOutputParams[1].restingVoltage = 0; // set output channel 1 resting voltage to 0V
    PulsePalObject.syncAllParams();

    // Set hardware-trigger link (trigger channels to output channels)
    PulsePalObject.setTrigger1Link(1, 1); // Link output channel 1 to trigger channel 1
    PulsePalObject.setTrigger1Link(2, 1); // Link output channel 2 to trigger channel 1
    PulsePalObject.setTrigger1Link(3, 0); // Un-Link output channel 3 from trigger channel 1
    PulsePalObject.setTrigger2Link(4, 0); // Un-link output channel 4 from trigger channel 2

    // Set hardware-trigger mode
    PulsePalObject.setTriggerMode(1, 0); // Set trigger channel 1 to normal mode
    PulsePalObject.setTriggerMode(1, 1); // Set trigger channel 1 to ttl-toggle mode
    PulsePalObject.setTriggerMode(2, 2); // Set trigger channel 2 to pulse-gated mode

    // Param sync mode (Pulse Pal 3): send the next trial's parameters during the current trial. They are applied
    // at the next rising edge on trigger channel 2.
    if (PulsePalObject.getHardwareVersion() > 2) {
        PulsePalObject.setTriggerMode(2, 3); // Set trigger channel 2 to param sync mode
        PulsePalObject.currentOutputParams[1].phase1Voltage = 2.5f;
        PulsePalObject.syncAllParams(); // Stored on the device, until the next rising edge on trigger channel 2
        // ... in an experiment, a TTL at the start of the next trial applies them ...
        PulsePalObject.setTriggerMode(2, 0); // Leave param sync mode. A stored set that was never applied is discarded,
        PulsePalObject.syncAllParams();      // so program it now, to match the device to currentOutputParams again
    }

    PulsePalObject.end();
    return 0;
}

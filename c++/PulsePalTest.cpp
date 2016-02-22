/*
----------------------------------------------------------------------------

This file is part of the PulsePal Project
Copyright (C) 2014 Joshua I. Sanders, Cold Spring Harbor Laboratory, NY, USA

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

// Test-program that calls Pulse Pal API functions

#include "stdafx.h"
#include "PulsePal.h"

int _tmain(int argc, _TCHAR* argv[])
{
	// Initialize
	PulsePal PulsePalObject;
	PulsePalObject.initialize();
	uint32_t FV = PulsePalObject.getFirmwareVersion(); // Get handshake byte and return firmware version
	std::cout << "Current firmware version:" << std::endl;
	std::cout << FV << std::endl;
	PulsePalObject.setClientIDString(" MyApp"); // A 6-character string specifying your app's name, that tops PulsePal's OLED menu tree
	std::cin.get(); // Wait so you can see your app name (press return to continue)

	// Set parameters for channels 1 and 3
	PulsePalObject.setPhase1Voltage(1, 5); PulsePalObject.setPhase1Voltage(2, 5); // Set voltage to 5V on output channels 1 and 2
	PulsePalObject.setPhase1Duration(1, .001); PulsePalObject.setPhase1Duration(2, .001); // Set duration to 1ms
	PulsePalObject.setInterPulseInterval(1, .1); PulsePalObject.setInterPulseInterval(2, .1); // Set interval to 100ms
	PulsePalObject.setPulseTrainDuration(1, 5); PulsePalObject.setPulseTrainDuration(2, 5); // Set train duration to 5s

	// Examples of software-triggering
	//PulsePalObject.triggerChannel(1); PulsePalObject.triggerChannel(3); // Previous channel-wise trigger function
	PulsePalObject.triggerChannels(1, 1, 0, 0); // Function allowing simultaneous triggering. Arguments are 1 (stimulate) or 0 (not) for channels 1, 2, 3, 4.
	Sleep(1000);
	PulsePalObject.abortPulseTrains(); // Aborts the 5-second pulse trains after 1 second.

	// Accessory functions
	PulsePalObject.setFixedVoltage(3, 10); // Sets the voltage on output channel 3 to 10V
	PulsePalObject.updateDisplay("Press", "Return. . ."); // Write text strings to screen

	// Example of programming a custom pulse train on output channel 2
	float customVoltages[4] = { 10, 2.5, -2.5, -10 };
	float customPulseTimes[4] = { 0, 0.001, 0.002, 0.005};
	uint8_t nPulses = 4;
	PulsePalObject.sendCustomPulseTrain(1, nPulses, customPulseTimes, customVoltages); // Program custom pulse train 1
	PulsePalObject.setCustomTrainID(2, 1); // Set output channel 2 to use custom train 1
	PulsePalObject.setCustomTrainLoop(2, 1); // Set output channel 2 to loop its custom pulse train until pulseTrainDuration seconds.
	PulsePalObject.setPulseTrainDuration(2, 2); // Set output channel 2 to play (the loop) for 2 seconds

	// Set output channel 1 to play pulses synchronized to channel 2 (for easy o-scope triggering - pulses aligned to waveform onsets on Ch2)
	float customVoltages2[2] = { 5, 5 };
	float customPulseTimes2[2] = { 0, 0.005 };
	nPulses = 2;
	PulsePalObject.sendCustomPulseTrain(2, nPulses, customPulseTimes2, customVoltages2);
	PulsePalObject.setCustomTrainID(1, 2); // Set output channel 1 to use custom train 2
	PulsePalObject.setCustomTrainLoop(1, 1); // Also loop this one
	PulsePalObject.setPulseTrainDuration(1, 2);

	// An alternate method for programming, using the Pulse Pal object's parameter fields (sends all parameters to Pulse Pal at once)
	PulsePalObject.currentOutputParams[1].phase1Voltage = 5; // set output channel 1 phase voltage to 5V
	PulsePalObject.currentOutputParams[3].phase1Duration = .001; // set output channel 3 phase duration to 1ms
	PulsePalObject.currentOutputParams[1].interPulseInterval = .2; // set output channel 1 pulse interval to 200ms
	PulsePalObject.currentOutputParams[1].pulseTrainDuration = 2; // set output channel 1 train to 2 sec
	PulsePalObject.currentOutputParams[1].restingVoltage = 0; // set output channel 1 resting voltage to 0V
	PulsePalObject.syncAllParams();

	// Set hardware-trigger link (trigger channels to output channels)
	PulsePalObject.setTrigger1Link(1, 1); // Link output channel 1 to trigger channel 1
	PulsePalObject.setTrigger1Link(2, 1); // Link output channel 2 to trigger channel 1
	PulsePalObject.setTrigger1Link(3, 0); // Un-Link output channel 3 to trigger channel 1
	PulsePalObject.setTrigger2Link(4, 0); // Un-link output channel 4 from trigger channel 2

	// Set hardware-trigger mode
	PulsePalObject.setTriggerMode(1, 0); // Set trigger channel 1 to normal mode
	PulsePalObject.setTriggerMode(1, 1); // Set trigger channel 1 to ttl-toggle mode
	PulsePalObject.setTriggerMode(2, 2); // Set trigger channel 2 to pulse-gated mode

	
	cin.get();
	return 0;
}


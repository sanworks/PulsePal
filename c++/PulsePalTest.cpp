/*
----------------------------------------------------------------------------

This file is part of the Pulse Pal Project
Copyright (C) 2016 Joshua I. Sanders, Sanworks LLC, NY, USA

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

// Note: In Microsoft Visual Studio, you may have to force Microsoft to allow sprintf 
// and related formatting functions used by Open Frameworks.
// In Visual Studio 2015, go to Project > Properties > C/C++ > Preprocessor, and add the following preprocessor definition:
// _CRT_SECURE_NO_WARNINGS;

// To run this code in Visual Studio (Community) 2015: 
// 1. Create a new project called PulsePalTest.
// 2. Move this file to the project folder: /Documents/Visual Studio 2015/Projects/PulsePalTest/PulsePalTest/ overwriting PulsePalTest.cpp
// 3. Also move PulsePal.h, PulsePal.cpp, ofSerial.h, ofSerial.cpp and ofConstants.h to the same folder.
// 4. In the solution explorer, click Add > Existing Item. From the file browser, select all of the files you just imported.
// 5. Modify the line "PulsePalObject.initialize("COM3");" to your actual serial port string for Pulse Pal (look in the device manager for Arduino Due) 
// 6. You should now be able to run the code by clicking "Local Windows Debugger" above the editor window.

#include "stdafx.h"
#include "PulsePal.h"

int _tmain(int argc, _TCHAR* argv[])
{
	// Initialize
	PulsePal PulsePalObject;
	PulsePalObject.initialize("COM13");
	uint32_t FV = PulsePalObject.getFirmwareVersion(); // Get handshake byte and return firmware version
	std::cout << "Current firmware version:" << std::endl;
	std::cout << FV << std::endl;
	PulsePalObject.setClientIDString("C++App"); // A 6-character string specifying your app's name, that tops PulsePal's OLED menu tree
	
	// Set parameters for channels 1 and 3
	PulsePalObject.setPhase1Voltage(1, 5); PulsePalObject.setPhase1Voltage(2, 5); // Set voltage to 5V on output channels 1 and 2
	PulsePalObject.setPhase1Duration(1, .001); PulsePalObject.setPhase1Duration(2, .001); // Set duration to 1ms
	PulsePalObject.setInterPulseInterval(1, .1); PulsePalObject.setInterPulseInterval(2, .1); // Set interval to 100ms
	//PulsePalObject.setPulseTrainDuration(1, 5); PulsePalObject.setPulseTrainDuration(2, .5); // Set train duration to 5s
	
	// Examples of software-triggering
	PulsePalObject.triggerChannel(1); PulsePalObject.triggerChannel(3); // Legacy channel-wise trigger function
	PulsePalObject.triggerChannels(1, 1, 0, 0); // Function allowing simultaneous triggering. Arguments are 1 (stimulate) or 0 (not) for channels 1, 2, 3, 4.
	Sleep(2000); // Wait for stimulation to play for 2 seconds
	PulsePalObject.abortPulseTrains(); // Aborts the 5-second pulse trains after 1 second.

	// Setting an output channel to a fixed voltage
	PulsePalObject.setFixedVoltage(3, 10); // Sets the voltage on output channel 3 to 10V

	// Example of programming a custom pulse train on output channel 2
	uint16_t nPulses = 4;
	float customVoltages[4] = { 10, 2.5, -2.5, -10 };
	float customPulseTimes[4] = { 0, 0.001, 0.002, 0.005};
	PulsePalObject.sendCustomPulseTrain(1, nPulses, customPulseTimes, customVoltages); // Program custom pulse train 1
	PulsePalObject.setCustomTrainID(2, 1); // Set output channel 2 to use custom train 1
	PulsePalObject.setCustomTrainLoop(2, 1); // Set output channel 2 to loop its custom pulse train until pulseTrainDuration seconds.
	PulsePalObject.setPulseTrainDuration(2, 2); // Set output channel 2 to play (the loop) for 2 seconds

	// Set a different custom pulse train on output channel 1
	nPulses = 2;
	float customVoltages2[2] = { 5, 10 };
	float customPulseTimes2[2] = { 0, 0.005 };
	
	PulsePalObject.sendCustomPulseTrain(2, nPulses, customPulseTimes2, customVoltages2);
	PulsePalObject.setCustomTrainID(1, 2); // Set output channel 1 to use custom train 2
	PulsePalObject.setCustomTrainLoop(1, 0); // Do not loop this one (default)

	PulsePalObject.triggerChannels(1, 1, 0, 0); // Trigger channels 1 and 2
	Sleep(3000); // Wait for stimulation to play for 3 seconds
	
	// An alternate method for programming, using the Pulse Pal object's parameter fields (sends all parameters to Pulse Pal at once)
	PulsePalObject.currentOutputParams[1].phase1Voltage = 5.1; // set output channel 1 phase voltage to 5.1V
	PulsePalObject.currentOutputParams[3].phase1Duration = .001; // set output channel 3 phase duration to 1ms
	PulsePalObject.currentOutputParams[1].interPulseInterval = .2; // set output channel 1 pulse interval to 200ms
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

	PulsePalObject.end();
	return 0;
}


"""
----------------------------------------------------------------------------

This file is part of the Sanworks PulsePal repository
Copyright (C) Sanworks LLC, Rochester, New York, USA

----------------------------------------------------------------------------

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, version 3.

This program is distributed  WITHOUT ANY WARRANTY and without even the
implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
"""

from ArCOM import ArCom
from decimal import Decimal
import numpy as np
import math


class PulsePalObject(object):
    # Constants
    OP_MENU_BYTE = 213
    HANDSHAKE_OPCODE = 72
    HANDSHAKE_RESPONSE = 75
    DAC_BITMAX_MODEL_1 = 255
    DAC_BITMAX_MODEL_2 = 65535
    CYCLE_FREQUENCY = 20000  # Hz
    
    def __init__(self, port_name):
        """
        Initializes a new instance of the PulsePalObject.

        Args:
            port_name (str): The name of the USB serial port to which the Pulse Pal is connected (e.g. COM3 on Windows)

        Raises:
            PulsePalError: If there is a problem initializing the connection.
        """
        self.Port = ArCom(port_name, 12000000)  # ArCom (Arduino Communication) wraps PySerial
        #                                         to simplify data transactions with Arduino
        self._model = 0
        self._dac_bitMax = self._toDecimal(0)
        
        # handshake to confirm connectivity
        self.Port.write((self.OP_MENU_BYTE, self.HANDSHAKE_OPCODE), 'uint8')
        handshake = self.Port.read(1, 'uint8')
        if handshake != self.HANDSHAKE_RESPONSE:
            raise PulsePalError('Error: incorrect handshake returned.')
        
        # setup
        firmware_version = self.Port.read(1, 'uint32')
        if firmware_version < 20:  # Model can be inferred from firmware version. Model 1 spanned firmware v1-19.
            self._model = 1
            self._dac_bitMax = self._toDecimal(self.DAC_BITMAX_MODEL_1)
        else:
            self._model = 2
            self._dac_bitMax = self._toDecimal(self.DAC_BITMAX_MODEL_2)
        self.firmware_version = firmware_version
        self._firmware = int(firmware_version[0])  # The version as an int, for the checks below
        if self.firmware_version == 20:
            print("Notice: NOTE: A firmware update is available. It fixes a bug in Pulse Gated trigger mode when used with multiple inputs.")
            print("To update, follow the instructions at https://sites.google.com/site/pulsepalwiki/updating-firmware")
        self.Port.write((self.OP_MENU_BYTE, 89, 80, 89, 84, 72, 79, 78), 'uint8')  # Client name op + 'PYTHON' in ASCII
        self.outputParameterNames = ['isBiphasic', 'phase1Voltage', 'phase2Voltage', 'phase1Duration', 
                                     'interPhaseInterval', 'phase2Duration', 'interPulseInterval', 'burstDuration', 
                                     'interBurstInterval', 'pulseTrainDuration', 'pulseTrainDelay', 
                                     'linkTriggerChannel1', 'linkTriggerChannel2', 'customTrainID',
                                     'customTrainTarget', 'customTrainLoop', 'restingVoltage']
        self.triggerParameterNames = ['triggerMode']
        self.set2DefaultParams()  # Initializes all parameters to default values
        self.syncAllParams()  # Sets all parameters on the device to the current class parameters

    def set2DefaultParams(self):
        """
           Returns all params to the defaults. This is called by the constructor, and may be
           subsequently called by the user (e.g. before running a new experiment)
        """
        self.isBiphasic = [float('nan'), 0, 0, 0, 0]
        self.phase1Voltage = [float('nan'), 5, 5, 5, 5]
        self.phase2Voltage = [float('nan'), -5, -5, -5, -5]
        self.restingVoltage = [float('nan'), 0, 0, 0, 0]
        self.phase1Duration = [float('nan'), 0.001, 0.001, 0.001, 0.001]
        self.interPhaseInterval = [float('nan'), 0.001, 0.001, 0.001, 0.001]
        self.phase2Duration = [float('nan'), 0.001, 0.001, 0.001, 0.001]
        self.interPulseInterval = [float('nan'), 0.01, 0.01, 0.01, 0.01]
        self.burstDuration = [float('nan'), 0, 0, 0, 0]
        self.interBurstInterval = [float('nan'), 0, 0, 0, 0]
        self.pulseTrainDuration = [float('nan'), 1, 1, 1, 1]
        self.pulseTrainDelay = [float('nan'), 0, 0, 0, 0]
        self.linkTriggerChannel1 = [float('nan'), 1, 1, 1, 1]
        self.linkTriggerChannel2 = [float('nan'), 0, 0, 0, 0]
        self.customTrainID = [float('nan'), 0, 0, 0, 0]
        self.customTrainTarget = [float('nan'), 0, 0, 0, 0]
        self.customTrainLoop = [float('nan'), 0, 0, 0, 0]
        self.triggerMode = [float('nan'), 0, 0]

    def setFixedVoltage(self, channel, voltage):
        """
        Sets a fixed voltage for the specified output channel.

        Args:
            channel (int): The channel number to set (1-4)
            voltage (float): The voltage level to set. Units = Volts.

        Raises:
            PulsePalError: If the device does not acknowledge the command.
        """
        voltage_bits = self._volts2Bits(voltage)
        if self._model == 1:
            self.Port.write((self.OP_MENU_BYTE, 79, channel, voltage_bits), 'uint8')
        else:
            self.Port.write((self.OP_MENU_BYTE, 79, channel), 'uint8', voltage_bits, 'uint16')
        ok = self.Port.read(1, 'uint8')  # Receive acknowledgement
        if len(ok) == 0:
            raise PulsePalError('Error: Pulse Pal did not return an acknowledgement after a call to setFixedVoltage().')
        
    def programOutputChannelParam(self, param_name, channel, value):
        """
        Programs a parameter for an output channel on the Pulse Pal device.

        Args:
            param_name (str): The name of the parameter to program. Names are given above in self.outputParameterNames.
            channel (int): The output channel number to program the parameter for (1-4)
            value: The value to set for the parameter. Units: Voltage (if volts), Seconds (if time), Integer (if item)

        Raises:
            PulsePalError: If the device does not acknowledge the command.
        """
        original_value = value
        if isinstance(param_name, str):
            param_code = self.outputParameterNames.index(param_name)+1
        else:
            param_code = param_name
        time_cycles = None
        interval_message = None  # Op 74 for the channel's interPulseInterval, sent with a change of isBiphasic
        if 4 <= param_code <= 11:
            time_cycles = self._seconds2Cycles(value)
        if self._firmware > 21 and param_code in (1, 4, 6, 7):
            # Firmware v22 and newer refuse some times of 0 that v21 accepted, and _channelCycles() translates them
            # depending on whether the channel is biphasic.
            times = [1]*8  # Only the times set below can be translated or refused
            is_biphasic = self.isBiphasic[channel]
            if param_code == 1:
                is_biphasic = value
                times[2] = self.phase2Duration[channel]
                times[3] = self.interPulseInterval[channel]
            else:
                times[param_code-4] = value
            cycles = self._channelCycles(channel, times, is_biphasic)
            if param_code == 1:
                zero_interval = self.interPulseInterval[channel] == 0
                if value == 0:
                    # Only a biphasic channel can hold an interval of 0, which may have been set before this session,
                    # so the device's own value is read (op 93, which starts with 4 uint32 times per parameter)
                    self.Port.write((self.OP_MENU_BYTE, 93), 'uint8')
                    times_now, rest = self.Port.read(32, 'uint32', 50, 'uint8')
                    zero_interval = zero_interval or times_now[12 + channel - 1] == 0
                    cycles[3] = self._seconds2Cycles(3600)
                if zero_interval:
                    # An interval of 0 is sent as 3600 s while the channel is monophasic, and as 0 while it is biphasic
                    interval_message = ((self.OP_MENU_BYTE, 74, 7, channel), 'uint8', cycles[3], 'uint32')
            else:
                time_cycles = cycles[param_code-4]

        # The device checks each channel's parameters together, and refuses a monophasic channel with an interval
        # of 0: the translated interval goes before a change to monophasic, and after a change to biphasic.
        if interval_message is not None and value == 0:
            self.Port.write(*interval_message)
            ok = self.Port.read(1, 'uint8')
        if 2 <= param_code <= 3 or param_code == 17:
            value = self._volts2Bits(value)
            if self._model == 1:
                self.Port.write((self.OP_MENU_BYTE, 74, param_code, channel, value), 'uint8')
            else:
                self.Port.write((self.OP_MENU_BYTE, 74, param_code, channel), 'uint8', value, 'uint16')
        elif 4 <= param_code <= 11:
            self.Port.write((self.OP_MENU_BYTE, 74, param_code, channel), 'uint8', time_cycles, 'uint32')
        else:
            self.Port.write((self.OP_MENU_BYTE, 74, param_code, channel, value), 'uint8')

        # Receive acknowledgement
        ok = self.Port.read(1, 'uint8')
        if len(ok) == 0:
            raise PulsePalError('Pulse Pal did not return an acknowledgement after call to programOutputChannelParam().')
        if interval_message is not None and value != 0:
            self.Port.write(*interval_message)
            ok = self.Port.read(1, 'uint8')

        # Update the PulsePal object's parameter fields
        if param_code == 1:
            self.isBiphasic[channel] = original_value
        elif param_code == 2:
            self.phase1Voltage[channel] = original_value
        elif param_code == 3:
            self.phase2Voltage[channel] = original_value
        elif param_code == 4:
            self.phase1Duration[channel] = original_value
        elif param_code == 5:
            self.interPhaseInterval[channel] = original_value
        elif param_code == 6:
            self.phase2Duration[channel] = original_value
        elif param_code == 7:
            self.interPulseInterval[channel] = original_value
        elif param_code == 8:
            self.burstDuration[channel] = original_value
        elif param_code == 9:
            self.interBurstInterval[channel] = original_value
        elif param_code == 10:
            self.pulseTrainDuration[channel] = original_value
        elif param_code == 11:
            self.pulseTrainDelay[channel] = original_value
        elif param_code == 12:
            self.linkTriggerChannel1[channel] = original_value
        elif param_code == 13:
            self.linkTriggerChannel2[channel] = original_value
        elif param_code == 14:
            self.customTrainID[channel] = original_value
        elif param_code == 15:
            self.customTrainTarget[channel] = original_value
        elif param_code == 16:
            self.customTrainLoop[channel] = original_value
        elif param_code == 17:
            self.restingVoltage[channel] = original_value
            
    def programTriggerChannelParam(self, param_name, channel, value):
        """
        Programs a parameter for the trigger channel on the Pulse Pal device.

        Args:
            param_name (str): The name of the parameter to program. Names are given above in self.triggerParameterNames.
            channel (int): The trigger channel number to program the parameter for (1-2)
            value: The value to set for the parameter.

        Raises:
            PulsePalError: If the device does not acknowledge the command.
        """
        original_value = value
        if isinstance(param_name, str):
            param_code = self.triggerParameterNames.index(param_name)+128
        else:
            param_code = param_name
        if param_code == 128:
            value = self._triggerModeToSend(value)
        self.Port.write((self.OP_MENU_BYTE, 74, param_code, channel, value), 'uint8')
        # Receive acknowledgement
        ok = self.Port.read(1, 'uint8')
        if len(ok) == 0:
            raise PulsePalError('Error: Pulse Pal did not return acknowledgement after call to programTriggerChannelParam().')
        if param_code == 128:  # Kept, so that syncAllParams() sends the same mode
            self.triggerMode[channel] = original_value
            
    def syncAllParams(self):
        """
        Synchronizes all current parameters of the PulsePalObject to the device

        Raises:
            PulsePalError: If the device does not acknowledge the synchronization command.
        """
        # Preallocate
        program_values_16 = []
        program_values_32 = []
        if self._model == 1:
            program_values_8 = [0]*28
        else:
            program_values_8 = [0]*16

        # Prepare 32-bit time params
        pos = 0
        for i in range(1, 5):
            program_values_32.extend(self._channelCycles(i, [
                self.phase1Duration[i],
                self.interPhaseInterval[i],
                self.phase2Duration[i],
                self.interPulseInterval[i],
                self.burstDuration[i],
                self.interBurstInterval[i],
                self.pulseTrainDuration[i],
                self.pulseTrainDelay[i]
            ], self.isBiphasic[i]))
        
        # Prepare 16-bit voltages for Pulse Pal v2 (Pulse Pal v1 has 8-bit voltages set with other 8-bit params below)
        if self._model == 2:
            pos = 0
            for i in range(1, 5):
                program_values_16.extend([
                    self._volts2Bits(self.phase1Voltage[i]),
                    self._volts2Bits(self.phase2Voltage[i]),
                    self._volts2Bits(self.restingVoltage[i])
                ])

        # Prepare 8-bit params
        pos = 0
        for i in range(1, 5):
            program_values_8[pos] = self.isBiphasic[i]
            pos += 1
            if self._model == 1:
                program_values_8[pos] = self._volts2Bits(self.phase1Voltage[i])
                pos += 1
                program_values_8[pos] = self._volts2Bits(self.phase2Voltage[i])
                pos += 1
            program_values_8[pos] = self.customTrainID[i]
            pos += 1
            program_values_8[pos] = self.customTrainTarget[i]
            pos += 1
            program_values_8[pos] = self.customTrainLoop[i]
            pos += 1
            if self._model == 1:
                program_values_8[pos] = self._volts2Bits(self.restingVoltage[i])
                pos += 1

        # Prepare trigger channel link params
        program_values_tl = [0]*8
        pos = 0
        for i in range(1, 5):
            program_values_tl[pos] = self.linkTriggerChannel1[i]
            pos += 1
        for i in range(1, 5):
            program_values_tl[pos] = self.linkTriggerChannel2[i]
            pos += 1
        trigger_modes = [self._triggerModeToSend(mode) for mode in self.triggerMode[1:3]]
            
        # Send all params to device
        if self._model == 1:
            self.Port.write(
                (self.OP_MENU_BYTE, 73), 'uint8',
                program_values_32, 'uint32',
                program_values_8, 'uint8',
                program_values_tl, 'uint8',
                trigger_modes, 'uint8')
        if self._model == 2:
            self.Port.write(
                (self.OP_MENU_BYTE, 73), 'uint8',
                program_values_32, 'uint32',
                program_values_16, 'uint16',
                program_values_8, 'uint8',
                program_values_tl, 'uint8',
                trigger_modes, 'uint8')

        # Receive acknowledgement
        ok = self.Port.read(1, 'uint8')
        if len(ok) == 0:
            raise PulsePalError('Error: Pulse Pal did not return an acknowledgement byte after a call to syncAllParams().')
        
    def sendCustomPulseTrain(self, custom_train_id, pulse_times, pulse_voltages):
        """
        Sends a custom pulse train to the Pulse Pal device.

        Args:
            custom_train_id (int): The ID of the custom train to send (1-2)
            pulse_times (list of float): The times at which each pulse should occur. Units = seconds.
            pulse_voltages (list of float): The voltages for each pulse. Units = volts.

        Raises:
            PulsePalError: If the device does not acknowledge the command.
        """
        if isinstance(pulse_times, np.ndarray):
            pulse_times = pulse_times.tolist()
        if isinstance(pulse_voltages, np.ndarray):
            pulse_voltages = pulse_voltages.tolist()
        n_pulses = len(pulse_times)
        for i in range(0, n_pulses):
            pulse_times[i] = self._seconds2Cycles(pulse_times[i])  # Convert seconds to multiples of update cycle (100us)
            pulse_voltages[i] = self._volts2Bits(pulse_voltages[i])
        
        op_code = custom_train_id + 74  #Serial op codes are 75 if custom train 1, 76 if 2
        if self._model == 1:
            self.Port.write((self.OP_MENU_BYTE, op_code, 0), 'uint8', n_pulses, 'uint32', pulse_times, 'uint32', pulse_voltages, 'uint8')
        else:
            self.Port.write((self.OP_MENU_BYTE, op_code), 'uint8', n_pulses, 'uint32', pulse_times, 'uint32', pulse_voltages, 'uint16')
        ok = self.Port.read(1, 'uint8')  # Receive acknowledgement
        if len(ok) == 0:
            raise PulsePalError('Error: Pulse Pal did not return an acknowledgement byte after a call to sendCustomPulseTrain().')
        
    def sendCustomWaveform(self, custom_train_id, pulse_width, pulse_voltages):  # For custom pulse trains with pulse times = pulse width
        """
        Sends a custom waveform to the Pulse Pal device.

        Args:
            custom_train_id (int): The ID of the custom waveform train to send (1-2)
            pulse_width (float): The width of each pulse in the waveform. Units = seconds.
            pulse_voltages (list of float): The voltages for each pulse in the waveform. Units = volts.

        Raises:
            PulsePalError: If the device does not acknowledge the command.
        """
        n_pulses = len(pulse_voltages)
        pulse_times = [0]*n_pulses
        pulse_width_cycles = self._seconds2Cycles(pulse_width)  # Convert seconds to multiples of minimum cycle (100us)
        if isinstance(pulse_voltages, np.ndarray):
            pulse_voltages = pulse_voltages.tolist()
        for i in range(0, n_pulses):
            pulse_times[i] = pulse_width_cycles*i  # Add consecutive pulse
            pulse_voltages[i] = self._volts2Bits(pulse_voltages[i])
        op_code = custom_train_id + 74  # 75 if custom train 1, 76 if 2
        if self._model == 1:
            self.Port.write((self.OP_MENU_BYTE, op_code, 0), 'uint8', n_pulses, 'uint32', pulse_times, 'uint32', pulse_voltages, 'uint8')
        else:
            self.Port.write((self.OP_MENU_BYTE, op_code), 'uint8', n_pulses, 'uint32', pulse_times, 'uint32', pulse_voltages, 'uint16')
        # Receive acknowledgement
        ok = self.Port.read(1, 'uint8')
        if len(ok) == 0:
            raise PulsePalError('Error: Pulse Pal did not return an acknowledgement byte after a call to sendCustomWaveform().')

    def setContinuousLoop(self, channel, state):
        """
        Sets the continuous loop state for a specified output channel.

        Args:
            channel (int): The output channel number to set the continuous loop state (1-4)
            state (int): The state to set for the continuous loop (1 for on, 0 for off).
        """
        self.Port.write((self.OP_MENU_BYTE, 82, channel, state), 'uint8')
        # The device replies to this command. Left unread, the reply would be taken as the acknowledgement of the
        # next command, and every acknowledgement after that would be read one command late.
        ok = self.Port.read(1, 'uint8')
        if len(ok) == 0:
            raise PulsePalError('Error: Pulse Pal did not return an acknowledgement after a call to setContinuousLoop().')
        if state and self._firmware > 21:
            # Firmware v21 started the channel when continuous loop was switched on. Newer firmware waits for a
            # trigger, so the channel is triggered here. A channel that is already playing ignores it.
            self.Port.write((self.OP_MENU_BYTE, 77, 1 << (channel - 1)), 'uint8')

    def triggerOutputChannels(self, channel1, channel2, channel3, channel4):
        """
        Triggers the output channels on the Pulse Pal device.

        Args:
            channel1 (int): 1 to trigger Ch1, 0 if not
            channel2 (int): 1 to trigger Ch2, 0 if not
            channel3 (int): 1 to trigger Ch3, 0 if not
            channel4 (int): 1 to trigger Ch4, 0 if not
        """
        trigger_byte = (1*channel1) + (2*channel2) + (4*channel3) + (8*channel4)
        self.Port.write((self.OP_MENU_BYTE, 77, trigger_byte), 'uint8')

    def abortPulseTrains(self):
        """
        Aborts all pulse trains currently being output by the Pulse Pal device.
        """
        self.Port.write((self.OP_MENU_BYTE, 80), 'uint8')

    def _toDecimal(self, value):
        """
        Converts a value to a Decimal with a Pulse Pal's required precision.

        Args:
            value (float): The value to convert to Decimal.

        Returns:
            Decimal: The value converted to a Decimal with fixed precision (0.0000)
        """
        return Decimal(value).quantize(Decimal('1.0000'))

    def _volts2Bits(self, value):
        """
        Converts a voltage value in range -10V to +10V to its corresponding bit value for the 12-bit DAC.

        Args:
            value (float): The voltage value to convert.

        Returns:
            int: The bit value corresponding to the given voltage.
        """
        return math.ceil(((self._toDecimal(value) + 10) / self._toDecimal(20)) * self._dac_bitMax)

    def _seconds2Cycles(self, value):
        """
        Converts a time value in seconds to the corresponding number of refresh cycles for the Pulse Pal device.

        Args:
            value (float): The time value in seconds to convert.

        Returns:
            Decimal: The number of cycles corresponding to the given time value.
        """
        return self._toDecimal(value)*self._toDecimal(self.CYCLE_FREQUENCY)

    def _channelCycles(self, channel, times, is_biphasic):
        """
        Converts an output channel's time parameters to refresh cycles, for programming the device.

        Firmware v22 and newer refuse a phase or inter-pulse interval of 0 cycles, which firmware v21 accepted. With
        those versions, zeros are translated so that programs written for v21 play as they did:
        - interPulseInterval 0 on a monophasic channel: v21 played one pulse per train (or one per burst), because it
          scheduled the next pulse for a cycle that had already passed. Newer firmware would use 1 cycle instead, and
          play pulses 50 us apart. 3600 s is sent, so that only the first pulse plays.
        - phase2Duration 0 on a monophasic channel: phase 2 is not played, so 1 cycle is sent.
        - phase1Duration 0, or phase2Duration 0 on a biphasic channel: v21 held the phase's voltage until the train
          (or burst) ended. Newer firmware cannot play that, so PulsePalError is raised.

        Args:
            channel (int): The output channel (1-4), for error messages
            times (list of float): The 8 time parameters in seconds, in parameter code order (phase1Duration,
                interPhaseInterval, phase2Duration, interPulseInterval, burstDuration, interBurstInterval,
                pulseTrainDuration, pulseTrainDelay)
            is_biphasic (int): 1 if the channel plays biphasic pulses, 0 if not

        Returns:
            list: The 8 times in refresh cycles
        """
        cycles = [self._seconds2Cycles(t) for t in times]
        if self._firmware > 21:
            if int(cycles[0]) == 0 or (is_biphasic and int(cycles[2]) == 0):
                raise PulsePalError('Error in output channel ' + str(channel) +
                                    ': phase durations must be at least 100 us.')
            if not is_biphasic:
                if int(cycles[2]) == 0:
                    cycles[2] = 1
                if int(cycles[3]) == 0:
                    cycles[3] = self._seconds2Cycles(3600)
        return cycles

    def _triggerModeToSend(self, mode):
        """
        Returns the trigger mode to program. Firmware v21 played trigger modes above 2 as normal mode (0). On Pulse
        Pal 3 with firmware v22 and newer, mode 3 is param sync mode, in which trigger edges start nothing, so 0 is
        sent instead.
        """
        if self._firmware > 21 and mode > 2:
            return 0
        return mode

    def __del__(self):
        """
        Destructor method that ensures the Pulse Pal connection is closed if the object is deleted.
        """
        self.Port.write((self.OP_MENU_BYTE, 81), 'uint8')
        self.Port.close()

        
class PulsePalError(Exception):
    pass

'''
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
'''

import struct
import math

class PulsePalObject(object):

    def __init__(self):
        self.serialObject = 0
        self.OpMenuByte = 213
        self.firmwareVersion = 0
        self.cycleFrequency = 20000;
        self.isBiphasic = [0]*5;
        self.phase1Voltage = [5]*5;
        self.phase2Voltage = [-5]*5;
        self.restingVoltage = [0]*5;
        self.phase1Duration = [0.001]*5;
        self.interPhaseInterval = [0.001]*5;
        self.phase2Duration = [0.001]*5;
        self.interPulseInterval = [0.01]*5;
        self.burstDuration = [0]*5;
        self.interBurstInterval = [0]*5;
        self.pulseTrainDuration = [1]*5;
        self.pulseTrainDelay = [0]*5;
        self.linkTriggerChannel1 = [1]*5;
        self.linkTriggerChannel2 = [0]*5;
        self.customTrainID = [0]*5;
        self.customTrainTarget = [0]*5;
        self.customTrainLoop = [0]*5;
        self.triggerMode = [0]*3;
        self.outputParameterNames = ['isBiphasic', 'phase1Voltage','phase2Voltage', 'phase1Duration', 'interPhaseInterval',
                               'phase2Duration','interPulseInterval', 'burstDuration', 'interBurstInterval', 'pulseTrainDuration',
                               'pulseTrainDelay', 'linkTriggerChannel1', 'linkTriggerChannel2', 'customTrainID',
                               'customTrainTarget', 'customTrainLoop', 'restingVoltage']
        self.triggerParameterNames = ['triggerMode']

    def connect(self, serialPortName):
        import serial
        self.serialObject = serial.Serial(serialPortName, 115200)
        handshakeByteString = struct.pack('BB', self.OpMenuByte, 72)
        self.serialObject.write(handshakeByteString)
        Response = self.serialObject.read(5)
        fvBytes = Response[1:5]
        self.firmwareVersion = struct.unpack('<I',fvBytes)[0]
        self.serialObject.write('YPYTHON')

    def disconnect(self):
        terminateByteString = struct.pack('BB', self.OpMenuByte, 81)
        self.serialObject.write(terminateByteString)
        self.serialObject.close()

    def programOutputChannelParam(self, paramName, channel, value):
        originalValue = value
        if isinstance(paramName, basestring):
            paramCode = self.outputParameterNames.index(paramName)+1
        else:
            paramCode = paramName

        if 2 <= paramCode <= 3:
            value = math.ceil(((value+10)/float(20))*255) # Convert volts to bits
        if paramCode == 17:
            value = math.ceil(((value+10)/float(20))*255) # Convert volts to bits
        if 4 <= paramCode <= 11:
            programByteString = struct.pack('<BBBBL',self.OpMenuByte,74,paramCode,channel,value*self.cycleFrequency)
        else:
            programByteString = struct.pack('BBBBB',self.OpMenuByte,74,paramCode,channel,value)
        self.serialObject.write(programByteString)
        # Update the PulsePal object's parameter fields
        if paramCode == 1:
            self.isBiphasic[channel] = originalValue
        elif paramCode == 2:
            self.phase1Voltage[channel] = originalValue
        elif paramCode == 3:
            self.phase2Voltage[channel] = originalValue
        elif paramCode == 4:
            self.phase1Duration[channel] = originalValue
        elif paramCode == 5:
            self.interPhaseInterval[channel] = originalValue
        elif paramCode == 6:
            self.phase2Duration[channel] = originalValue
        elif paramCode == 7:
            self.interPulseInterval[channel] = originalValue
        elif paramCode == 8:
            self.burstDuration[channel] = originalValue
        elif paramCode == 9:
            self.interBurstInterval[channel] = originalValue
        elif paramCode == 10:
            self.pulseTrainDuration[channel] = originalValue
        elif paramCode == 11:
            self.pulseTrainDelay[channel] = originalValue
        elif paramCode == 12:
            self.linkTriggerChannel1[channel] = originalValue
        elif paramCode == 13:
            self.linkTriggerChannel2[channel] = originalValue
        elif paramCode == 14:
            self.customTrainID[channel] = originalValue
        elif paramCode == 15:
            self.customTrainTarget[channel] = originalValue
        elif paramCode == 16:
            self.customTrainLoop[channel] = originalValue
        elif paramCode == 17:
            self.restingVoltage[channel] = originalValue

    def programTriggerChannelParam(self, paramName, channel, value):
        originalValue = value
        if isinstance(paramName, basestring):
            paramCode = self.triggerParameterNames.index(paramName)+1
        else:
            paramCode = paramName
        messageBytes = struct.pack('BBBBB',self.OpMenuByte, 74,paramCode,channel,value)
        self.serialObject.write(messageBytes)
        if paramCode == 1:
            self.triggerMode[channel] = originalValue

    def syncAllParams(self):
        # First make a list data-type with all param values in an iteration of the loop.
        # Then pack them by data-type and append to string with + operation
        programByteString = struct.pack('BB',self.OpMenuByte, 73)

        # Add 32-bit time params
        programValues = [0]*32; pos = 0
        for i in range(1,5):
            programValues[pos] = self.phase1Duration[i]*self.cycleFrequency; pos+=1
            programValues[pos] = self.interPhaseInterval[i]*self.cycleFrequency; pos+=1
            programValues[pos] = self.phase2Duration[i]*self.cycleFrequency; pos+=1
            programValues[pos] = self.interPulseInterval[i]*self.cycleFrequency; pos+=1
            programValues[pos] = self.burstDuration[i]*self.cycleFrequency; pos+=1
            programValues[pos] = self.interBurstInterval[i]*self.cycleFrequency; pos+=1
            programValues[pos] = self.pulseTrainDuration[i]*self.cycleFrequency; pos+=1
            programValues[pos] = self.pulseTrainDelay[i]*self.cycleFrequency; pos+=1
        # Pack 32-bit times to bytes and append to program byte-string
        programByteString = programByteString + struct.pack('<LLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLL' , *programValues)

        # Add 8-bit params
        programValues = [0]*28; pos = 0
        for i in range(1,5):
            programValues[pos] = self.isBiphasic[i]; pos+=1
            value = math.ceil(((self.phase1Voltage[i]+10)/float(20))*255) # Convert volts to bits
            programValues[pos] = value; pos+=1
            value = math.ceil(((self.phase2Voltage[i]+10)/float(20))*255) # Convert volts to bits
            programValues[pos] = value; pos+=1
            programValues[pos] = self.customTrainID[i]; pos+=1
            programValues[pos] = self.customTrainTarget[i]; pos+=1
            programValues[pos] = self.customTrainLoop[i]; pos+=1
            value = math.ceil(((self.restingVoltage[i]+10)/float(20))*255) # Convert volts to bits
            programValues[pos] = value; pos+=1
        # Pack 8-bit params to bytes and append to program byte-string
        programByteString = programByteString + struct.pack('BBBBBBBBBBBBBBBBBBBBBBBBBBBB', *programValues)

        # Add trigger channel link params
        programValues = [0]*8; pos = 0
        for i in range(1,5):
            programValues[pos] = self.linkTriggerChannel1[i]; pos+=1
        for i in range(1,5):
            programValues[pos] = self.linkTriggerChannel2[i]; pos+=1
        # Pack 8-bit params to bytes and append to program byte-string
        programByteString = programByteString + struct.pack('BBBBBBBB', *programValues)

        # Add trigger mode params
        programByteString = programByteString + struct.pack('BB', self.triggerMode[1], self.triggerMode[2])

        # Send program byte string to PulsePal
        self.serialObject.write(programByteString)

    def sendCustomPulseTrain(self, customTrainID, pulseTimes, pulseVoltages):
        nPulses = len(pulseTimes)
        for i in range(0,nPulses):
            pulseTimes[i] = pulseTimes[i]*self.cycleFrequency # Convert seconds to multiples of minimum cycle (100us)
            pulseVoltages[i] = math.ceil(((pulseVoltages[i]+10)/float(20))*255) # Convert volts to bytes
        if customTrainID == 1:
            messageBytes = struct.pack('BB', self.OpMenuByte , 75) # Op code for programming train 1
        else:
            messageBytes = struct.pack('BB', self.OpMenuByte , 76) # Op code for programming train 2
        messageBytes = messageBytes + struct.pack('<BL', 0, nPulses) # 0 is the USB packet correction byte. See PulsePal wiki
        messageBytes = messageBytes + struct.pack(('<' + 'L'*nPulses), *pulseTimes) # Add pulse times
        messageBytes = messageBytes + struct.pack(('B'*nPulses), *pulseVoltages) # Add pulse times
        self.serialObject.write(messageBytes)

    def sendCustomWaveform(self, customTrainID, pulseWidth, pulseVoltages): # For custom pulse trains with pulse times = pulse width
        nPulses = len(pulseVoltages)
        pulseTimes = [0]*nPulses
        pulseWidth = pulseWidth*self.cycleFrequency # Convert seconds to to multiples of minimum cycle (100us)
        for i in range(0,nPulses):
            pulseTimes[i] = pulseWidth*i # Add consecutive pulse
            pulseVoltages[i] = math.ceil(((pulseVoltages[i]+10)/float(20))*255) # Convert volts to bytes
        if customTrainID == 1:
            messageBytes = struct.pack('BB', self.OpMenuByte , 75) # Op code for programming train 1
        else:
            messageBytes = struct.pack('BB', self.OpMenuByte , 76) # Op code for programming train 2
        messageBytes = messageBytes + struct.pack('<BL', 0, nPulses) # 0 is the USB packet correction byte. See PulsePal wiki
        messageBytes = messageBytes + struct.pack(('<' + 'L'*nPulses), *pulseTimes) # Add pulse times
        messageBytes = messageBytes + struct.pack(('B'*nPulses), *pulseVoltages) # Add pulse times
        self.serialObject.write(messageBytes) # Send custom waveform
    def triggerOutputChannels(self, channel1, channel2, channel3, channel4):
        triggerByte = 0
        triggerByte = triggerByte + (1*channel1)
        triggerByte = triggerByte + (2*channel2)
        triggerByte = triggerByte + (4*channel3)
        triggerByte = triggerByte + (8*channel4)
        messageBytes = struct.pack('BBB',self.OpMenuByte,77,triggerByte)
        self.serialObject.write(messageBytes)
    def abortPulseTrains(self):
        messageBytes = struct.pack('BB',self.OpMenuByte, 80)
        self.serialObject.write(messageBytes)
    def setContinuousLoop(self, channel, state):
        messageBytes = struct.pack('BBBB',self.OpMenuByte, 82, channel, state)
        self.serialObject.write(messageBytes)
    def setFixedVoltage(self, channel, voltage):
        voltage = math.ceil(((voltage+10)/float(20))*255) # Convert volts to bytes
        messageBytes = struct.pack('BBBB',self.OpMenuByte, 79, channel, voltage)
        self.serialObject.write(messageBytes)
    def setDisplay(self, row1String, row2String):
        messageBytes = row1String + chr(254) + row2String
        messageSize = len(messageBytes)
        messageBytes = chr(self.OpMenuByte) + chr(78) + chr(messageSize) + messageBytes
        self.serialObject.write(messageBytes)

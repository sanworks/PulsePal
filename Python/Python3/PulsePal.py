from ArCOM import ArCOMObject
import math

class PulsePalObject(object):
    def __init__(self, PortName):
        self.OpMenuByte = 213
        self.model = 0
        self.dac_bitMax = 0;
        self.Port = ArCOMObject(PortName, 115200)
        # Handshake to confirm connectivity
        self.Port.write((self.OpMenuByte,72), 'uint8')
        Handshake = self.Port.read(1, 'uint8')
        if Handshake != 75:
             raise PulsePalError('Error: incorrect handshake returned.')
        FirmwareVersion = self.Port.read(1, 'uint32')
        if FirmwareVersion < 20:
            self.model = 1;
            self.dac_bitMax = 255;
        else:
            self.model = 2;
            self.dac_bitMax = 65535;
        self.firmwareVersion = FirmwareVersion
        if self.firmwareVersion == 20:
            print("Notice: NOTE: A firmware update is available. It fixes a bug in Pulse Gated trigger mode when used with multiple inputs.")
            print("To update, follow the instructions at https://sites.google.com/site/pulsepalwiki/updating-firmware")
        self.Port.write((self.OpMenuByte,89,80,89,84,72,79,78), 'uint8') # Client name op + 'PYTHON' in ASCII
        self.cycleFrequency = 20000;
        self.isBiphasic = [float('nan'),0, 0, 0, 0];
        self.phase1Voltage = [float('nan'),5, 5, 5, 5];
        self.phase2Voltage = [float('nan'),-5, -5, -5, -5];
        self.restingVoltage = [float('nan'),0, 0, 0, 0];
        self.phase1Duration = [float('nan'),0.001, 0.001, 0.001, 0.001];
        self.interPhaseInterval = [float('nan'),0.001, 0.001, 0.001, 0.001];
        self.phase2Duration = [float('nan'),0.001, 0.001, 0.001, 0.001];
        self.interPulseInterval = [float('nan'),0.01, 0.01, 0.01, 0.01];
        self.burstDuration = [float('nan'),0, 0, 0, 0];
        self.interBurstInterval = [float('nan'),0, 0, 0, 0];
        self.pulseTrainDuration = [float('nan'),1, 1, 1, 1];
        self.pulseTrainDelay = [float('nan'),0, 0, 0, 0];
        self.linkTriggerChannel1 = [float('nan'),1, 1, 1, 1];
        self.linkTriggerChannel2 = [float('nan'),0, 0, 0, 0];
        self.customTrainID = [float('nan'),0, 0, 0, 0];
        self.customTrainTarget = [float('nan'),0, 0, 0, 0];
        self.customTrainLoop = [float('nan'),0, 0, 0, 0];
        self.triggerMode = [float('nan'),0, 0];
        self.outputParameterNames = ['isBiphasic', 'phase1Voltage','phase2Voltage', 'phase1Duration', 'interPhaseInterval',
                               'phase2Duration','interPulseInterval', 'burstDuration', 'interBurstInterval', 'pulseTrainDuration',
                               'pulseTrainDelay', 'linkTriggerChannel1', 'linkTriggerChannel2', 'customTrainID',
                               'customTrainTarget', 'customTrainLoop', 'restingVoltage']
        self.triggerParameterNames = ['triggerMode']
    def setFixedVoltage(self, channel, voltage):
        voltage = math.ceil(((voltage+10)/float(20))*self.dac_bitMax) # Convert volts to bytes
        if self.model == 1:
            self.Port.write((self.OpMenuByte, 79, channel, voltage), 'uint8')
        else:
            self.Port.write((self.OpMenuByte, 79, channel), 'uint8', voltage, 'uint16')
        # Receive acknowledgement
        ok = self.Port.read(1, 'uint8')
        if len(ok) == 0:
            raise PulsePalError('Error: Pulse Pal did not return an acknowledgement byte after a call to setFixedVoltage.')
    def programOutputChannelParam(self, paramName, channel, value):
        originalValue = value
        if isinstance(paramName, str):
            paramCode = self.outputParameterNames.index(paramName)+1
        else:
            paramCode = paramName

        if 2 <= paramCode <= 3 or paramCode == 17:
            value = math.ceil(((value+10)/float(20))*self.dac_bitMax) # Convert volts to bits
            if self.model == 1:
                self.Port.write((self.OpMenuByte,74,paramCode,channel,value),'uint8')
            else:
                self.Port.write((self.OpMenuByte,74,paramCode,channel),'uint8',value,'uint16')
        elif 4 <= paramCode <= 11:
            self.Port.write((self.OpMenuByte,74,paramCode,channel),'uint8',value*self.cycleFrequency,'uint32')
        else:
            self.Port.write((self.OpMenuByte,74,paramCode,channel,value),'uint8')
        # Receive acknowledgement
        ok = self.Port.read(1, 'uint8')
        if len(ok) == 0:
            raise PulsePalError('Error: Pulse Pal did not return an acknowledgement byte after a call to programOutputChannelParam.')
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
        if isinstance(paramName, str):
            paramCode = self.triggerParameterNames.index(paramName)+128
        else:
            paramCode = paramName
        self.Port.write((self.OpMenuByte,74,paramCode,channel,value),'uint8')
        # Receive acknowledgement
        ok = self.Port.read(1, 'uint8')
        if len(ok) == 0:
            raise PulsePalError('Error: Pulse Pal did not return an acknowledgement byte after a call to programTriggerChannelParam.')
        if paramCode == 1:
            self.triggerMode[channel] = originalValue
    def syncAllParams(self):
        # Prepare 32-bit time params
        programValues32 = [0]*32; pos = 0
        for i in range(1,5):
            programValues32[pos] = self.phase1Duration[i]*self.cycleFrequency; pos+=1
            programValues32[pos] = self.interPhaseInterval[i]*self.cycleFrequency; pos+=1
            programValues32[pos] = self.phase2Duration[i]*self.cycleFrequency; pos+=1
            programValues32[pos] = self.interPulseInterval[i]*self.cycleFrequency; pos+=1
            programValues32[pos] = self.burstDuration[i]*self.cycleFrequency; pos+=1
            programValues32[pos] = self.interBurstInterval[i]*self.cycleFrequency; pos+=1
            programValues32[pos] = self.pulseTrainDuration[i]*self.cycleFrequency; pos+=1
            programValues32[pos] = self.pulseTrainDelay[i]*self.cycleFrequency; pos+=1
        
        # Prepare 16-bit voltages
        if self.model == 2:
            programValues16 = [0]*12;
            pos = 0
            for i in range(1,5):
                value = math.ceil(((self.phase1Voltage[i]+10)/float(20))*self.dac_bitMax) # Convert volts to bits
                programValues16[pos] = value; pos+=1
                value = math.ceil(((self.phase2Voltage[i]+10)/float(20))*self.dac_bitMax) # Convert volts to bits
                programValues16[pos] = value; pos+=1
                value = math.ceil(((self.restingVoltage[i]+10)/float(20))*self.dac_bitMax) # Convert volts to bits
                programValues16[pos] = value; pos+=1
        # Prepare 8-bit params
        if self.model == 1:
            programValues8 = [0]*28;
        else:
            programValues8 = [0]*16;
        pos = 0
        for i in range(1,5):
            programValues8[pos] = self.isBiphasic[i]; pos+=1
            if self.model == 1:
                value = math.ceil(((self.phase1Voltage[i]+10)/float(20))*self.dac_bitMax) # Convert volts to bits
                programValues8[pos] = value; pos+=1
                value = math.ceil(((self.phase2Voltage[i]+10)/float(20))*self.dac_bitMax) # Convert volts to bits
                programValues8[pos] = value; pos+=1
            programValues8[pos] = self.customTrainID[i]; pos+=1
            programValues8[pos] = self.customTrainTarget[i]; pos+=1
            programValues8[pos] = self.customTrainLoop[i]; pos+=1
            if self.model == 1:
                value = math.ceil(((self.restingVoltage[i]+10)/float(20))*self.dac_bitMax) # Convert volts to bits
                programValues8[pos] = value; pos+=1

        # Prepare trigger channel link params
        programValuesTL = [0]*8; pos = 0
        for i in range(1,5):
            programValuesTL[pos] = self.linkTriggerChannel1[i]; pos+=1
        for i in range(1,5):
            programValuesTL[pos] = self.linkTriggerChannel2[i]; pos+=1
            
        # Send all params to device
        if self.model == 1:
            self.Port.write(
                (self.OpMenuByte, 73), 'uint8',
                programValues32, 'uint32',
                programValues8, 'uint8',
                programValuesTL, 'uint8',
                self.triggerMode[1:3], 'uint8')
        if self.model == 2:
            self.Port.write(
                (self.OpMenuByte, 73), 'uint8',
                programValues32, 'uint32',
                programValues16, 'uint16',
                programValues8, 'uint8',
                programValuesTL, 'uint8',
                self.triggerMode[1:3], 'uint8')
        # Receive acknowledgement
        ok = self.Port.read(1, 'uint8')
        if len(ok) == 0:
            raise PulsePalError('Error: Pulse Pal did not return an acknowledgement byte after a call to syncAllParams.')
    def sendCustomPulseTrain(self, customTrainID, pulseTimes, pulseVoltages):
        nPulses = len(pulseTimes)
        for i in range(0,nPulses):
            pulseTimes[i] = pulseTimes[i]*self.cycleFrequency # Convert seconds to multiples of minimum cycle (100us)
            pulseVoltages[i] = math.ceil(((pulseVoltages[i]+10)/float(20))*self.dac_bitMax) # Convert volts to bytes
        
        opCode = customTrainID + 74 # 75 if custom train 1, 76 if 2
        if self.model == 1:
            self.Port.write((self.OpMenuByte , opCode, 0), 'uint8', nPulses, 'uint32', pulseTimes, 'uint32', pulseVoltages, 'uint8')
        else:
            self.Port.write((self.OpMenuByte , opCode), 'uint8', nPulses, 'uint32', pulseTimes, 'uint32', pulseVoltages, 'uint16')
        # Receive acknowledgement
        ok = self.Port.read(1, 'uint8')
        if len(ok) == 0:
            raise PulsePalError('Error: Pulse Pal did not return an acknowledgement byte after a call to sendCustomPulseTrain.')
    def sendCustomWaveform(self, customTrainID, pulseWidth, pulseVoltages): # For custom pulse trains with pulse times = pulse width
        nPulses = len(pulseVoltages)
        pulseTimes = [0]*nPulses
        pulseWidth = pulseWidth*self.cycleFrequency # Convert seconds to to multiples of minimum cycle (100us)
        for i in range(0,nPulses):
            pulseTimes[i] = pulseWidth*i # Add consecutive pulse
            pulseVoltages[i] = math.ceil(((pulseVoltages[i]+10)/float(20))*self.dac_bitMax) # Convert volts to bytes
        opCode = customTrainID + 74 # 75 if custom train 1, 76 if 2
        if self.model == 1:
            self.Port.write((self.OpMenuByte , opCode, 0), 'uint8', nPulses, 'uint32', pulseTimes, 'uint32', pulseVoltages, 'uint8')
        else:
            self.Port.write((self.OpMenuByte , opCode), 'uint8', nPulses, 'uint32', pulseTimes, 'uint32', pulseVoltages, 'uint16')
        # Receive acknowledgement
        ok = self.Port.read(1, 'uint8')
        if len(ok) == 0:
            raise PulsePalError('Error: Pulse Pal did not return an acknowledgement byte after a call to sendCustomWaveform.')
    def setContinuousLoop(self, channel, state):
        self.Port.write((self.OpMenuByte, 82, channel, state), 'uint8')
    def triggerOutputChannels(self, channel1, channel2, channel3, channel4):
        triggerByte = (1*channel1) + (2*channel2) + (4*channel3) + (8*channel4)
        self.Port.write((self.OpMenuByte, 77, triggerByte), 'uint8')
    def abortPulseTrains(self):
        self.Port.write((self.OpMenuByte, 80), 'uint8')
    def __del__(self):
        self.Port.write((self.OpMenuByte, 81), 'uint8')
        self.Port.close()
        
class PulsePalError(Exception):
    pass
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
#include <LiquidCrystal.h>
#include <DueTimer.h>
#include <SPI.h>
#include "ArCOM.h"
ArCOM PPUSB(SerialUSB);
LiquidCrystal lcd(10, 9, 8, 7, 6, 5);
SPISettings DACSettings(25000000, MSBFIRST, SPI_MODE2); // Settings for DAC

// Pin declarations
byte SyncPin=44; // AD5724 Pin 7 (Sync)
byte LDACPin=A2; // AD5724 Pin 10 (LDAC)
byte SDChipSelect=14; // microSD CS Pin 
byte triggerPin=12;
byte OutputLEDLines[4] = {A1,A7,A11,A10};
byte TriggerLEDLine = 13;
#define TriggerLevel 0

// Parameters
byte waveType = 1; // 1 = sine, 2 = sawtooth 3 = triange 4 = square 5 = white noise 6 = custom
unsigned short nSamples = 100; // Number of samples to loop (automatically determined)
unsigned long frequency = 500; // Waveform frequency
unsigned short amplitude = 3276; //32768 = max // Waveform peak-to-peak amplitude in DAC bits (16-bit), 20V range
unsigned long waveDuration = 100000;
unsigned short restingVoltage = 32768;

// Waveform variables
unsigned short waveBuffer[40000] = {0};
unsigned short currentSample = 0;
unsigned short samplingRate = 0;
unsigned long nTriggerSamples = 0;
const double twoPi = 6.28318530717958;
double timeStep = 0;
double waveTime = 0;
long randomNum = 0;

// System state variables
byte op = 0;
boolean isPlaying = false;
boolean triggerPinLevel = 1;
boolean lastTriggerPinLevel = 1;
byte playbackMode = 1; // 0 = Continuous, 1 = Triggered, 2 = Hardware-trigger gated
boolean useChannelLEDs = false;
boolean displayParams = true;
boolean softwareTriggered = false;
byte activeOutputCh = 4; // 0-3 to play waveform on ch1-4, 4 for all channels.
unsigned long endTime = 0; // Switch to endTime and make time based on cycles
unsigned long currentTime = 0;

// Other variables
byte dacBuffer[3] = {0};
union {
    byte byteArray[2];
    uint16_t uint16;
} dacValue;
char Value2Display[18] = {' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', '\0'};

void setup(){
  pinMode(SyncPin, OUTPUT); // Configure SPI bus pins as outputs
  pinMode(LDACPin, OUTPUT);
  SPI.begin();
  SPI.beginTransaction(DACSettings);
  digitalWrite(LDACPin, LOW);
  ProgramDAC(12, 0, 4); // Set range to +/- 10V
  ProgramDAC(16, 0, 31); // Power up DACs
  dacBuffer[0] = 4;
  dacValue.uint16 = restingVoltage;
  dacWrite();
  generateWaveform(1, 100, frequency, amplitude);
  lcd.begin(16, 2);
  lcd.clear();
  lcd.home();
  lcd.noDisplay() ;
  delay(100);
  lcd.display();
  playSplashScreen();
  if (displayParams) {
    displayCurrentParams();
  }
  randomSeed(analogRead(4));
  pinMode(SDChipSelect, OUTPUT);
  pinMode(triggerPin, INPUT);
  for (int i = 0; i < 4; i++) {
    pinMode(OutputLEDLines[i], OUTPUT); // Configure channel LED pins as outputs
    digitalWrite(OutputLEDLines[i], LOW); // Initialize channel LEDs to low (off)
  }
  pinMode(TriggerLEDLine, OUTPUT);
  digitalWrite(TriggerLEDLine, LOW);
  Timer3.attachInterrupt(handler);
  Timer3.setFrequency(frequency*nSamples);
  nTriggerSamples = frequency*nSamples*((double)waveDuration/1000000);
  endTime = nTriggerSamples;
  currentTime = 0;
  Timer3.start(); // Calls handler precisely every 20us (50kHz)
}

void loop(){
}

void handler() {
  if (PPUSB.available() > 0) {
    op = PPUSB.readByte();
    switch(op) {
      case 'L':
        nSamples = PPUSB.readUint16();
        PPUSB.readUint16Array(waveBuffer,nSamples);
      break;
      case 'T': // Trigger
        currentSample = 0;
        isPlaying = true;
        triggerLEDs();
        endTime = nTriggerSamples;
        currentTime = 0;
        softwareTriggered = true;
      break;
      case 'A': // Abort
        endPlayback();
      break;
      case 'S': // Settings
        playbackMode = PPUSB.readByte();
        useChannelLEDs = PPUSB.readByte();
        activeOutputCh = PPUSB.readByte();
        if (playbackMode == 0) {
          currentSample = 0;
          isPlaying = true;
          currentTime = 0;
          triggerLEDs();
        } else {
          endPlayback();
        }
        if (useChannelLEDs == 0) {
          killOutputChannelLEDs();
        }
      break;
      case 'P': // Parametric program
        currentSample = 0;
        waveType = PPUSB.readByte();
        amplitude = PPUSB.readUint16();
        frequency = PPUSB.readUint16();
        waveDuration = PPUSB.readUint32();
        if (frequency > 20) {
          nSamples = (1/(double)frequency)*60000;
        } else {
          nSamples = (1/(double)frequency)*30000;
        }
        if (nSamples % 2 != 0) { // Force nSamples to be even
          nSamples++;
        }
        generateWaveform(waveType, nSamples, frequency, amplitude);
        Timer3.stop();
        if (waveType == 5) {
          samplingRate = 50000;
        } else {
          samplingRate = frequency*nSamples;
        }
        if (displayParams) {
          displayCurrentParams();
        }
        Timer3.setFrequency(samplingRate);
        nTriggerSamples = samplingRate*((double)waveDuration/1000000);
        Timer3.start();
      break;
      case 'C': // Custom waveform and sampling rate
        samplingRate = PPUSB.readUint16();
        nSamples = PPUSB.readUint16();
        PPUSB.readUint16Array(waveBuffer, nSamples);
        nTriggerSamples = nSamples;
        waveType = 6;
        Timer3.stop();
        if (displayParams) {
          displayCurrentParams();
        }
        Timer3.setFrequency(samplingRate);
        Timer3.start();
      break;
      case 'F': // Set custom waveform sampling frequency
        samplingRate = PPUSB.readUint16();
        if (waveType == 6) {
          Timer3.stop();
          if (displayParams) {
            displayCurrentParams();
          }
          Timer3.setFrequency(samplingRate);
          Timer3.start();
        }
      break;
    }
  }
  if (isPlaying) {
    currentTime++;
    if (playbackMode > 0) {
      if (currentTime == endTime) {
        if ((playbackMode == 1) || (playbackMode == 2) && softwareTriggered)  {
          endPlayback();
          softwareTriggered = false;
        }
      }
    }
    if (isPlaying) {
      if (waveType == 5) {
        dacValue.uint16 = random(amplitude) + (restingVoltage - (amplitude/2));
        dacWrite();
      } else {
        if (currentSample == nSamples) {
          currentSample = 0;
        }
        dacValue.uint16 = waveBuffer[currentSample];
        dacWrite();
        currentSample++;
      }
    }
  }
  if (playbackMode > 0) {
    triggerPinLevel = digitalReadDirect(triggerPin);
    if (triggerPinLevel == 0) {
      if (lastTriggerPinLevel == 1){
        if (useChannelLEDs) {
          digitalWriteDirect(TriggerLEDLine, HIGH);
        }
        currentSample = 0;
        isPlaying = true;
        endTime = nTriggerSamples;
        currentTime = 0;
        triggerLEDs();
      }
    } else {
      if (lastTriggerPinLevel == 0){
        if (playbackMode == 2) {
            endPlayback();
        }
        if (useChannelLEDs) {
          digitalWriteDirect(TriggerLEDLine, LOW);
        }
      }
    }
    lastTriggerPinLevel = triggerPinLevel;
  }
}
void ProgramDAC(byte Data1, byte Data2, byte Data3) {
  digitalWriteDirect(LDACPin,HIGH);
  digitalWriteDirect(SyncPin,LOW);
  SPI.transfer (Data1);
  SPI.transfer (Data2);
  SPI.transfer (Data3);
  digitalWriteDirect(SyncPin,HIGH);
  digitalWriteDirect(LDACPin,LOW);
}
void dacWrite() {
  digitalWriteDirect(LDACPin,HIGH);
  digitalWriteDirect(SyncPin,LOW); 
  dacBuffer[0] = activeOutputCh;
  dacBuffer[1] = dacValue.byteArray[1];
  dacBuffer[2] = dacValue.byteArray[0];
  SPI.transfer(dacBuffer,3);
  digitalWriteDirect(SyncPin,HIGH);
  digitalWriteDirect(LDACPin,LOW);
}
void digitalWriteDirect(int pin, boolean val){
  if(val) g_APinDescription[pin].pPort -> PIO_SODR = g_APinDescription[pin].ulPin;
  else    g_APinDescription[pin].pPort -> PIO_CODR = g_APinDescription[pin].ulPin;
}

byte digitalReadDirect(int pin){
  return !!(g_APinDescription[pin].pPort -> PIO_PDSR & g_APinDescription[pin].ulPin);
}

void write2Screen(const char* Line1, const char* Line2) {
  lcd.clear(); lcd.home(); lcd.print(Line1); lcd.setCursor(0, 1); lcd.print(Line2);
}
void write2Screen2(const char* Line2) {
  lcd.home(); lcd.setCursor(0, 1); lcd.print(Line2);
}

void generateWaveform(byte wavType, unsigned short nSamp, unsigned short freq, unsigned short amp) { 
  waveTime = 0;
  timeStep = twoPi/(double)nSamp;
  double halfAmp = round((double)amp/2);
  unsigned short halftime = nSamp/2;
  unsigned short LowVoltage = restingVoltage - halfAmp;
  unsigned short HighVoltage = restingVoltage + halfAmp;
  double voltStep = 0;
  double volts = (double)restingVoltage - halfAmp;
  switch (wavType) {
    case 1: // Sine
      for (int i = 0; i < nSamp; i++) {
        waveBuffer[i] = round((sin(waveTime)*halfAmp)+(double)restingVoltage);
        waveTime += timeStep;
      }
    break;
    case 2: // Sawtooth
      voltStep = (double)amp/(double)nSamp;
      for (int i = 0; i < nSamp; i++) {
        waveBuffer[i] = (unsigned short)volts;
        volts += voltStep;
      }
    break;
    case 3: // Triangle
      voltStep = (double)amp*2/(double)nSamp;
      for (int i = 0; i < halftime; i++) {
        waveBuffer[i] = (unsigned short)volts;
        volts += voltStep;
      }
      for (int i = halftime; i < nSamp; i++) {
        volts -= voltStep;
        waveBuffer[i] = (unsigned short)volts;
      }
    break;
    case 4: // Square
      for (int i = 0; i < nSamp; i++) {
        if (i < halftime) {
          waveBuffer[i] = HighVoltage;
        } else {
          waveBuffer[i] = LowVoltage;
        }
      }
    break;
  }
}

void triggerLEDs() {
  if (useChannelLEDs) {
    if (activeOutputCh == 4) {
      for (int i = 0; i < 4; i++) {
        digitalWriteDirect(OutputLEDLines[i], HIGH); // Initialize channel LEDs to low (off)
      }
    } else {
        digitalWriteDirect(OutputLEDLines[activeOutputCh], HIGH);
    }
  }
}

void endPlayback() {
  isPlaying = false;
  dacValue.uint16 = restingVoltage;
  dacWrite();
  killOutputChannelLEDs();
}

void killOutputChannelLEDs() {
  for (int i = 0; i < 4; i++) {
    digitalWriteDirect(OutputLEDLines[i], LOW); 
  }
}

void playSplashScreen() {
  byte splashFrameDelay = 75;
  write2Screen("   Pulse Pal","W A V  E  G E N");
  delay(splashFrameDelay);
  write2Screen2(" W A V E G E N  ");
  delay(splashFrameDelay);
  write2Screen2("  W A VEG E N   ");
  delay(splashFrameDelay);
  write2Screen2("   W AVEGE N    ");
  splashFrameDelay = 20;
  for (int i = 0; i < 4; i++) {
    delay(splashFrameDelay);
    write2Screen2("    WAVEGEN     ");
    delay(splashFrameDelay);
    write2Screen2("   -WAVEGEN-    ");
    delay(splashFrameDelay);
    write2Screen2("  --WAVEGEN--   ");
    delay(splashFrameDelay);
    write2Screen2(" ---WAVEGEN---  ");
    delay(splashFrameDelay);
    write2Screen2("----WAVEGEN---- ");
    delay(splashFrameDelay);
    write2Screen2("---=WAVEGEN=--- ");
    delay(splashFrameDelay);
    write2Screen2("--==WAVEGEN==-- ");
    delay(splashFrameDelay);
    write2Screen2("-===WAVEGEN===- ");
    delay(splashFrameDelay);
    write2Screen2("====WAVEGEN==== ");
    delay(splashFrameDelay);
    write2Screen2("-===WAVEGEN===- ");
    delay(splashFrameDelay);
    write2Screen2("--==WAVEGEN==-- ");
    delay(splashFrameDelay);
    write2Screen2("---=WAVEGEN=--- ");
    delay(splashFrameDelay);
    write2Screen2("----WAVEGEN---- ");
    delay(splashFrameDelay);
    write2Screen2(" ---WAVEGEN---  ");
    delay(splashFrameDelay);
    write2Screen2("  --WAVEGEN--   ");
    delay(splashFrameDelay);
    write2Screen2("   -WAVEGEN-    ");
  }
  delay(splashFrameDelay);
  write2Screen2("    WAVEGEN     ");
  delay(1000);
}

void displayCurrentParams() {
  if (waveType < 6) {
    double floatAmplitude = (double)amplitude*10/32768;
    if (frequency < 10000) {
      if (floatAmplitude < 9.999) {
        sprintf (Value2Display, "F:%dHz A:%1.2fV", frequency, floatAmplitude);
      } else {
        sprintf (Value2Display, "F:%dHz A:%1.1fV", frequency, floatAmplitude);
      }
    } else {
      sprintf (Value2Display, "%gkHz/%1.2fV", (double)frequency/1000, floatAmplitude);
    }
  } else {
    sprintf (Value2Display, "SF: %dHz", samplingRate);
  }
  switch (waveType) {
    case 1: // Sine 
      write2Screen("Waveform: Sine",Value2Display);
    break;
    case 2: // Sawtooth 
      write2Screen("Waveform: Saw",Value2Display);
    break;
    case 3: // Triangle 
      write2Screen("Wave: Triangle",Value2Display);
    break;
    case 4: // Square 
      write2Screen("Waveform: Square",Value2Display);
    break;
    case 5: // Noise 
      write2Screen("Waveform: Noise",Value2Display);
    break;
    case 6: // Custom 
      write2Screen("Waveform: Custom",Value2Display);
    break;
  }  
}


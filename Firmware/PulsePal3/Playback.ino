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


// Pulse train playback. handler() is the hardware timer callback that plays pulse trains on all output
// channels (see the description above it). The other functions support it.
//
// Functions in this file:
//   TC3_Handler() (HW2 only)
//   mirrorAboutZero()
//   handler()
//   killChannel()
//   AbortAllPulseTrains()
//   updateUsesBursts()

#if (HARDWARE_VERSION == 2)
  // Interrupt service routine for timer counter TC3, configured in startHardwareTimer(). On Pulse Pal 3, IntervalTimer calls handler() directly.
  void TC3_Handler(void) {
    TC_GetStatus(TC1, 0); // Read the status register to clear the interrupt
    handler();
  }
#endif

// Returns the DAC code for the same voltage with the opposite sign. Used for phase 2 of biphasic custom pulses.
static inline uint16_t mirrorAboutZero(uint16_t dacCode) {
  if (dacCode < 32768) {
    uint32_t mirrored = 32768 + (32768 - (uint32_t)dacCode);
    if (mirrored > 65535) {
      return 65535; // -10V mirrors to +10V, the top of the DAC range
    }
    return (uint16_t)mirrored;
  } else {
    return 32768 - (dacCode - 32768);
  }
}

// ---------------------------------------------------------------------------------------------------------------
// handler() is the hardware timer callback, and it does all pulse train playback. It runs every TIMER_PERIOD
// microseconds (50us). Time is counted in timer cycles: SystemTime is the number of cycles since playback started,
// and all durations and custom pulse times are also in cycles (20000 cycles = 1 second).
//
// Each cycle:
//  1. If any channel is playing: write DAC updates requested on the previous cycle, and abort all playback if the
//     joystick button is pressed. The final DAC update after playback ends is written on the next idle cycle.
//  2. Soft triggers scheduled from loop() (op 77 or joystick menu) become active.
//  3. Read the trigger lines, update their LEDs and detect transitions (LineTriggerEvent). On Pulse Pal 3, a rising
//     edge on a trigger channel in param sync mode takes the parameter set op 92 left in paramBuffer. Output
//     channels that are idle take it at the edge; channels playing a pulse train finish that train on the
//     parameters it started with, and take the new ones in the cycle it ends. Both happen before step 4, so an edge
//     on the other trigger channel in the same cycle starts its trains with the new parameters, and a channel
//     triggered after its train ended plays the whole of the next train with them. A param sync channel starts and
//     stops nothing itself.
//  4. For each output channel that is playing: stop it if a linked trigger channel in toggle mode went low to high,
//     or a linked trigger channel in gated mode went high to low (unless the other trigger channel is also linked,
//     gated and still high). For each channel that is not playing: start it if a linked trigger went low to high,
//     or a soft trigger arrived. Triggers in normal mode are ignored while a channel is playing.
//  5. For each output channel, advance its state machine. Transitions occur when SystemTime equals
//     NextPulseTransitionTime or NextBurstTransitionTime.
//       PreStimulusStatus = 1   Waiting for PulseTrainDelay to elapse, then StimulusStatus = 1
//       StimulusStatus = 1      Playing the pulse train
//         BurstStatus           1 during a burst, 0 between bursts (always 1 if UsesBursts is false)
//           PulseStatus         PULSE_IDLE -> PULSE_PHASE1 -> [PULSE_INTER_PHASE] -> [PULSE_PHASE2] -> PULSE_IDLE
//                               (bracketed phases are for biphasic pulses only)
//     Parametric trains and looping custom trains stop at PulseTrainEndTime, unless ContinuousLoopMode is set.
//     Non-looping custom trains stop after their last pulse.
//     New voltages are stored with setDAC(), and written to the DAC at the start of the next cycle.
//     All DAC writes after setup() happen here, including values set with setDAC() from loop() while idle.
//
// Custom trains (CustomTrainID > 0) use CustomPulseTimes (relative to PulseTrainTimestamps) and CustomVoltages
// in place of the parametric pulse timing and phase 1 voltage. CustomPulseTimeIndex tracks the current pulse.
// If CustomTrainTarget is 0 each time starts a pulse; if 1 each time starts a burst of parametric pulses.
// Phase 2 of a biphasic custom pulse is the phase 1 voltage mirrored about 0V.
//
// StimulatingState is 0 when no channel is playing, 2 on the cycle playback starts from idle, and 1 otherwise.
// loop() only runs the joystick menu while StimulatingState is 0.
// ---------------------------------------------------------------------------------------------------------------
void handler(void) {
  if (StimulatingState == 0) {
      if ((LastStimulatingState == 1) || (DACFlag == 1)) { // The cycle on which all pulse trains have finished, or a DAC update was requested from loop()
        dacWrite(); // Update DAC to final voltages (should be resting voltage), or to values set with setDAC()
        DACFlag = 0;
      }
      SystemTime = 0;
   } else {
  //     if (StimulatingState == 2) {
  //        // Place to include custom code that executes on the first cycle of a pulse train
  //     }
       StimulatingState = 1;
       if (DACFlag == 1) { // A DAC update was requested
         dacWrite(); // Update DAC
         DACFlag = 0;
       }
       SystemTime++; // Increment system time (# of hardware timer cycles since stim start)
       ClickerButtonState = digitalReadDirect(ClickerButtonLine); // Read the joystick button
       if (ClickerButtonState == 0){ // A button click (pulls line to ground, = logic 0) and ends ongoing stimulation on all channels.
         AbortAllPulseTrains();
       }
    }
    for (int i = 0; i<4; i++) {
      if(SoftTriggerScheduled[i]) { // Soft triggers are "scheduled" to be handled on the next cycle, since the serial read took too much time.
        SoftTriggered[i] = 1;
        SoftTriggerScheduled[i] = 0;
      }
    }
    LastStimulatingState = StimulatingState;

    // Read values of trigger pins
    LineTriggerEvent[0] = TRIGGER_EVENT_NONE; LineTriggerEvent[1] = TRIGGER_EVENT_NONE;
    for (int x = 0; x < 2; x++) {
         InputValues[x] = digitalReadDirect(TriggerLines[x]);
         if (InputValues[x] == TriggerLevel) {
           digitalWriteDirect(InputLEDLines[x], HIGH);
         } else {
           digitalWriteDirect(InputLEDLines[x], LOW);
         }
         // update LineTriggerEvent with logic representing logic transition
         if ((InputValues[x] == TriggerLevel) && (InputValuesLastCycle[x] == DefaultInputLevel)) {
           LineTriggerEvent[x] = TRIGGER_EVENT_LOW_TO_HIGH; // Low to high transition
         } else if ((InputValues[x] == DefaultInputLevel) && (InputValuesLastCycle[x] == TriggerLevel)) {
           LineTriggerEvent[x] = TRIGGER_EVENT_HIGH_TO_LOW; // High to low transition
         }
         InputValuesLastCycle[x] = InputValues[x];
    }

    #if (HARDWARE_VERSION > 2)
      // Take the parameter set that op 92 left waiting, if a trigger channel in param sync mode just went high
      if (paramSyncPending) {
        for (int y = 0; y < 2; y++) {
          if ((TriggerMode[y] == TRIGGER_MODE_PARAM_SYNC) && (LineTriggerEvent[y] == TRIGGER_EVENT_LOW_TO_HIGH)) {
            paramSyncPending = false;
            startParamSync();
            break; // The set may have changed TriggerMode, so this cycle starts at most one param sync
          }
        }
      }
      // Channels that were playing at the edge take their new parameters here, once their pulse train has ended.
      // This runs before the trigger responses below, so a channel triggered after its train ended plays the whole
      // of the next train with the new parameters.
      if (paramSyncChannelsWaiting) {
        loadWaitingParamSyncChannels();
      }
    #endif

    for (int x = 0; x < 4; x++) {
      byte KillChannel = 0;
       // If trigger channels are in toggle mode and a trigger arrived, or in gated mode and line is low, shut down any governed channels that are playing a pulse train
       if (((StimulusStatus[x] == 1) || (PreStimulusStatus[x] == 1))) {
          for (int y = 0; y < 2; y++) {
            if (TriggerAddress[y][x]) {
                if ((TriggerMode[y] == TRIGGER_MODE_TOGGLE) && (LineTriggerEvent[y] == TRIGGER_EVENT_LOW_TO_HIGH)) {
                     KillChannel = 1;
                }
                if ((TriggerMode[y] == TRIGGER_MODE_GATED) && (LineTriggerEvent[y] == TRIGGER_EVENT_HIGH_TO_LOW)) {
                    if ((TriggerMode[1-y] == TRIGGER_MODE_GATED) && (TriggerAddress[1-y][x])) {
                      if (InputValues[1-y] == DefaultInputLevel) {
                        KillChannel = 1;
                      }
                    } else {
                      KillChannel = 1;
                    }
                }
            }
          }   
          if (KillChannel) {
             killChannel(x);
          }
        
      } else {
       // Adjust StimulusStatus to reflect any new trigger events
       if ((TriggerAddress[0][x] && (LineTriggerEvent[0] == TRIGGER_EVENT_LOW_TO_HIGH)
            #if (HARDWARE_VERSION > 2)
              && (TriggerMode[0] != TRIGGER_MODE_PARAM_SYNC) // A param sync channel loads parameters and starts nothing
            #endif
            ) || SoftTriggered[x]) {
         if (StimulatingState == 0) {SystemTime = 0; StimulatingState = 2;}
         PreStimulusStatus[x] = 1; BurstStatus[x] = 1; PrePulseTrainTimestamps[x] = SystemTime; PulseStatus[x] = PULSE_IDLE; 
         SoftTriggered[x] = 0;
       }
       if (TriggerAddress[1][x] && (LineTriggerEvent[1] == TRIGGER_EVENT_LOW_TO_HIGH)
           #if (HARDWARE_VERSION > 2)
             && (TriggerMode[1] != TRIGGER_MODE_PARAM_SYNC) // A param sync channel loads parameters and starts nothing
           #endif
           ) {
         if (StimulatingState == 0) {SystemTime = 0; StimulatingState = 2;}
         PreStimulusStatus[x] = 1; BurstStatus[x] = 1; PrePulseTrainTimestamps[x] = SystemTime; PulseStatus[x] = PULSE_IDLE;
       }
      }
    }
    if (StimulatingState != 2) {
     StimulatingState = 0; // null condition, will be overridden in loop if any channels are still stimulating.
    }
    
    // Check clock and adjust line levels for new time as per programming
    for (int x = 0; x < 4; x++) {
      byte thisTrainID = CustomTrainID[x];
      byte thisTrainIDIndex = thisTrainID-1;
      if (PreStimulusStatus[x] == 1) {
          if (StimulatingState != 2) {
           StimulatingState = 1;
          }
        if (SystemTime == (PrePulseTrainTimestamps[x] + PulseTrainDelay[x])) {
          PreStimulusStatus[x] = 0;
          StimulusStatus[x] = 1;
          PulseStatus[x] = PULSE_IDLE;
          PulseTrainTimestamps[x] = SystemTime;
          PulseTrainEndTime[x] = SystemTime + PulseTrainDuration[x];
          if (CustomTrainTarget[x] == 1)  {
            if (CustomTrainID[x] > 0) {
              NextBurstTransitionTime[x] = SystemTime + CustomPulseTimes[thisTrainIDIndex][0];
            } else {
              NextBurstTransitionTime[x] = SystemTime + CustomPulseTimes[1][0]; // Legacy behavior: burst target with no custom train selected
            }
            BurstStatus[x] = 0;
          } else {
            NextBurstTransitionTime[x] = SystemTime+BurstDuration[x];
          }
          if (CustomTrainID[x] == 0) {
            NextPulseTransitionTime[x] = SystemTime;
            setDAC(x, Phase1Voltage[x]);
          } else {
            NextPulseTransitionTime[x] = SystemTime + CustomPulseTimes[thisTrainIDIndex][0]; 
            CustomPulseTimeIndex[x] = 0;
          }
        }
      }
      if (StimulusStatus[x] == 1) { // if this output line has been triggered and is delivering a pulse train
          if (StimulatingState != 2) {
           StimulatingState = 1; 
          }
        if (BurstStatus[x] == 1) { // if this output line is currently gated "on"
          switch (PulseStatus[x]) { // depending on the phase of the pulse
           case PULSE_IDLE: { // if this is the inter-pulse interval
            // determine if the next pulse should start now
            if ((CustomTrainID[x] == 0) || ((CustomTrainID[x] > 0) && (CustomTrainTarget[x] == 1))) {
              if (SystemTime == NextPulseTransitionTime[x]) {
                NextPulseTransitionTime[x] = SystemTime + Phase1Duration[x];
                    if (!((UsesBursts[x] == 1) && (NextPulseTransitionTime[x] >= NextBurstTransitionTime[x]))){ // so that it doesn't start a pulse it can't finish due to burst end
                      PulseStatus[x] = PULSE_PHASE1;
                      digitalWriteDirect(OutputLEDLines[x], HIGH);
                      if ((CustomTrainID[x] > 0) && (CustomTrainTarget[x] == 1)) {
                        setDAC(x, CustomVoltages[thisTrainIDIndex][CustomPulseTimeIndex[x]]);
                      } else {
                        setDAC(x, Phase1Voltage[x]);
                      }
                    }
                 }
              } else {
               if (SystemTime == NextPulseTransitionTime[x]) {
                     int SkipNextInterval = 0;
                     if ((CustomTrainLoop[x] == 1) && (CustomPulseTimeIndex[x] == CustomTrainNpulses[thisTrainIDIndex])) {
                            CustomPulseTimeIndex[x] = 0;
                            PulseTrainTimestamps[x] = SystemTime;
                     }
                     if (CustomPulseTimeIndex[x] < CustomTrainNpulses[thisTrainIDIndex]) {
                       if ((CustomPulseTimes[thisTrainIDIndex][CustomPulseTimeIndex[x]+1] - CustomPulseTimes[thisTrainIDIndex][CustomPulseTimeIndex[x]]) > Phase1Duration[x]) {
                         NextPulseTransitionTime[x] = SystemTime + Phase1Duration[x];
                       } else {
                         NextPulseTransitionTime[x] = PulseTrainTimestamps[x] + CustomPulseTimes[thisTrainIDIndex][CustomPulseTimeIndex[x]+1];  
                         SkipNextInterval = 1;
                       }
                     }
                     if (SkipNextInterval == 0) {
                        PulseStatus[x] = PULSE_PHASE1;
                     }
                     setDAC(x, CustomVoltages[thisTrainIDIndex][CustomPulseTimeIndex[x]]);
                     digitalWriteDirect(OutputLEDLines[x], HIGH);
                     if (IsBiphasic[x] == 0) {
                        CustomPulseTimeIndex[x] = CustomPulseTimeIndex[x] + 1;
                     }
                     if (CustomPulseTimeIndex[x] > (CustomTrainNpulses[thisTrainIDIndex])){
                       CustomPulseTimeIndex[x] = 0;
                       if (CustomTrainLoop[x] == 0) {
                         killChannel(x);
                       }
                     }
                  }
              } 
            } break;
            
            case PULSE_PHASE1: { // if this is the first phase of the pulse
             // determine if this phase should end now
             if (SystemTime == NextPulseTransitionTime[x]) {
                if (IsBiphasic[x] == 0) {
                  if (CustomTrainID[x] == 0) {
                      NextPulseTransitionTime[x] = SystemTime + InterPulseInterval[x];
                      PulseStatus[x] = PULSE_IDLE;
                      digitalWriteDirect(OutputLEDLines[x], LOW);
                      setDAC(x, RestingVoltage[x]);
                  } else {
                    if (CustomTrainTarget[x] == 0) {
                      NextPulseTransitionTime[x] = PulseTrainTimestamps[x] + CustomPulseTimes[thisTrainIDIndex][CustomPulseTimeIndex[x]];
                    } else {
                      NextPulseTransitionTime[x] = SystemTime + InterPulseInterval[x];
                    }
                    if (CustomPulseTimeIndex[x] == CustomTrainNpulses[thisTrainIDIndex]) {
                      if (CustomTrainLoop[x] == 1) {
                              CustomPulseTimeIndex[x] = 0;
                              PulseTrainTimestamps[x] = SystemTime;
                              setDAC(x, CustomVoltages[thisTrainIDIndex][CustomPulseTimeIndex[x]]);
                              if ((CustomPulseTimes[thisTrainIDIndex][CustomPulseTimeIndex[x]+1] - CustomPulseTimes[thisTrainIDIndex][CustomPulseTimeIndex[x]]) > Phase1Duration[x]) {
                                PulseStatus[x] = PULSE_PHASE1;
                              } else {
                                PulseStatus[x] = PULSE_IDLE;
                              }
                              NextPulseTransitionTime[x] = PulseTrainTimestamps[x] + Phase1Duration[x];
                              CustomPulseTimeIndex[x] = CustomPulseTimeIndex[x] + 1;
                      } else {
                        killChannel(x);
                      }
                    } else {
                      PulseStatus[x] = PULSE_IDLE;
                      digitalWriteDirect(OutputLEDLines[x], LOW);
                      setDAC(x, RestingVoltage[x]);
                    }
                  }
     
                } else {
                  if (InterPhaseInterval[x] == 0) {
                    NextPulseTransitionTime[x] = SystemTime + Phase2Duration[x];
                    PulseStatus[x] = PULSE_PHASE2;
                    if (CustomTrainID[x] == 0) {
                      setDAC(x, Phase2Voltage[x]);
                    } else {
                      
                       setDAC(x, mirrorAboutZero(CustomVoltages[thisTrainIDIndex][CustomPulseTimeIndex[x]]));
                       if (CustomTrainTarget[x] == 0) {
                           CustomPulseTimeIndex[x] = CustomPulseTimeIndex[x] + 1;
                       }
                    } 
                  } else {
                    NextPulseTransitionTime[x] = SystemTime + InterPhaseInterval[x];
                    PulseStatus[x] = PULSE_INTER_PHASE;
                    setDAC(x, RestingVoltage[x]);
                  }
                }
              }
            } break;
            case PULSE_INTER_PHASE: {
               if (SystemTime == NextPulseTransitionTime[x]) {
                 NextPulseTransitionTime[x] = SystemTime + Phase2Duration[x];
                 PulseStatus[x] = PULSE_PHASE2;
                 if (CustomTrainID[x] == 0) {
                   setDAC(x, Phase2Voltage[x]);  
                 } else {
                   setDAC(x, mirrorAboutZero(CustomVoltages[thisTrainIDIndex][CustomPulseTimeIndex[x]]));
                   if (CustomTrainTarget[x] == 0) {
                       CustomPulseTimeIndex[x] = CustomPulseTimeIndex[x] + 1;
                   }
                 }
               }
            } break;
            case PULSE_PHASE2: {
              if (SystemTime == NextPulseTransitionTime[x]) {
                  if (CustomTrainID[x] == 0) {
                      NextPulseTransitionTime[x] = SystemTime + InterPulseInterval[x];
                  } else {
                    if (CustomTrainTarget[x] == 0) {
                      NextPulseTransitionTime[x] = PulseTrainTimestamps[x] + CustomPulseTimes[thisTrainIDIndex][CustomPulseTimeIndex[x]];
                      if (CustomPulseTimeIndex[x] == (CustomTrainNpulses[thisTrainIDIndex])){
                          killChannel(x);
                     }
                    } else {
                      NextPulseTransitionTime[x] = SystemTime + InterPulseInterval[x];
                    }
                  }
                 if (!((CustomTrainID[x] == 0) && (InterPulseInterval[x] == 0))) { 
                   PulseStatus[x] = PULSE_IDLE;
                   digitalWriteDirect(OutputLEDLines[x], LOW);
                   setDAC(x, RestingVoltage[x]);
                 } else {
                   PulseStatus[x] = PULSE_PHASE1;
                   NextPulseTransitionTime[x] = (NextPulseTransitionTime[x] - InterPulseInterval[x]) + (Phase1Duration[x]);
                   setDAC(x, Phase1Voltage[x]);
                 }
               }
            } break;
            
          }
        }
          // Determine if burst status should go to 0 now
       if (UsesBursts[x] == true) {
        if (SystemTime == NextBurstTransitionTime[x]) {
          if (BurstStatus[x] == 1) {
            if (CustomTrainID[x] == 0) {
                     NextPulseTransitionTime[x] = SystemTime + BurstInterval[x];
                     NextBurstTransitionTime[x] = SystemTime + BurstInterval[x];              
            } else if (CustomTrainTarget[x] == 1) {
              CustomPulseTimeIndex[x] = CustomPulseTimeIndex[x] + 1;
              if (CustomPulseTimeIndex[x] == (CustomTrainNpulses[thisTrainIDIndex])){
                  killChannel(x);
              }
              NextPulseTransitionTime[x] = PulseTrainTimestamps[x] + CustomPulseTimes[thisTrainIDIndex][CustomPulseTimeIndex[x]];
              NextBurstTransitionTime[x] = NextPulseTransitionTime[x];
            }
              BurstStatus[x] = 0;
              setDAC(x, RestingVoltage[x]);
          } else {
          // Determine if burst status should go to 1 now
            NextBurstTransitionTime[x] = SystemTime + BurstDuration[x];
            NextPulseTransitionTime[x] = SystemTime + Phase1Duration[x];
            PulseStatus[x] = PULSE_PHASE1;
            if ((CustomTrainID[x] > 0) && (CustomTrainTarget[x] == 1)) {
              if (CustomPulseTimeIndex[x] < CustomTrainNpulses[thisTrainIDIndex]){
                  setDAC(x, CustomVoltages[thisTrainIDIndex][CustomPulseTimeIndex[x]]);
              }
            } else {
                 setDAC(x, Phase1Voltage[x]);
            }
            BurstStatus[x] = 1;
         }
        }
       } 
        // Determine if Stimulus Status should go to 0 now
        if ((SystemTime == PulseTrainEndTime[x]) && (StimulusStatus[x] == 1)) {
          if (((CustomTrainID[x] > 0) && (CustomTrainLoop[x] == 1)) || (CustomTrainID[x] == 0)) {
            if (ContinuousLoopMode[x] == false) {
                killChannel(x);
            }
          }
        }
     }
   }
}
// End hw timer callback

void killChannel(byte outputChannel) {
  if (PulseTrainDuration_ExamplePulse[outputChannel] > 0) { // Restore the duration saved by the single pulse menu option
    PulseTrainDuration[outputChannel] = PulseTrainDuration_ExamplePulse[outputChannel];
    PulseTrainDuration_ExamplePulse[outputChannel] = 0;
  }
  CustomPulseTimeIndex[outputChannel] = 0;
  PreStimulusStatus[outputChannel] = 0;
  StimulusStatus[outputChannel] = 0;
  PulseStatus[outputChannel] = PULSE_IDLE;
  BurstStatus[outputChannel] = 0;
  setDAC(outputChannel, RestingVoltage[outputChannel]);
  digitalWriteDirect(OutputLEDLines[outputChannel], LOW);
}

// Stops playback on all output channels. Called from handler() when the joystick button is pressed.
// The screen message is shown by loop(), because writing to the screen from the timer interrupt could interrupt
// a screen write already in progress in loop(), leaving both stuck (see the note above dacWrite()).
void AbortAllPulseTrains() {
    for (int x = 0; x < 4; x++) {
      killChannel(x);
    }
    dacWrite();
    abortRequested = true;
}

// Sets UsesBursts for an output channel (0-3) from its parameters. Call after any output channel parameter changes.
void updateUsesBursts(byte channel) {
  if ((BurstDuration[channel] == 0) || (BurstInterval[channel] == 0)) {UsesBursts[channel] = false;} else {UsesBursts[channel] = true;}
  if (CustomTrainTarget[channel] == 1) {UsesBursts[channel] = true;}
  if ((CustomTrainID[channel] > 0) && (CustomTrainTarget[channel] == 0)) {UsesBursts[channel] = false;}
}

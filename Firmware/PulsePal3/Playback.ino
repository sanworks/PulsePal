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
//   timeReached()
//   mirrorAboutZero()
//   pulseFitsInBurst()
//   pulseFitsInTrain()
//   startCustomPulse()
//   endCustomTrainPass()
//   handler()
//   killChannel()
//   AbortAllPulseTrains()
//   updateUsesBursts()
//   stopCustomTrainChannels()

#if (HARDWARE_VERSION == 2)
  // Interrupt service routine for timer counter TC3, configured in startHardwareTimer(). On Pulse Pal 3, IntervalTimer calls handler() directly.
  void TC3_Handler(void) {
    TC_GetStatus(TC1, 0); // Read the status register to clear the interrupt
    handler();
  }
#endif

// True once SystemTime has reached a time: the time is now, or has passed. handler() schedules each transition at a time,
// and makes it in the first cycle at or after that time. It used to wait for SystemTime to equal the time, so a time
// scheduled in the past (a biphasic custom pulse longer than the gap to the next one, a custom burst longer than the
// gap between burst onsets, a delay shortened during the delay...) was never met: the channel stayed "playing", at a
// pulse's voltage or at rest, and ignored triggers until it was stopped, or until SystemTime wrapped around 59.6 hours
// later. The subtraction works across that wrap, for times up to 2^31 cycles (29.8 hours) apart.
static inline bool timeReached(uint32_t time) {
  return (int32_t)(SystemTime - time) >= 0;
}

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

// True if a pulse starting on an output channel (0-3) now would end before its burst does. The end of a burst sets the
// resting voltage whatever phase is playing, so a biphasic pulse must fit whole: cut short, it loses part or all of
// phase 2 and is no longer charge balanced. A monophasic pulse needs only its phase 1 to fit.
static inline bool pulseFitsInBurst(byte channel) {
  uint32_t pulseEnd = SystemTime + Phase1Duration[channel];
  if (IsBiphasic[channel]) {
    pulseEnd += InterPhaseInterval[channel] + Phase2Duration[channel];
  }
  return (int32_t)(NextBurstTransitionTime[channel] - pulseEnd) > 0; // pulseEnd < NextBurstTransitionTime, across SystemTime's wrap
}

// True if a pulse starting on an output channel (0-3) now would end by the end of its pulse train. The end of a train
// sets the resting voltage whatever phase is playing, so a biphasic pulse must fit whole, as in a burst. A pulse that
// ends in the train's last cycle is whole: its phase 2 ends in the same cycle. Always true for a monophasic pulse (the
// end of the train cuts it short), and for a train that does not stop at PulseTrainEndTime (see the end of handler()).
static inline bool pulseFitsInTrain(byte channel) {
  if ((IsBiphasic[channel] == 0) || ContinuousLoopMode[channel] ||
      ((CustomTrainID[channel] > 0) && (CustomTrainLoop[channel] == 0))) {
    return true;
  }
  uint32_t pulseEnd = SystemTime + Phase1Duration[channel] + InterPhaseInterval[channel] + Phase2Duration[channel];
  return (int32_t)(PulseTrainEndTime[channel] - pulseEnd) >= 0; // pulseEnd <= PulseTrainEndTime, across SystemTime's wrap
}

// Starts the next pulse of a custom train whose times are pulse onsets (CustomTrainTarget 0) on an output channel (0-3),
// from the memory block the channel plays. handler() calls it when the pulse's time has come, or when the pulse before
// ended after that time. A looping train that has played its last pulse starts again from its first. A channel past the
// last pulse of its train stops: the train is empty (an empty block holds no times), or its custom train ID changed
// during the train to a shorter train.
static inline void startCustomPulse(byte channel, byte block) {
  if ((CustomTrainLoop[channel] == 1) && (CustomPulseTimeIndex[channel] >= (int)CustomTrainNpulses[block])) {
    CustomPulseTimeIndex[channel] = 0;
    PulseTrainTimestamps[channel] = SystemTime;
  }
  int pulse = CustomPulseTimeIndex[channel];
  if (pulse >= (int)CustomTrainNpulses[block]) {
    killChannel(channel);
    return;
  }
  // A monophasic pulse whose next pulse starts before its phase 1 would end, or as it ends, is a sample of a custom
  // waveform: it holds until the next pulse's time, with no rest between them. The last pulse has no next pulse time
  // (the slot after the train is never loaded, and on Pulse Pal 3 not even zeroed at startup), so it lasts
  // Phase1Duration, and a looping train starts again when it ends (see endCustomTrainPass()). A biphasic pulse always
  // plays whole: phase 1, the inter-phase interval and phase 2. If the next pulse's time passes meanwhile, the next pulse
  // starts as this one ends.
  if ((IsBiphasic[channel] == 0) && (pulse + 1 < (int)CustomTrainNpulses[block]) &&
      ((CustomPulseTimes[block][pulse+1] - CustomPulseTimes[block][pulse]) <= Phase1Duration[channel])) {
    PulseStatus[channel] = PULSE_IDLE;
    NextPulseTransitionTime[channel] = PulseTrainTimestamps[channel] + CustomPulseTimes[block][pulse+1];
  } else {
    PulseStatus[channel] = PULSE_PHASE1;
    NextPulseTransitionTime[channel] = SystemTime + Phase1Duration[channel];
  }
  setDAC(channel, CustomVoltages[block][pulse]);
  digitalWriteDirect(OutputLEDLines[channel], HIGH);
  if (IsBiphasic[channel] == 0) {
    CustomPulseTimeIndex[channel] = pulse + 1; // A biphasic pulse moves on at its phase 2, which plays this voltage mirrored
  }
}

// Called by handler() when the last pulse of a custom train whose times are pulse onsets has ended, on an output
// channel (0-3). A train that does not loop ends. A looping train starts again at once, with its first pulse, if that
// pulse fits before the end of the train (see pulseFitsInTrain()); if not, the channel rests until the train ends.
static inline void endCustomTrainPass(byte channel, byte block) {
  if (CustomTrainLoop[channel] == 0) {
    killChannel(channel);
  } else if (pulseFitsInTrain(channel)) {
    startCustomPulse(channel, block);
  } else {
    PulseStatus[channel] = PULSE_IDLE;
    NextPulseTransitionTime[channel] = SystemTime; // The PULSE_IDLE case finds that the pulse still does not fit
    digitalWriteDirect(OutputLEDLines[channel], LOW);
    setDAC(channel, RestingVoltage[channel]);
  }
}

// ---------------------------------------------------------------------------------------------------------------
// handler() is the hardware timer callback, and it does all pulse train playback. It runs every TIMER_PERIOD
// microseconds (50us). Time is counted in timer cycles: SystemTime is the number of cycles since playback started,
// and all durations and custom pulse times are also in cycles (20000 cycles = 1 second).
//
// Each cycle:
//  1. If any channel is playing: write DAC updates requested on the previous cycle, and abort all playback if the
//     joystick button is pressed. The final DAC update after playback ends is written on the next idle cycle. The
//     outputs change DAC_LATCH_US after the timer tick, however many channels changed (see dacWriteTimed()).
//  2. Soft triggers scheduled from loop() (op 77 or joystick menu) become active.
//  3. Read the trigger lines, update their LEDs and detect transitions (LineTriggerEvent). On Pulse Pal 3, a rising
//     edge on a trigger channel in param sync mode takes the parameter set op 92 left in paramBuffer, and makes the
//     custom trains op 95 loaded since the last edge current. Output channels that are idle take the parameters at
//     the edge; channels playing a pulse train finish that train on the parameters, and the custom train memory
//     block, it started with, and take the new ones in the cycle it ends. Both happen before step 4, so an edge on
//     the other trigger channel in the same cycle starts its trains with the new parameters and custom trains, and a
//     channel triggered after its train ended plays the whole of the next train with them. A param sync channel
//     starts and stops nothing itself, even when the set it loads moves it to another trigger mode.
//  4. For each output channel that is playing: stop it if a linked trigger channel in toggle mode went low to high,
//     or a linked trigger channel in gated mode went high to low (unless the other trigger channel is also linked,
//     gated and still high). For each channel that is not playing: start it if a linked trigger went low to high,
//     or a soft trigger arrived. Triggers in normal mode, and soft triggers, are ignored while a channel is playing.
//  5. For each output channel, advance its state machine. Transitions occur when SystemTime reaches
//     NextPulseTransitionTime or NextBurstTransitionTime (see timeReached()).
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
// Phase 2 of a biphasic custom pulse is the phase 1 voltage mirrored about 0V. A channel reads its custom train from
// one memory block (customTrainPlayBlock), the train's current block when the channel started, or when its custom
// train ID changed during the train: a train loaded meanwhile plays from the channel's next start. An empty train
// (CustomTrainNpulses 0) ends at once.
//
// StimulatingState is 0 when no channel is playing, 2 on the cycle playback starts from idle, and 1 otherwise.
// loop() only runs the joystick menu while StimulatingState is 0.
// ---------------------------------------------------------------------------------------------------------------
void handler(void) {
  if (StimulatingState == 0) {
      if ((LastStimulatingState == 1) || (DACFlag == 1)) { // The cycle on which all pulse trains have finished, or a DAC update was requested from loop()
        dacWriteTimed(); // Update DAC to final voltages (should be resting voltage), or to values set with setDAC()
        DACFlag = 0;
      }
      SystemTime = 0;
   } else {
  //     if (StimulatingState == 2) {
  //        // Place to include custom code that executes on the first cycle of a pulse train
  //     }
       StimulatingState = 1;
       if (DACFlag == 1) { // A DAC update was requested
         dacWriteTimed(); // Update DAC, DAC_LATCH_US after the timer tick
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
           screenSaverActivity = true; // loop() wakes the screen, after this cycle (see updateScreenSaver())
         } else if ((InputValues[x] == DefaultInputLevel) && (InputValuesLastCycle[x] == TriggerLevel)) {
           LineTriggerEvent[x] = TRIGGER_EVENT_HIGH_TO_LOW; // High to low transition
         }
         InputValuesLastCycle[x] = InputValues[x];
    }

    #if (HARDWARE_VERSION > 2)
      // Take the parameter set that op 92 left waiting, and the custom trains op 95 loaded, if a trigger channel in param
      // sync mode just went high
      if (paramSyncPending || customTrainLoadsStored) {
        byte paramSyncEdges = 0; // One bit per trigger channel in param sync mode that went high this cycle
        for (int y = 0; y < 2; y++) {
          if ((TriggerMode[y] == TRIGGER_MODE_PARAM_SYNC) && (LineTriggerEvent[y] == TRIGGER_EVENT_LOW_TO_HIGH)) {
            bitSet(paramSyncEdges, y);
          }
        }
        if (paramSyncEdges) {
          applyStoredCustomTrains();
          if (paramSyncPending) {
            paramSyncPending = false;
            startParamSync();
          }
          // The set can move a trigger channel out of param sync mode. Its edge was a param sync edge, so it must
          // not also start or toggle trains below under the new mode.
          for (int y = 0; y < 2; y++) {
            if (bitRead(paramSyncEdges, y)) {
              LineTriggerEvent[y] = TRIGGER_EVENT_NONE;
            }
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
          // A soft trigger is ignored while the channel plays, as a trigger line in normal mode is. Left set, it started a
          // second train the cycle this one ended, even when a stop command ended it.
          SoftTriggered[x] = 0;
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
      // The custom train memory block this channel plays (see "Custom train memory" in PulsePal3.ino). A channel takes
      // its train's current block when it starts, and again if its custom train ID changes during the train.
      if ((PreStimulusStatus[x] || StimulusStatus[x]) && (CustomTrainID[x] != customTrainPlayID[x])) {
        customTrainPlayID[x] = CustomTrainID[x];
        if (CustomTrainID[x] > 0) {
          customTrainPlayBlock[x] = currentCustomTrainBlock[CustomTrainID[x]-1];
        }
      }
      byte thisBlock = customTrainPlayBlock[x];
      if (PreStimulusStatus[x] == 1) {
          if (StimulatingState != 2) {
           StimulatingState = 1;
          }
        if (timeReached(PrePulseTrainTimestamps[x] + PulseTrainDelay[x])) {
          if ((CustomTrainID[x] > 0) && (CustomTrainNpulses[thisBlock] == 0)) {
            // An empty custom train plays nothing. Its block holds no times: on Pulse Pal 3 the memory is not even zeroed
            // at startup, and a looping train once held a voltage left there for the whole train.
            killChannel(x);
          } else {
            PreStimulusStatus[x] = 0;
            StimulusStatus[x] = 1;
            PulseStatus[x] = PULSE_IDLE;
            PulseTrainTimestamps[x] = SystemTime;
            PulseTrainEndTime[x] = SystemTime + PulseTrainDuration[x];
            if (CustomTrainTarget[x] == 1)  {
              if (CustomTrainID[x] > 0) {
                NextBurstTransitionTime[x] = SystemTime + CustomPulseTimes[thisBlock][0];
              } else {
                // Legacy behavior: burst target with no custom train selected takes its first burst time from custom train 2
                byte legacyBlock = currentCustomTrainBlock[1];
                NextBurstTransitionTime[x] = SystemTime + ((CustomTrainNpulses[legacyBlock] > 0) ? CustomPulseTimes[legacyBlock][0] : 0);
              }
              BurstStatus[x] = 0;
            } else {
              NextBurstTransitionTime[x] = SystemTime+BurstDuration[x];
            }
            if (CustomTrainID[x] == 0) {
              NextPulseTransitionTime[x] = SystemTime;
              // The PULSE_IDLE case below starts the first pulse in this cycle, unless its phase 1 does not fit in the first
              // burst: a monophasic pulse then still shows here, cut short at the end of the burst, as it is at the start of
              // later bursts. A biphasic pulse must play whole, so only the PULSE_IDLE case starts it.
              if (IsBiphasic[x] == 0) {
                setDAC(x, Phase1Voltage[x]);
              }
            } else {
              NextPulseTransitionTime[x] = SystemTime + CustomPulseTimes[thisBlock][0];
              CustomPulseTimeIndex[x] = 0;
            }
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
              if (timeReached(NextPulseTransitionTime[x])) {
                NextPulseTransitionTime[x] = SystemTime + Phase1Duration[x];
                    // so that it doesn't start a pulse it can't finish due to burst end, or a biphasic pulse it can't finish due to train end
                    if (!((UsesBursts[x] == 1) && !pulseFitsInBurst(x)) && pulseFitsInTrain(x)){
                      PulseStatus[x] = PULSE_PHASE1;
                      digitalWriteDirect(OutputLEDLines[x], HIGH);
                      if ((CustomTrainID[x] > 0) && (CustomTrainTarget[x] == 1)) {
                        setDAC(x, CustomVoltages[thisBlock][CustomPulseTimeIndex[x]]);
                      } else {
                        setDAC(x, Phase1Voltage[x]);
                      }
                    }
                 }
              } else {
               // A biphasic pulse that cannot finish before the train ends is not started. Nothing else happens on this
               // channel until the train ends.
               if (timeReached(NextPulseTransitionTime[x]) && pulseFitsInTrain(x)) {
                 startCustomPulse(x, thisBlock);
               }
              }
            } break;

            case PULSE_PHASE1: { // if this is the first phase of the pulse
             // determine if this phase should end now
             if (timeReached(NextPulseTransitionTime[x])) {
                if (IsBiphasic[x] == 0) {
                  if (CustomTrainID[x] == 0) {
                      NextPulseTransitionTime[x] = SystemTime + InterPulseInterval[x];
                      PulseStatus[x] = PULSE_IDLE;
                      digitalWriteDirect(OutputLEDLines[x], LOW);
                      setDAC(x, RestingVoltage[x]);
                  } else if (CustomTrainTarget[x] == 0) {
                    if (CustomPulseTimeIndex[x] >= (int)CustomTrainNpulses[thisBlock]) {
                      endCustomTrainPass(x, thisBlock); // The last pulse has ended. A looping train plays pulse 0 now
                    } else {
                      NextPulseTransitionTime[x] = PulseTrainTimestamps[x] + CustomPulseTimes[thisBlock][CustomPulseTimeIndex[x]];
                      PulseStatus[x] = PULSE_IDLE;
                      digitalWriteDirect(OutputLEDLines[x], LOW);
                      setDAC(x, RestingVoltage[x]);
                    }
                  } else {
                    NextPulseTransitionTime[x] = SystemTime + InterPulseInterval[x];
                    PulseStatus[x] = PULSE_IDLE;
                    digitalWriteDirect(OutputLEDLines[x], LOW);
                    setDAC(x, RestingVoltage[x]);
                  }
                } else {
                  if (InterPhaseInterval[x] == 0) {
                    NextPulseTransitionTime[x] = SystemTime + Phase2Duration[x];
                    PulseStatus[x] = PULSE_PHASE2;
                    if (CustomTrainID[x] == 0) {
                      setDAC(x, Phase2Voltage[x]);
                    } else {

                       setDAC(x, mirrorAboutZero(CustomVoltages[thisBlock][CustomPulseTimeIndex[x]]));
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
               if (timeReached(NextPulseTransitionTime[x])) {
                 NextPulseTransitionTime[x] = SystemTime + Phase2Duration[x];
                 PulseStatus[x] = PULSE_PHASE2;
                 if (CustomTrainID[x] == 0) {
                   setDAC(x, Phase2Voltage[x]);
                 } else {
                   setDAC(x, mirrorAboutZero(CustomVoltages[thisBlock][CustomPulseTimeIndex[x]]));
                   if (CustomTrainTarget[x] == 0) {
                       CustomPulseTimeIndex[x] = CustomPulseTimeIndex[x] + 1;
                   }
                 }
               }
            } break;
            case PULSE_PHASE2: {
              if (timeReached(NextPulseTransitionTime[x])) {
                if ((CustomTrainID[x] > 0) && (CustomTrainTarget[x] == 0)) {
                  // A biphasic custom pulse has ended. The next one starts at its own time, or now if that time passed
                  // while this pulse played, with no rest between them.
                  if (CustomPulseTimeIndex[x] >= (int)CustomTrainNpulses[thisBlock]) {
                    endCustomTrainPass(x, thisBlock); // The last pulse has ended. A looping train plays pulse 0 now
                  } else {
                    NextPulseTransitionTime[x] = PulseTrainTimestamps[x] + CustomPulseTimes[thisBlock][CustomPulseTimeIndex[x]];
                    if (timeReached(NextPulseTransitionTime[x]) && pulseFitsInTrain(x)) {
                      startCustomPulse(x, thisBlock);
                    } else {
                      PulseStatus[x] = PULSE_IDLE;
                      digitalWriteDirect(OutputLEDLines[x], LOW);
                      setDAC(x, RestingVoltage[x]);
                    }
                  }
                } else {
                 NextPulseTransitionTime[x] = SystemTime + InterPulseInterval[x];
                 // With no inter-pulse interval, the next pulse starts now, if it can play whole (as in the PULSE_IDLE case).
                 // If not, the channel rests until the next burst, or the end of the train.
                 bool startNextPulse = (CustomTrainID[x] == 0) && (InterPulseInterval[x] == 0) &&
                                       !((UsesBursts[x] == 1) && !pulseFitsInBurst(x)) && pulseFitsInTrain(x);
                 if (!startNextPulse) {
                   PulseStatus[x] = PULSE_IDLE;
                   digitalWriteDirect(OutputLEDLines[x], LOW);
                   setDAC(x, RestingVoltage[x]);
                 } else {
                   PulseStatus[x] = PULSE_PHASE1;
                   NextPulseTransitionTime[x] = (NextPulseTransitionTime[x] - InterPulseInterval[x]) + (Phase1Duration[x]);
                   setDAC(x, Phase1Voltage[x]);
                 }
                }
               }
            } break;

          }
        }
          // Determine if burst status should go to 0 now
       if (UsesBursts[x] == true) {
        if (timeReached(NextBurstTransitionTime[x])) {
          if (BurstStatus[x] == 1) {
            if (CustomTrainID[x] == 0) {
                     NextPulseTransitionTime[x] = SystemTime + BurstInterval[x];
                     NextBurstTransitionTime[x] = SystemTime + BurstInterval[x];
            } else if (CustomTrainTarget[x] == 1) {
              CustomPulseTimeIndex[x] = CustomPulseTimeIndex[x] + 1;
              if (CustomPulseTimeIndex[x] >= (int)CustomTrainNpulses[thisBlock]){
                  killChannel(x);
              }
              // The next burst starts at its own time, or at once if that time passed during this burst
              NextPulseTransitionTime[x] = PulseTrainTimestamps[x] + CustomPulseTimes[thisBlock][CustomPulseTimeIndex[x]];
              NextBurstTransitionTime[x] = NextPulseTransitionTime[x];
            }
              BurstStatus[x] = 0;
              setDAC(x, RestingVoltage[x]);
          } else {
          // Determine if burst status should go to 1 now
            NextBurstTransitionTime[x] = SystemTime + BurstDuration[x];
            NextPulseTransitionTime[x] = SystemTime + Phase1Duration[x];
            if ((IsBiphasic[x] == 0) || (pulseFitsInBurst(x) && pulseFitsInTrain(x))) {
              PulseStatus[x] = PULSE_PHASE1;
              if ((CustomTrainID[x] > 0) && (CustomTrainTarget[x] == 1)) {
                if (CustomPulseTimeIndex[x] < (int)CustomTrainNpulses[thisBlock]){
                    setDAC(x, CustomVoltages[thisBlock][CustomPulseTimeIndex[x]]);
                }
              } else {
                   setDAC(x, Phase1Voltage[x]);
              }
            } else {
              PulseStatus[x] = PULSE_IDLE; // A biphasic pulse cut short by the end of the burst or train would lose its phase 2 (see pulseFitsInBurst())
            }
            BurstStatus[x] = 1;
         }
        }
       }
        // Determine if Stimulus Status should go to 0 now
        if (timeReached(PulseTrainEndTime[x]) && (StimulusStatus[x] == 1)) {
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
  // A soft trigger not yet handled would start the channel again straight after a stop (op 77 then op 98 in one packet)
  SoftTriggerScheduled[outputChannel] = 0;
  SoftTriggered[outputChannel] = 0;
  CustomPulseTimeIndex[outputChannel] = 0;
  customTrainPlayID[outputChannel] = 0; // The channel takes its custom train's current memory block when it starts again
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
    screenSaverActivity = true; // The click also wakes the screen, before loop() shows the message
}

// Sets UsesBursts for an output channel (0-3) from its parameters. Call after any output channel parameter changes.
void updateUsesBursts(byte channel) {
  if ((BurstDuration[channel] == 0) || (BurstInterval[channel] == 0)) {UsesBursts[channel] = false;} else {UsesBursts[channel] = true;}
  if (CustomTrainTarget[channel] == 1) {UsesBursts[channel] = true;}
  if ((CustomTrainID[channel] > 0) && (CustomTrainTarget[channel] == 0)) {UsesBursts[channel] = false;}
}

// Stops the output channels that are playing a custom train (0-based), or waiting out their pulse train delay to play
// it: those playing it from a memory block, or from either of its blocks if block is ANY_CUSTOM_TRAIN_BLOCK. Used by
// loadCustomPulseTrain() before a block is loaded. Call from loop() with interrupts off, so that handler() cannot start
// a channel on the block between the test and the stop.
void stopCustomTrainChannels(byte train, byte block) {
  for (int x = 0; x < 4; x++) {
    if ((StimulusStatus[x] || PreStimulusStatus[x]) && (customTrainPlayID[x] == train + 1) &&
        ((block == ANY_CUSTOM_TRAIN_BLOCK) || (customTrainPlayBlock[x] == block))) {
      killChannel(x);
    }
  }
}

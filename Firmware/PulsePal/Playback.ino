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
//   firstCustomTimeFrom()
//   dropOverlappedCustomPulses()
//   dropOverlappedCustomBursts()
//   startBurst()
//   handler()
//   killChannel()
//   AbortAllPulseTrains()
//   updateUsesBursts()
//   endContinuousLoop()
//   stopCustomTrainChannels()
//   playbackOngoing() (HW2 only)

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
  if ((IsBiphasic[channel] == 0) || (ContinuousLoopMode[channel] && (CustomTrainID[channel] == 0)) ||
      ((CustomTrainID[channel] > 0) && (CustomTrainLoop[channel] == 0))) {
    return true;
  }
  uint32_t pulseEnd = SystemTime + Phase1Duration[channel] + InterPhaseInterval[channel] + Phase2Duration[channel];
  return (int32_t)(PulseTrainEndTime[channel] - pulseEnd) >= 0; // pulseEnd <= PulseTrainEndTime, across SystemTime's wrap
}

// Starts the next pulse of a custom train whose times are pulse onsets (CustomTrainTarget 0) on an output channel (0-3),
// from the memory block the channel plays. handler() calls it when the pulse's time has come, or, past the last pulse of
// the train, when this pass of the train ends. A looping train that has played its last pulse starts again from its
// first. A channel past the last pulse of its train stops: the train is empty (an empty block holds no times), its
// custom train ID changed during the train to a shorter train, or its last pulse was dropped (see
// dropOverlappedCustomPulses()) and the train does not loop.
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
  // (the slot after the train holds whatever an earlier, longer load left there), so it lasts
  // Phase1Duration, and a looping train starts again when it ends (see endCustomTrainPass()). A biphasic pulse always
  // plays whole: phase 1, the inter-phase interval and phase 2. Pulses whose times come meanwhile are dropped (see
  // dropOverlappedCustomPulses()).
  if ((IsBiphasic[channel] == 0) && (pulse + 1 < (int)CustomTrainNpulses[block]) &&
      ((CustomPulseTimes[block][pulse+1] - CustomPulseTimes[block][pulse]) <= Phase1Duration[channel])) {
    PulseStatus[channel] = PULSE_IDLE;
    NextPulseTransitionTime[channel] = PulseTrainTimestamps[channel] + CustomPulseTimes[block][pulse+1];
  } else {
    PulseStatus[channel] = PULSE_PHASE1;
    NextPulseTransitionTime[channel] = SystemTime + Phase1Duration[channel];
  }
  customPulseVoltage[channel] = CustomVoltages[block][pulse]; // A biphasic pulse's phase 2 plays it mirrored
  setDAC(channel, customPulseVoltage[channel]);
  digitalWriteDirect(OutputLEDLines[channel], HIGH);
  CustomPulseTimeIndex[channel] = pulse + 1;
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

// Returns the first time in a custom train memory block, from index first (below the block's CustomTrainNpulses) on,
// that is elapsed or later, or CustomTrainNpulses if there is none. Used to drop the custom pulses and bursts whose
// times came while the one before played (see dropOverlappedCustomPulses()).
//
// Times increase (the clients check this), so a binary search finds it, with at most 14 reads for 10000 times. Checking
// times one at a time could read thousands in one cycle (pulses 100us apart under one long pulse or burst), and hold up
// the timer interrupt for several cycles. Times that do not increase give an unpredictable index, but take no longer.
static inline int firstCustomTimeFrom(byte block, int first, uint32_t elapsed) {
  if (CustomPulseTimes[block][first] >= elapsed) {
    return first; // Nothing to drop: the usual case
  }
  int low = first + 1; // The index is in [low, high]
  int high = (int)CustomTrainNpulses[block];
  while (low < high) {
    int mid = (low + high) / 2;
    if (CustomPulseTimes[block][mid] < elapsed) {
      low = mid + 1;
    } else {
      high = mid;
    }
  }
  return low;
}

// Called by handler() when a biphasic custom pulse that is not the last of its train ends, on an output channel (0-3)
// playing a custom train whose times are pulse onsets. Drops the pulses whose times came while it played, and schedules
// the next pulse at its own time: now, with no rest between the two, or later. If the train's last pulse is dropped,
// it schedules the end of this pass of the train instead, when that pulse would have ended; startCustomPulse() then
// ends the train, or starts a looping train again.
//
// Playing an overlapped pulse late, as the pulse before ended, changed the intervals between pulses: a train whose
// intervals were drawn from a distribution played intervals of exactly one pulse length more often than it held them.
// A dropped pulse leaves its place empty instead: the pulses that play are on time, and a looping train keeps its period.
static inline void dropOverlappedCustomPulses(byte channel, byte block) {
  int nPulses = (int)CustomTrainNpulses[block];
  int next = firstCustomTimeFrom(block, CustomPulseTimeIndex[channel], SystemTime - PulseTrainTimestamps[channel]);
  CustomPulseTimeIndex[channel] = next;
  if (next < nPulses) {
    NextPulseTransitionTime[channel] = PulseTrainTimestamps[channel] + CustomPulseTimes[block][next];
  } else {
    NextPulseTransitionTime[channel] = PulseTrainTimestamps[channel] + CustomPulseTimes[block][nPulses - 1] +
                                       Phase1Duration[channel] + InterPhaseInterval[channel] + Phase2Duration[channel];
  }
}

// Called by handler() when a burst that is not the last of its train ends, on an output channel (0-3) playing a custom
// train whose times are burst onsets (CustomTrainTarget 1). As dropOverlappedCustomPulses() does for pulses, drops the
// bursts whose times came while it played, and schedules the next burst at its own time: now, with no rest between the
// two, or later. If the train's last burst is dropped, it schedules the end of this pass of the train instead, when that
// burst would have ended; startBurst() then ends the train, or starts a looping train again.
static inline void dropOverlappedCustomBursts(byte channel, byte block) {
  int nBursts = (int)CustomTrainNpulses[block];
  int next = firstCustomTimeFrom(block, CustomPulseTimeIndex[channel], SystemTime - PulseTrainTimestamps[channel]);
  CustomPulseTimeIndex[channel] = next;
  if (next < nBursts) {
    NextBurstTransitionTime[channel] = PulseTrainTimestamps[channel] + CustomPulseTimes[block][next];
  } else {
    NextBurstTransitionTime[channel] = PulseTrainTimestamps[channel] + CustomPulseTimes[block][nBursts - 1] +
                                       BurstDuration[channel];
  }
  NextPulseTransitionTime[channel] = NextBurstTransitionTime[channel];
}

// Starts the next pass of a looping custom train whose times are burst onsets, on an output channel (0-3), as its last
// burst ends, or when a last burst that was dropped would have ended. As in a looping train of pulses (see
// startCustomPulse()), the pass starts now, and its first burst at its own time from now: at once if that time is 0. The
// train stops at its duration (see the end of handler()). A train of bursts that loops used to stop after one pass.
static inline void startCustomBurstPass(byte channel, byte block) {
  CustomPulseTimeIndex[channel] = 0;
  PulseTrainTimestamps[channel] = SystemTime;
  NextBurstTransitionTime[channel] = SystemTime + CustomPulseTimes[block][0];
  NextPulseTransitionTime[channel] = NextBurstTransitionTime[channel];
}

// Starts a burst on an output channel (0-3), from the custom train memory block the channel plays, with its first pulse
// if that pulse fits (a monophasic pulse always starts: the end of the burst cuts it short). A channel playing a custom
// train whose times are burst onsets may be past the train's last burst: its last burst was dropped, and this is when
// it would have ended (see dropOverlappedCustomBursts()), or its custom train ID changed during the train to a shorter
// train. A looping train then starts its next pass, which starts a burst now only if its first burst's time is 0; a
// train that does not loop ends.
static inline void startBurst(byte channel, byte block) {
  if ((CustomTrainID[channel] > 0) && (CustomTrainTarget[channel] == 1) &&
      (CustomPulseTimeIndex[channel] >= (int)CustomTrainNpulses[block])) {
    if (CustomTrainLoop[channel] == 0) {
      killChannel(channel);
      return;
    }
    startCustomBurstPass(channel, block);
    if (!timeReached(NextBurstTransitionTime[channel])) {
      return; // The burst code in handler() starts the first burst at its time
    }
  }
  NextBurstTransitionTime[channel] = SystemTime + BurstDuration[channel];
  NextPulseTransitionTime[channel] = SystemTime + Phase1Duration[channel];
  if ((CustomTrainID[channel] > 0) && (CustomTrainTarget[channel] == 1)) {
    customPulseVoltage[channel] = CustomVoltages[block][CustomPulseTimeIndex[channel]]; // Every pulse of this burst plays it
  }
  if ((IsBiphasic[channel] == 0) || (pulseFitsInBurst(channel) && pulseFitsInTrain(channel))) {
    PulseStatus[channel] = PULSE_PHASE1;
    if ((CustomTrainID[channel] > 0) && (CustomTrainTarget[channel] == 1)) {
      setDAC(channel, customPulseVoltage[channel]);
    } else {
      setDAC(channel, Phase1Voltage[channel]);
    }
  } else {
    PulseStatus[channel] = PULSE_IDLE; // A biphasic pulse cut short by the end of the burst or train would lose its phase 2 (see pulseFitsInBurst())
  }
  BurstStatus[channel] = 1;
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
//  3. Read the trigger lines, update their LEDs and detect transitions (LineTriggerEvent). Pulse Pal 3 also detects a
//     pulse that came and went since the last cycle's read (see setupTriggerEdgeLatches()). On Pulse Pal 3, a rising
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
//         BurstStatus           1 during a burst, 0 between bursts. Pulses play whenever UsesBursts is false, whatever it says
//                               (see the burst check below)
//           PulseStatus         PULSE_IDLE -> PULSE_PHASE1 -> [PULSE_INTER_PHASE] -> [PULSE_PHASE2] -> PULSE_IDLE
//                               (bracketed phases are for biphasic pulses only)
//     Parametric trains stop at PulseTrainEndTime, unless ContinuousLoopMode is set. Looping custom trains stop at
//     PulseTrainEndTime, and non-looping custom trains after their last pulse or burst: ContinuousLoopMode does not
//     apply to custom trains, which have CustomTrainLoop.
//     New voltages are stored with setDAC(), and written to the DAC at the start of the next cycle.
//     All DAC writes after setup() happen here, including values set with setDAC() from loop() while idle.
//
// Custom trains (CustomTrainID > 0) use CustomPulseTimes (relative to PulseTrainTimestamps) and CustomVoltages
// in place of the parametric pulse timing and phase 1 voltage. CustomPulseTimeIndex tracks the current pulse.
// If CustomTrainTarget is 0 each time starts a pulse; if 1 each time starts a burst of parametric pulses.
// Phase 2 of a biphasic custom pulse is the phase 1 voltage mirrored about 0V, and a biphasic custom pulse whose time
// comes while the pulse before plays is dropped (see dropOverlappedCustomPulses()), as is a custom burst whose time comes
// while the burst before plays (dropOverlappedCustomBursts()). A channel reads its custom train from
// one memory block (customTrainPlayBlock), the train's current block when the channel started, or when its custom
// train ID changed during the train: a train loaded meanwhile plays from the channel's next start. An empty train
// (CustomTrainNpulses 0) ends at once.
//
// StimulatingState is 0 when no channel is playing, 2 on the cycle playback starts from idle, and 1 otherwise.
// loop() only runs the joystick menu while StimulatingState is 0.
// ---------------------------------------------------------------------------------------------------------------
void handler(void) {
  #if (HARDWARE_VERSION > 2)
    advanceTickCycles(); // First: dacWriteTimed() times the outputs' change from this cycle's tick
  #endif
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
         #if (HARDWARE_VERSION > 2)
           // The line changed since the last read, and is back at the level read then: a pulse, or a gap between
           // pulses, shorter than a cycle came and went between the reads (see setupTriggerEdgeLatches()). Take it as
           // read during the pulse, so that it makes the transitions of a longer one: into it now, out of it next cycle.
           if (triggerEdgeLatched(x) && (InputValues[x] == InputValuesLastCycle[x])) {
             InputValues[x] = !InputValues[x];
           }
         #endif
         if (InputValues[x] == TriggerLevel) {
           digitalWriteDirect(InputLEDLines[x], HIGH);
         } else {
           digitalWriteDirect(InputLEDLines[x], LOW);
         }
         // update LineTriggerEvent with logic representing logic transition
         if ((InputValues[x] == TriggerLevel) && (InputValuesLastCycle[x] == DefaultInputLevel)) {
           LineTriggerEvent[x] = TRIGGER_EVENT_LOW_TO_HIGH; // Low to high transition
           triggerEdgeCount[x]++; // Reported by op 101, whatever the trigger mode
           screenSaverActivity = true; // loop() wakes the screen, after this cycle (see updateScreenSaver())
         } else if ((InputValues[x] == DefaultInputLevel) && (InputValuesLastCycle[x] == TriggerLevel)) {
           LineTriggerEvent[x] = TRIGGER_EVENT_HIGH_TO_LOW; // High to low transition
         }
         InputValuesLastCycle[x] = InputValues[x];
    }
    #if (HARDWARE_VERSION > 2)
      // The trigger inputs' opto-isolator can switch more than once as its input crosses the threshold: measured after
      // about 1% of edges from 3.3 and 4V TTLs, for up to 0.09us. A read during that chatter takes one level, but the
      // switches after it stay latched, and next cycle, with the line still at the level read, they would look like a
      // pulse that came and went. So edges latched this soon after the reads are dropped. A pulse shorter than
      // TRIGGER_CHATTER_NS that starts just after a read is missed.
      delayNanoseconds(TRIGGER_CHATTER_NS);
      clearTriggerEdgeLatches();

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
       if (PreStimulusStatus[x]) { // Started this cycle, once even if both trigger channels and a soft trigger started it
         channelStartCount[x]++; // Reported by op 101
       }
      }
    }
    if (StimulatingState != 2) {
     StimulatingState = 0; // null condition, will be overridden in loop if any channels are still stimulating.
    }
    
    // Check clock and adjust line levels for new time as per programming
    for (int x = 0; x < 4; x++) {
      // The custom train memory block this channel plays (see "Custom train memory" in PulsePal.ino). A channel takes
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
            // An empty custom train plays nothing. Its block holds no times, only what an earlier load left there, and a
            // looping train once held a voltage left there for the whole train.
            killChannel(x);
          } else {
            PreStimulusStatus[x] = 0;
            StimulusStatus[x] = 1;
            PulseStatus[x] = PULSE_IDLE;
            PulseTrainTimestamps[x] = SystemTime;
            PulseTrainEndTime[x] = SystemTime + PulseTrainDuration[x];
            // Set again as each custom pulse or burst starts. Until one has (the custom train ID changed during a
            // parametric pulse or burst), it is a programmed voltage too.
            customPulseVoltage[x] = (CustomTrainID[x] > 0) ? CustomVoltages[thisBlock][0] : Phase1Voltage[x];
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
        // Pulses play during a burst, or always when the channel no longer uses bursts. A change that switched bursts off
        // between two bursts (a burst duration or interval of 0, or a custom train targeting pulses) left BurstStatus 0,
        // and only the burst code below, which runs only while UsesBursts, sets it back: the rest of the train played
        // nothing, and a train that does not end at its duration (a custom train that does not loop, continuous loop
        // mode) stayed "playing" for ever, ignoring triggers. So did a channel started by a trigger in the same cycle as
        // a stop command, which killChannel() in loop() then left with BurstStatus 0.
        if ((BurstStatus[x] == 1) || !UsesBursts[x]) {
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
                        setDAC(x, customPulseVoltage[x]); // The voltage of the custom burst playing
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
                      setDAC(x, mirrorAboutZero(customPulseVoltage[x])); // Phase 1's voltage, mirrored
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
                   setDAC(x, mirrorAboutZero(customPulseVoltage[x])); // Phase 1's voltage, mirrored
                 }
               }
            } break;
            case PULSE_PHASE2: {
              if (timeReached(NextPulseTransitionTime[x])) {
                if ((CustomTrainID[x] > 0) && (CustomTrainTarget[x] == 0)) {
                  // A biphasic custom pulse has ended. Pulses whose times came while it played are dropped, and the next
                  // one starts at its own time: now, with no rest between them, or later.
                  if (CustomPulseTimeIndex[x] >= (int)CustomTrainNpulses[thisBlock]) {
                    endCustomTrainPass(x, thisBlock); // The last pulse has ended. A looping train plays pulse 0 now
                  } else {
                    dropOverlappedCustomPulses(x, thisBlock);
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
                if (CustomTrainLoop[x] == 1) {
                  startCustomBurstPass(x, thisBlock); // The last burst has ended. A looping train starts again (below)
                } else {
                  killChannel(x);
                }
              } else {
                // Bursts whose times came during this burst are dropped, and the next one starts at its own time: now
                // (below), with no rest between them, or later
                dropOverlappedCustomBursts(x, thisBlock);
              }
            }
              BurstStatus[x] = 0;
              setDAC(x, RestingVoltage[x]);
              if ((StimulusStatus[x] == 1) && (CustomTrainID[x] > 0) && (CustomTrainTarget[x] == 1) &&
                  timeReached(NextBurstTransitionTime[x])) {
                startBurst(x, thisBlock);
              }
          } else {
          // Determine if burst status should go to 1 now
            startBurst(x, thisBlock);
         }
        }
       }
        // Determine if Stimulus Status should go to 0 now. A parametric train ends at its duration unless it is in
        // continuous loop mode. A looping custom train ends at its duration, and one that does not loop after its last
        // pulse or burst: continuous loop mode is for parametric trains, and custom trains have their own loop setting.
        if (timeReached(PulseTrainEndTime[x]) && (StimulusStatus[x] == 1)) {
          if (((CustomTrainID[x] > 0) && (CustomTrainLoop[x] == 1)) ||
              ((CustomTrainID[x] == 0) && (ContinuousLoopMode[x] == false))) {
            killChannel(x);
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

// Called when an output channel's (0-3) continuous loop mode is switched off. A parametric train in continuous loop mode
// has no end, so it stops. Continuous loop mode does not apply to custom trains (they have CustomTrainLoop), so a
// channel set to a custom train goes on playing it.
void endContinuousLoop(byte channel) {
  if (CustomTrainID[channel] == 0) {
    killChannel(channel);
  }
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

#if (HARDWARE_VERSION == 2)
  // True if a channel is playing (its delay included), or a soft trigger is about to start one. Op 90 refuses to use
  // the microSD card then (see pauseTimerForSD()). Call with the timer interrupt paused, so that no channel can start
  // between this check and the card's use. Not StimulatingState: handler() clears it only in the cycle after a stop.
  bool playbackOngoing() {
    for (int x = 0; x < 4; x++) {
      if (StimulusStatus[x] || PreStimulusStatus[x] || SoftTriggerScheduled[x] || SoftTriggered[x]) {
        return true;
      }
    }
    return false;
  }
#endif

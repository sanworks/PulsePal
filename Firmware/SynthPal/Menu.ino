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


// Thumb joystick menu. UpdateMenu() is called from loop() on every pass, also during playback. Moving through the menu
// never waits. Editing a value does, as in Pulse Pal firmware (editNumber() and editChoice() return when the joystick
// is clicked): playback carries on meanwhile, since the playback interrupts do all of it, but USB commands wait.
//
// Functions in this file:
//   UpdateMenu()
//   onMenuClick()
//   scrollMenu()
//   refreshMenu()
//   RefreshChannelMenu()
//   RefreshActionMenu()
//   RefreshTriggerMenu()
//   editOutputSetting()
//   formatFrequency()
//   formatVolts()
//   formatDuration()
//   editNumber()
//   editChoice()
//   drawEditLine()
//   waitForButtonRelease()
//   ReadDebouncedButton()
//
// ---------------------------------------------------------------------------------------------------------------
// Thumb joystick menu map. Joystick left/right scrolls through the options at the current level (inMenu), wrapping at
// the ends, and a click selects. Each level stores its selected option in a separate variable:
//
// MENU_TOP              "Synth Pal v3.0" / "Click for menu". Click -> MENU_CHANNEL_LIST, at item 1
// MENU_CHANNEL_LIST     SelectedChannel      1-4   Output channels -> MENU_OUTPUT_CHANNEL
//                                            5-6   Trigger channels -> MENU_TRIGGER_CHANNEL
//                                            7     Frequency: a click edits it (MENU_ITEM_FREQUENCY)
//                                            8     Screen saver on/off (MENU_ITEM_SCREEN_SAVER)
//                                            9     Device info (MENU_ITEM_DEVICE_INFO)
//                                            10    Reset (MENU_ITEM_RESET)
//                                            11    Exit -> MENU_TOP (MENU_ITEM_EXIT)
// MENU_OUTPUT_CHANNEL   SelectedAction       1     Trigger now: plays the channel, or stops it while it plays. The
//                                                  second line says which. Trigger modes do not apply here.
//                                            2-7   Edit the waveform, amplitude, resting voltage, play duration, and
//                                                  the links to trigger channels 1 and 2 (enum OutputChannelAction)
//                                            8     Exit -> MENU_CHANNEL_LIST (MENU_ACTION_EXIT)
// MENU_TRIGGER_CHANNEL  SelectedInputAction  1     Trigger now: acts as a rising edge on the trigger channel, in its
//                                                  trigger mode. In gated mode, no falling edge follows: the channels
//                                                  play for their play duration, or until stopped.
//                                            2     Edit the trigger mode
//                                            3     Exit -> MENU_CHANNEL_LIST
//                       SelectedChannel stays 5-6 while in this menu.
//
// To add an option to a level: add it to that level's enum in SynthPal.ino (the last item must stay the exit item,
// which the scroll wraps at), then add its click in onMenuClick() (or editOutputSetting()) and its screen in the
// level's Refresh...Menu() function.
// ---------------------------------------------------------------------------------------------------------------

#define EDIT_REPEAT_MS 200 // While the joystick is held, a value being edited changes this often
#define CURSOR_BLINK_MS 300 // The cursor under the digit being edited blinks at this rate

const char* const waveformNames[] = {"Sine", "Triangle", "Square", "Sawtooth", "Fixed Voltage"}; // Indexed by enum
                                                                                                 // WaveformValue
const char* const triggerModeNames[] = {"Normal", "Toggle", "Pulse Gated"}; // Indexed by enum TriggerModeValue
const char* const offOnNames[] = {"Off", "On"};

void UpdateMenu() {
  ClickerX = analogRead(ClickerXLine);
  ClickerY = analogRead(ClickerYLine);
  ClickerButtonState = ReadDebouncedButton();
  // A joystick click or push restarts the screen saver's idle time. While the screen is dimmed, it only wakes the
  // screen: it is marked as handled here, so the menu acts on the next click or push, not on this one.
  if (ClickerButtonState || (ClickerX < ClickerMinThreshold) || (ClickerX > ClickerMaxThreshold) ||
      (ClickerY < ClickerMinThreshold) || (ClickerY > ClickerMaxThreshold)) {
    screenSaverActivity = true;
    if (screenDimmed) {
      if (ClickerButtonState) {LastClickerButtonState = 1;}
      if (ClickerX < ClickerMinThreshold) {LastClickerXState = 1;}
      if (ClickerX > ClickerMaxThreshold) {LastClickerXState = 2;}
      return;
    }
  }
  if (ClickerButtonState && !LastClickerButtonState) {
    onMenuClick();
  }
  LastClickerButtonState = ClickerButtonState;
  if ((LastClickerXState != 1) && (ClickerX < 200)) {
    LastClickerXState = 1;
    scrollMenu(-1);
  } else if ((LastClickerXState != 2) && (ClickerX > ClickerMaxThreshold)) {
    LastClickerXState = 2;
    scrollMenu(1);
  } else if ((ClickerX > ClickerMinThreshold) && (ClickerX < ClickerMaxThreshold)) {
    LastClickerXState = 0;
  }
  if ((inMenu == MENU_OUTPUT_CHANNEL) && (SelectedAction == MENU_ACTION_TRIGGER) &&
      (isPlaying(SelectedChannel - 1) != shownPlayState)) {
    RefreshActionMenu(); // The channel started or stopped playing, e.g. from a trigger or at the end of its play duration
  }
}

void onMenuClick() {
  switch (inMenu) {
    case MENU_TOP: {
      inMenu = MENU_CHANNEL_LIST;
      SelectedChannel = 1;
      viewingInfo = false;
      RefreshChannelMenu();
    } break;
    case MENU_CHANNEL_LIST: {
      if (SelectedChannel <= N_CHANNELS) { // An output channel
        inMenu = MENU_OUTPUT_CHANNEL;
        SelectedAction = MENU_ACTION_TRIGGER;
        RefreshActionMenu();
      } else if (SelectedChannel <= N_CHANNELS + 2) { // A trigger channel
        inMenu = MENU_TRIGGER_CHANNEL;
        SelectedInputAction = MENU_INPUT_ACTION_TRIGGER;
        RefreshTriggerMenu();
      } else {
        switch (SelectedChannel) {
          case MENU_ITEM_FREQUENCY: {
            uint32_t newFrequency = editNumber(frequencyCentiHz, MIN_FREQUENCY_CENTIHZ, MAX_FREQUENCY_CENTIHZ, 5, 2,
                                               false, " Hz");
            if (newFrequency != frequencyCentiHz) {
              setFrequency(newFrequency);
            }
            RefreshChannelMenu();
          } break;
          case MENU_ITEM_SCREEN_SAVER: { // Its timeout is set over USB only (op 99)
            screenSaverEnabled = !screenSaverEnabled;
            screenSaverSavePending = true; // Saved to the EEPROM by updateScreenSaver()
            RefreshChannelMenu();
          } break;
          case MENU_ITEM_DEVICE_INFO: {
            viewingInfo = !viewingInfo;
            if (viewingInfo) {
              write2Screen("Hardware v" TOSTRING(HARDWARE_VERSION), "Firmware v" TOSTRING(FIRMWARE_VERSION));
            } else {
              RefreshChannelMenu();
            }
          } break;
          case MENU_ITEM_RESET: {
            noInterrupts();
            triggersEnabled = false;
            stopChannels(ALL_CHANNELS); // handler() returns the outputs to their resting voltages on its next tick
            interrupts();
            write2Screen(" ", " ");
            delay(1000);
            Software_Reset();
          } break;
          default: { // MENU_ITEM_EXIT
            showTopScreen();
          } break;
        }
      }
    } break;
    case MENU_OUTPUT_CHANNEL: {
      byte channel = SelectedChannel - 1;
      if (SelectedAction == MENU_ACTION_TRIGGER) { // Play the channel, or stop it
        noInterrupts();
        if (isPlaying(channel)) {
          stopChannels(bit(channel));
        } else {
          startChannels(bit(channel));
        }
        interrupts();
        RefreshActionMenu();
      } else if (SelectedAction == MENU_ACTION_EXIT) {
        inMenu = MENU_CHANNEL_LIST;
        RefreshChannelMenu();
      } else {
        editOutputSetting(channel, SelectedAction);
        RefreshActionMenu();
      }
    } break;
    case MENU_TRIGGER_CHANNEL: {
      byte triggerChannel = SelectedChannel - (N_CHANNELS + 1);
      switch (SelectedInputAction) {
        case MENU_INPUT_ACTION_TRIGGER: {
          noInterrupts();
          processTriggerEdge(triggerChannel, true);
          interrupts();
          write2Screen("< Trigger Now  >", "ZAP!");
          waitForButtonRelease();
          RefreshTriggerMenu();
        } break;
        case MENU_INPUT_ACTION_MODE: {
          byte newMode = editChoice(TriggerMode[triggerChannel], MAX_TRIGGER_MODE + 1, triggerModeNames, false);
          noInterrupts();
          TriggerMode[triggerChannel] = newMode;
          interrupts();
          RefreshTriggerMenu();
        } break;
        default: { // MENU_INPUT_ACTION_EXIT
          inMenu = MENU_CHANNEL_LIST;
          RefreshChannelMenu();
        } break;
      }
    } break;
  }
}

// Moves the selection at the current menu level by one item (direction -1 or 1), wrapping at the ends
void scrollMenu(int8_t direction) {
  byte *selection;
  byte lastItem;
  switch (inMenu) {
    case MENU_CHANNEL_LIST: {selection = &SelectedChannel; lastItem = MENU_ITEM_EXIT; viewingInfo = false;} break;
    case MENU_OUTPUT_CHANNEL: {selection = &SelectedAction; lastItem = MENU_ACTION_EXIT;} break;
    case MENU_TRIGGER_CHANNEL: {selection = &SelectedInputAction; lastItem = MENU_INPUT_ACTION_EXIT;} break;
    default: return; // MENU_TOP has nothing to scroll
  }
  if ((direction < 0) && (*selection == 1)) {
    *selection = lastItem;
  } else if ((direction > 0) && (*selection == lastItem)) {
    *selection = 1;
  } else {
    *selection += direction;
  }
  refreshMenu();
}

// Draws the selected option of the current menu level
void refreshMenu() {
  switch (inMenu) {
    case MENU_CHANNEL_LIST: {RefreshChannelMenu();} break;
    case MENU_OUTPUT_CHANNEL: {RefreshActionMenu();} break;
    case MENU_TRIGGER_CHANNEL: {RefreshTriggerMenu();} break;
    default: {showTopScreen();} break;
  }
}

void RefreshChannelMenu() {
  static const char* const channelNames[N_CHANNELS] = {"<  Channel 1  >", "<  Channel 2  >", "<  Channel 3  >", "<  Channel 4  >"};
  if (SelectedChannel <= N_CHANNELS) {
    write2Screen("Output Channels", channelNames[SelectedChannel - 1]);
  } else if (SelectedChannel <= N_CHANNELS + 2) {
    write2Screen("Trigger Channels", channelNames[SelectedChannel - (N_CHANNELS + 1)]);
  } else {
    switch (SelectedChannel) {
      case MENU_ITEM_FREQUENCY: {write2Screen("Frequency", formatFrequency(frequencyCentiHz));} break;
      case MENU_ITEM_SCREEN_SAVER: {write2Screen("Screen Saver", offOnNames[screenSaverEnabled]);} break;
      case MENU_ITEM_DEVICE_INFO: {write2Screen("Device Info", "<Click to view>");} break;
      case MENU_ITEM_RESET: {write2Screen("-RESET-", "<Click to reset>");} break;
      default: {write2Screen("<Click to exit>", " ");} break; // MENU_ITEM_EXIT
    }
  }
}

// Draws the selected option of an output channel's menu, with the setting's value on the second line
void RefreshActionMenu() {
  byte channel = SelectedChannel - 1;
  shownPlayState = -1;
  switch (SelectedAction) {
    case MENU_ACTION_TRIGGER: {
      shownPlayState = isPlaying(channel);
      write2Screen("< Trigger Now  >", shownPlayState ? "Click to stop" : "Click to play");
    } break;
    case MENU_ACTION_WAVEFORM: {write2Screen("<   Waveform   >", waveformNames[waveform[channel]]);} break;
    case MENU_ACTION_AMPLITUDE: { // A fixed voltage is a voltage, not peak to peak
      bool isFixed = (waveform[channel] == WAVEFORM_FIXED_VOLTAGE);
      write2Screen("<  Amplitude   >", formatVolts(amplitudeMicrovolts[channel], isFixed ? " V" : " Vpp"));
    } break;
    case MENU_ACTION_RESTING_VOLTAGE: {write2Screen("<RestingVoltage>", formatVolts(restingVoltageMicrovolts[channel], " V"));} break;
    case MENU_ACTION_PLAY_DURATION: {write2Screen("<Play Duration >", formatDuration(playDurationMicros[channel]));} break;
    case MENU_ACTION_LINK_TRIGGER1: {write2Screen("<Link Trigger 1>", offOnNames[TriggerAddress[0][channel]]);} break;
    case MENU_ACTION_LINK_TRIGGER2: {write2Screen("<Link Trigger 2>", offOnNames[TriggerAddress[1][channel]]);} break;
    default: {write2Screen("<     Exit     >", " ");} break; // MENU_ACTION_EXIT
  }
}

void RefreshTriggerMenu() {
  byte triggerChannel = SelectedChannel - (N_CHANNELS + 1);
  switch (SelectedInputAction) {
    case MENU_INPUT_ACTION_TRIGGER: {write2Screen("< Trigger Now  >", " ");} break;
    case MENU_INPUT_ACTION_MODE: {write2Screen("< Trigger Mode >", triggerModeNames[TriggerMode[triggerChannel]]);} break;
    default: {write2Screen("<     Exit     >", " ");} break; // MENU_INPUT_ACTION_EXIT
  }
}

// Edits one setting of an output channel (0-3) with the joystick, and applies it. Voltages are edited in steps of
// 0.01V and play durations in steps of 0.1ms, as in Pulse Pal firmware. A value set more finely over USB is kept if
// the edit leaves it unchanged. Amplitudes and resting voltages are limited so that the waveform stays within +/-10V.
// A fixed voltage's amplitude is a voltage, -10V to 10V, and a new waveform takes the nearest amplitude it can play
// (fitAmplitude()).
void editOutputSetting(byte channel, byte action) {
  bool isFixed = (waveform[channel] == WAVEFORM_FIXED_VOLTAGE);
  switch (action) {
    case MENU_ACTION_WAVEFORM: {
      byte newWaveform = editChoice(waveform[channel], MAX_WAVEFORM + 1, waveformNames, true);
      if (newWaveform != waveform[channel]) {
        waveform[channel] = newWaveform;
        amplitudeMicrovolts[channel] = fitAmplitude(newWaveform, restingVoltageMicrovolts[channel],
                                                    amplitudeMicrovolts[channel]);
        updateChannelOutput(channel);
      }
    } break;
    case MENU_ACTION_AMPLITUDE: { // In hundredths of a volt
      int32_t amplitude = amplitudeMicrovolts[channel];
      int32_t start = (amplitude >= 0) ? ((amplitude + 5000) / 10000) : -((-amplitude + 5000) / 10000);
      int32_t newValue;
      if (isFixed) {
        newValue = editNumber(start, -MAX_VOLTAGE_MICROVOLTS / 10000, MAX_VOLTAGE_MICROVOLTS / 10000, 2, 2, true, " V");
      } else {
        int32_t maxValue = ((2 * MAX_VOLTAGE_MICROVOLTS) - (2 * abs(restingVoltageMicrovolts[channel]))) / 10000;
        newValue = editNumber(start, 0, maxValue, 2, 2, false, " Vpp");
      }
      if (newValue != start) {
        amplitudeMicrovolts[channel] = newValue * 10000;
        updateChannelOutput(channel);
      }
    } break;
    case MENU_ACTION_RESTING_VOLTAGE: { // In hundredths of a volt
      int32_t rest = restingVoltageMicrovolts[channel];
      int32_t start = (rest >= 0) ? ((rest + 5000) / 10000) : -((-rest + 5000) / 10000);
      int32_t maxValue = isFixed ? (MAX_VOLTAGE_MICROVOLTS / 10000) // Any resting voltage goes with a fixed voltage
                                 : (((2 * MAX_VOLTAGE_MICROVOLTS) - amplitudeMicrovolts[channel]) / 20000);
      int32_t newValue = editNumber(start, -maxValue, maxValue, 2, 2, true, " V");
      if (newValue != start) {
        restingVoltageMicrovolts[channel] = newValue * 10000;
        updateChannelOutput(channel);
      }
    } break;
    case MENU_ACTION_PLAY_DURATION: { // In tenths of a millisecond
      int32_t start = (playDurationMicros[channel] + 50) / 100;
      int32_t newValue = editNumber(start, 0, MAX_PLAY_DURATION_MICROS / 100, 4, 4, false, " s");
      if (newValue != start) {
        uint32_t durations[N_CHANNELS];
        for (byte i = 0; i < N_CHANNELS; i++) {
          durations[i] = playDurationMicros[i];
        }
        durations[channel] = (uint32_t)newValue * 100;
        setPlayDurations(durations);
      }
    } break;
    case MENU_ACTION_LINK_TRIGGER1:
    case MENU_ACTION_LINK_TRIGGER2: {
      byte triggerChannel = (action == MENU_ACTION_LINK_TRIGGER1) ? 0 : 1;
      byte newLink = editChoice(TriggerAddress[triggerChannel][channel], 2, offOnNames, false);
      noInterrupts();
      TriggerAddress[triggerChannel][channel] = newLink;
      interrupts();
    } break;
  }
}

// Formats a frequency in centiHz for the screen, e.g. "100.00 Hz". The returned buffer is reused by the next call.
const char* formatFrequency(uint32_t centiHz) {
  static char text[17];
  snprintf(text, sizeof(text), "%lu.%02lu Hz", (unsigned long)(centiHz / 100), (unsigned long)(centiHz % 100));
  return text;
}

// Formats a voltage in microvolts for the screen, rounded to 0.01V, e.g. "-1.25 V". The returned buffer is reused by
// the next call.
const char* formatVolts(int32_t microvolts, const char* units) {
  static char text[17];
  uint32_t centivolts = ((uint32_t)abs(microvolts) + 5000) / 10000;
  snprintf(text, sizeof(text), "%s%lu.%02lu%s", ((microvolts < 0) && (centivolts > 0)) ? "-" : "",
           (unsigned long)(centivolts / 100), (unsigned long)(centivolts % 100), units);
  return text;
}

// Formats a play duration in microseconds for the screen, rounded to 0.1ms, e.g. "1.5000 s", or "Infinite" for 0. The
// returned buffer is reused by the next call.
const char* formatDuration(uint32_t micros) {
  static char text[17];
  if (micros == 0) {
    return "Infinite";
  }
  uint32_t tenthsOfMs = (micros + 50) / 100;
  snprintf(text, sizeof(text), "%lu.%04lu s", (unsigned long)(tenthsOfMs / 10000), (unsigned long)(tenthsOfMs % 10000));
  return text;
}

// Edits a number with the joystick, digit by digit, and returns it when the joystick is clicked. The number is shown
// as an optional sign, nIntegerDigits digits, a decimal point, nDecimals digits and units, e.g. "+05.00 V", and value
// is in units of its last digit (here, hundredths of a volt). Up and down change the digit under the cursor, without
// carrying into the next digit, and left and right move the cursor. On a signed number, the cursor's leftmost
// position is the sign, which up and down switch. A digit cannot go up past maxValue. It can go below minValue while
// the number is edited (e.g. 1.00Hz to 0.50Hz passes through 0.00Hz), but if the number is still out of range when the
// joystick is clicked, startValue is returned, as in Pulse Pal firmware.
int32_t editNumber(int32_t startValue, int32_t minValue, int32_t maxValue, byte nIntegerDigits, byte nDecimals,
                   bool isSigned, const char* units) {
  byte nDigits = nIntegerDigits + nDecimals;
  bool negative = (startValue < 0);
  uint32_t magnitude = abs(startValue);
  int8_t cursor = nIntegerDigits - 1; // The digit edited: 0 is the most significant, and -1 is the sign
  int8_t leftmost = isSigned ? -1 : 0;
  char text[17];
  bool cursorVisible = true;
  uint32_t blinkTime = millis();
  bool redraw = true;
  waitForButtonRelease(); // The click that opened the editor
  while (digitalRead(ClickerButtonLine) == HIGH) { // Until a click: the button pulls the line low
    if ((millis() - blinkTime) > CURSOR_BLINK_MS) {
      cursorVisible = !cursorVisible;
      blinkTime = millis();
      redraw = true;
    }
    ClickerX = analogRead(ClickerXLine);
    ClickerY = analogRead(ClickerYLine);
    bool moved = true;
    uint32_t weight = 1;
    for (int8_t d = nDigits - 1; d > cursor; d--) {
      weight *= 10;
    }
    uint32_t digit = (magnitude / weight) % 10;
    uint32_t maxMagnitude = negative ? -minValue : maxValue;
    if (ClickerY < ClickerMinThreshold) { // Up
      if (cursor < 0) {
        if (magnitude <= (uint32_t)(negative ? maxValue : -minValue)) {
          negative = !negative;
        }
      } else if ((digit < 9) && (magnitude + weight <= maxMagnitude)) {
        magnitude += weight;
      }
    } else if (ClickerY > ClickerMaxThreshold) { // Down
      if (cursor < 0) {
        if (magnitude <= (uint32_t)(negative ? maxValue : -minValue)) {
          negative = !negative;
        }
      } else if (digit > 0) {
        magnitude -= weight;
      }
    } else if ((ClickerX > ClickerMaxThreshold) && (cursor < nDigits - 1)) {
      cursor++;
    } else if ((ClickerX < ClickerMinThreshold) && (cursor > leftmost)) {
      cursor--;
    } else {
      moved = false;
    }
    if (moved) {
      cursorVisible = true;
      blinkTime = millis();
      redraw = true;
    }
    if (redraw) {
      char *p = text;
      if (isSigned) {
        *p++ = negative ? '-' : '+';
      }
      uint32_t remaining = magnitude;
      char digits[10];
      for (int8_t d = nDigits - 1; d >= 0; d--) {
        digits[d] = '0' + (remaining % 10);
        remaining /= 10;
      }
      for (byte d = 0; d < nDigits; d++) {
        if (d == nIntegerDigits) {
          *p++ = '.';
        }
        *p++ = digits[d];
      }
      strcpy(p, units);
      byte column = (cursor < 0) ? 0 : ((isSigned ? 1 : 0) + cursor + ((cursor >= nIntegerDigits) ? 1 : 0));
      drawEditLine(text, column, cursorVisible);
      redraw = false;
    }
    if (moved) {
      delay(EDIT_REPEAT_MS);
    }
  }
  int32_t value = negative ? -(int32_t)magnitude : (int32_t)magnitude;
  if ((value < minValue) || (value > maxValue)) {
    value = startValue;
  }
  LastClickerButtonState = 1; // The click that ended the edit must not also be taken as a click in the menu
  screenSaverActivity = true;
  return value;
}

// Edits a choice from a list of names with the joystick, and returns its index when the joystick is clicked. Up moves
// to the next name and down to the one before, without wrapping, as in Pulse Pal firmware. With downIsNext, the list
// reads downwards instead, first name at the top: down moves to the next name. The waveform list does this, so that
// from its first name, the default sine wave, the joystick moves down to the others.
byte editChoice(byte startValue, byte nChoices, const char* const* names, bool downIsNext) {
  byte value = startValue;
  bool cursorVisible = true;
  uint32_t blinkTime = millis();
  bool redraw = true;
  waitForButtonRelease(); // The click that opened the editor
  while (digitalRead(ClickerButtonLine) == HIGH) {
    if ((millis() - blinkTime) > CURSOR_BLINK_MS) {
      cursorVisible = !cursorVisible;
      blinkTime = millis();
      redraw = true;
    }
    ClickerY = analogRead(ClickerYLine);
    bool up = (ClickerY < ClickerMinThreshold);
    bool down = (ClickerY > ClickerMaxThreshold);
    bool toNext = downIsNext ? down : up;
    bool toPrevious = downIsNext ? up : down;
    bool moved = false;
    if (toNext && (value < nChoices - 1)) {
      value++;
      moved = true;
    } else if (toPrevious && (value > 0)) {
      value--;
      moved = true;
    }
    if (moved) {
      cursorVisible = true;
      blinkTime = millis();
      redraw = true;
    }
    if (redraw) {
      drawEditLine(names[value], 0, cursorVisible);
      redraw = false;
    }
    if (moved) {
      delay(EDIT_REPEAT_MS);
    }
  }
  LastClickerButtonState = 1; // The click that ended the edit must not also be taken as a click in the menu
  screenSaverActivity = true;
  return value;
}

// Draws a value being edited on the second line of the screen, with the cursor under one of its characters
void drawEditLine(const char* text, byte column, bool cursorVisible) {
  lcd.setCursor(0, 1);
  lcd.print("                ");
  lcd.setCursor(0, 1);
  lcd.print(text);
  lcd.setCursor(column, 1);
  if (cursorVisible) {
    lcd.cursor();
  } else {
    lcd.noCursor();
  }
  lcd.render();
}

// Waits until the joystick button has been released for 50ms, so that a click that opens an editor, and its bounce
// on release, are not taken for the click that ends it
void waitForButtonRelease() {
  uint32_t releasedTime = millis();
  while ((millis() - releasedTime) < 50) {
    if (digitalRead(ClickerButtonLine) == LOW) {
      releasedTime = millis();
    }
  }
}

// Returns 1 while the joystick button has been pressed for more than 75ms. Never waits.
boolean ReadDebouncedButton() {
  uint32_t now = millis();
  boolean buttonState = digitalRead(ClickerButtonLine); // 0 while pressed: the button pulls the line to ground
  if (buttonState != lastButtonState) {
    lastDebounceTime = now;
  }
  lastButtonState = buttonState;
  return ((now - lastDebounceTime) > 75) && (buttonState == 0);
}

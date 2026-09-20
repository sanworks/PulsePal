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


// Thumb joystick menu: navigation (see the menu map above UpdateSettingsMenu()), the editor for parameter values,
// and formatting of values for display.
//
// Functions in this file:
//   UpdateSettingsMenu()
//   menuActionParam()
//   getOutputParam()
//   setOutputParam()
//   scrollOutputAction()
//   centerText()
//   RefreshChannelMenu()
//   RefreshActionMenu()
//   RefreshTriggerMenu()
//   RefreshFileMenu()
//   ShowAbortMessage()
//   ReadDebouncedButton()
//   FormatNumberForDisplay()
//   placeEditCursor()
//   redrawEditValue()
//   digitsToVolts()
//   ReturnUserValue()

// ---------------------------------------------------------------------------------------------------------------
// Thumb joystick menu map. UpdateSettingsMenu() is called from loop() whenever no channel is playing.
// Joystick left/right scrolls through the options at the current menu level (inMenu), and a click selects.
// Each level stores its selected option in a separate variable:
//
// MENU_TOP              "Click for menu". Click -> MENU_CHANNEL_LIST
// MENU_CHANNEL_LIST     SelectedChannel      1-4   Output channels -> MENU_OUTPUT_CHANNEL
//                                            5-6   Trigger channels -> MENU_TRIGGER_CHANNEL
//                                            7     Save settings -> MENU_FILE_SAVE
//                                            8     Load settings -> MENU_FILE_LOAD
//                                            9     Erase settings -> MENU_FILE_DELETE
//                                            10    Device info
//                                            11    Reset
//                                            12    Exit -> MENU_TOP
// MENU_OUTPUT_CHANNEL   SelectedAction       1     Trigger now -> MENU_OUTPUT_TRIGGER (MENU_ACTION_TRIGGER)
//                                            2-17  Edit a parameter. Which parameters, in which order, and their
//                                                  labels, limits and storage, all come from menuActionParams and
//                                                  outputParams in PulsePal3.ino. Parameters marked biphasicOnly
//                                                  are skipped when the channel is monophasic.
//                                            18    Exit -> MENU_CHANNEL_LIST (MENU_ACTION_EXIT)
// MENU_OUTPUT_TRIGGER   SelectedStimMode     1     Single train
//                                            2     Single pulse
//                                            3     Continuous on/off
//                                            4     Exit -> MENU_OUTPUT_CHANNEL
// MENU_TRIGGER_CHANNEL  SelectedInputAction  1     Trigger linked output channels
//                                            2     Edit trigger mode
//                                            3     Exit -> MENU_CHANNEL_LIST
//                       SelectedChannel is 1-2 while in this menu, and is restored to 5-6 on exit.
// MENU_FILE_LOAD        myFilePos            0     Cancel -> MENU_CHANNEL_LIST
//                                            1     The default settings file (DEFAULT_SETTINGS_FILE_NAME)
//                                            2+    Other files in the Pulse_Pal folder
// MENU_FILE_SAVE        myFilePos            0     Cancel -> MENU_CHANNEL_LIST
//                                            1     New file (opens the file name editor)
//                                            2+    Files in the Pulse_Pal folder, to overwrite
// MENU_FILE_DELETE      myFilePos            0     Cancel -> MENU_CHANNEL_LIST
//                                            1+    Files in the Pulse_Pal folder, to erase
//                       The default settings file is not listed in the save and erase menus, and a new file cannot
//                       use its name, so the default parameters can always be loaded. File lists come from
//                       findListedFile(), and are drawn by RefreshFileMenu().
//
// To add an output channel parameter to the menu, add its code to menuActionParams in PulsePal3.ino; nothing in
// this file needs to change. To add an option to any other level: update the click handler below, the wrap-around
// limits in the left/right scroll handlers below, and the Refresh...Menu() function that draws the option.
// ---------------------------------------------------------------------------------------------------------------
void UpdateSettingsMenu() {
    ClickerX = analogRead(ClickerXLine);
    ClickerY = analogRead(ClickerYLine);
    ClickerButtonState = ReadDebouncedButton();
    if (ClickerButtonState == 1 && LastClickerButtonState == 0) {
        LastClickerButtonState = 1;
        switch(inMenu) {
          case MENU_TOP: { // Menu top
            inMenu = MENU_CHANNEL_LIST;
            SelectedChannel = 1;
            write2Screen("Output Channels","<  Channel 1  >");
            NeedUpdate = 1;
          } break;
          case MENU_CHANNEL_LIST: { // Channel / Save-Load / Reset Menu
            switch(SelectedChannel) {
              case 5:{
                inMenu = MENU_TRIGGER_CHANNEL; // trigger menu
                SelectedInputAction = 1;
                SelectedChannel = 1;
                write2Screen("< Trigger Now  >"," ");
              } break;  
              case 6: {
                inMenu = MENU_TRIGGER_CHANNEL; // trigger menu
                SelectedInputAction = 1;
                SelectedChannel = 2;
                write2Screen("< Trigger Now  >"," ");
              } break;
              case 7: { // Save settings
                inMenu = MENU_FILE_SAVE; // file save menu
                myFilePos = 1;
                RefreshFileMenu();
              } break;
              case 8: { // Load settings
                inMenu = MENU_FILE_LOAD; // file load menu
                settingsFile.close();
                myFilePos = 1;
                RefreshFileMenu();
                settingsFile.open(currentSettingsFileNameChar, O_READ);
              } break;
              case 9: { // Delete settings
                inMenu = MENU_FILE_DELETE;
                settingsFile.close();
                myFilePos = 1;
                RefreshFileMenu();
              } break;
              case 10: { // Info
                if (!viewingInfo) {
                  write2Screen("Hardware v" TOSTRING(HARDWARE_VERSION), "Firmware v" TOSTRING(FIRMWARE_VERSION));
                  viewingInfo = true;
                } else {
                  write2Screen("Device Info","<Click to view>");
                  viewingInfo = false;
                }
              } break;
              case 11: { // Reset
              write2Screen(" "," ");
              delayMicroseconds(1000000);
                Software_Reset();
              } break;
              case 12: {
                inMenu = MENU_TOP;
                write2Screen(CommanderString," Click for menu");
              } break;
              
              default: {
                inMenu = MENU_OUTPUT_CHANNEL; // output menu
                SelectedAction = MENU_ACTION_TRIGGER;
                write2Screen("< Trigger Now  >"," ");
              } break;
           }
         } break;
         case MENU_OUTPUT_CHANNEL: { // Output channel menu: trigger the channel, edit a parameter, or exit
          byte thisChannel = SelectedChannel - 1;
          if (SelectedAction == MENU_ACTION_TRIGGER) {
            inMenu = MENU_OUTPUT_TRIGGER; // soft-trigger menu
            write2Screen("< Single Train >"," ");
            SelectedStimMode = 1;
          } else if (SelectedAction == MENU_ACTION_EXIT) {
            inMenu = MENU_CHANNEL_LIST;
            RefreshChannelMenu(SelectedChannel);
          } else { // Edit the parameter this menu action selects
            const OutputParam &param = menuActionParam(SelectedAction);
            uint32_t newValue = ReturnUserValue(getOutputParam(param, thisChannel), param.minValue, param.maxValue, param.units);
            setOutputParam(param, thisChannel, newValue);
            if (param.values == (void*)RestingVoltage) { // The channel rests at this voltage, so update the output now
              setDAC(thisChannel, RestingVoltage[thisChannel]);
            }
          }
          updateUsesBursts(thisChannel);
          } break;
          case MENU_OUTPUT_TRIGGER: { // Trigger menu
          switch (SelectedStimMode) {
            case 1: {
              // Soft-trigger channel
              write2Screen("< Single Train >","      ZAP!");
              delayMicroseconds(100000);
              while (ClickerButtonState == 1) {
               ClickerButtonState = ReadDebouncedButton();
              }
              write2Screen("< Single Train >"," ");
              SoftTriggerScheduled[SelectedChannel-1] = 1;
            } break;
            case 2: { // Single example pulse. Timing for the example pulse is done with micros() instead of the HW timer.
              write2Screen("< Single Pulse >","      ZAP!");
              delayMicroseconds(100000);
              write2Screen("< Single Pulse >"," ");
              PulseTrainDuration_ExamplePulse[SelectedChannel-1] = PulseTrainDuration[SelectedChannel-1];
              PulseTrainDuration[SelectedChannel-1] = Phase1Duration[SelectedChannel-1] + InterPhaseInterval[SelectedChannel-1] + Phase2Duration[SelectedChannel-1];
              SoftTriggerScheduled[SelectedChannel-1] = 1;
            } break;
            case 3: {
              if (ContinuousLoopMode[SelectedChannel-1] == false) {
                 write2Screen("<  Continuous  >","      On");
                 delayMicroseconds(200000); // Debounce
                 ContinuousLoopMode[SelectedChannel-1] = true;
             } else {
                 write2Screen("<  Continuous  >","      Off");
                 ContinuousLoopMode[SelectedChannel-1] = false;
                 killChannel(SelectedChannel-1);
               }
            } break;
            case 4: {
              inMenu = MENU_OUTPUT_CHANNEL;
              SelectedAction = MENU_ACTION_TRIGGER;
              write2Screen("< Trigger Now  >"," ");
            } break;
           }
         } break; 
         case MENU_TRIGGER_CHANNEL: { // Trigger channel menu
          switch (SelectedInputAction) {
            case 1: {
              // Trigger linked output channels
              write2Screen("< Trigger Now >","      ZAP!");
              delayMicroseconds(100000);
              while (ClickerButtonState == 1) {
               ClickerButtonState = ReadDebouncedButton();
              }
              write2Screen("< Trigger Now >"," ");
              for (int x = 0; x < 4; x++) {
                if (TriggerAddress[SelectedChannel-1][x] == 1) {
                  SoftTriggerScheduled[x] = 1;
                }
              }
            } break;
            case 2: {
              // Change mode of selected channel
              TriggerMode[SelectedChannel-1] = ReturnUserValue(TriggerMode[SelectedChannel-1], 0, 2, UNITS_TRIGGER_MODE); // Get user to input trigger mode
              //Store changes
              //SaveCurrentProgram2SD();
            } break;
            case 3: {
              inMenu = MENU_CHANNEL_LIST;
              SelectedAction = MENU_ACTION_TRIGGER; // The output channel menu opens on its first action
              write2Screen("Output Channels","<  Channel 1  >");
              NeedUpdate = 1;
              SelectedChannel = SelectedChannel + 4;
            } break;
          }
        } break;
        case MENU_FILE_LOAD: { // Handle click in file load/save menu
          if (myFilePos < 1) {
            inMenu = MENU_CHANNEL_LIST;
            SelectedChannel = 8;
            write2Screen(" LOAD SETTINGS  ","<Click to load >");
            NeedUpdate = 1;
            myFilePos = 1;
          } else {
            // Load the settings file selected in RefreshFileMenu()
            settingsFile.close();
            settingsFile.open(candidateSettingsFileChar, O_READ);
            validProgram = RestoreParametersFromSD();
            if (validProgram != SETTINGS_FILE_END_MARKER) {
              write2Screen("!ERROR! INVALID ","SETTINGS FILE.");
              delayMicroseconds(1000000);
              LoadDefaultParameters();
            } else {
              outputRestingVoltages();
              strcpy(currentSettingsFileNameChar, candidateSettingsFileChar);
              write2Screen("Settings loaded."," ");
              delayMicroseconds(1000000);
              inMenu = MENU_CHANNEL_LIST;
              SelectedChannel = 8;
              write2Screen(" LOAD SETTINGS  ","<Click to load >");
              NeedUpdate = 1;
            }
          }
        } break;
        case MENU_FILE_SAVE: { // handle click in save menu
          if (myFilePos < 1) {
            inMenu = MENU_CHANNEL_LIST;
            SelectedChannel = 7;
            write2Screen(" SAVE SETTINGS  ","<Click to save >");
            NeedUpdate = 1;
            myFilePos = 1;
          } else {
            // save selected or enter file name creation mode
            boolean nameIsReserved = false;
            if (myFilePos == 1) {
              // Create file name
              CursorPos = 0;
              candidateSettingsFileChar[CursorPos] = 'A';
              candidateSettingsFileChar[CursorPos+1] = '.';
              candidateSettingsFileChar[CursorPos+2] = 'p';
              candidateSettingsFileChar[CursorPos+3] = 'p';
              candidateSettingsFileChar[CursorPos+4] = 's';
              for (int i = 5; i<16; i++) {
                candidateSettingsFileChar[i] = 32;
              }
              LCD_noCursor();
              LCD_setCursor(0, 1); LCD_print("                ");
              delayMicroseconds(100000);
              write2Screen("<Click to save >", candidateSettingsFileChar);
              ChoiceMade = 0;
              CursorOn = 0;
              CursorToggleTimer = 0;
              CursorToggleThreshold = CURSOR_BLINK_CYCLES;
              while (ChoiceMade == 0) {
                 CursorToggleTimer++;
                 if (CursorToggleTimer == CursorToggleThreshold) {
                   switch (CursorOn) {
                     case 0: {LCD_setCursor(CursorPos, 1); LCD_cursor(); CursorOn = 1;} break;
                     case 1: {LCD_noCursor(); CursorOn = 0;} break;
                   }
                   CursorToggleTimer = 0;
                 }
                 ClickerX = analogRead(ClickerXLine);
                 ClickerY = analogRead(ClickerYLine);
                 ClickerButtonState = digitalRead(ClickerButtonLine);
                 if (ClickerButtonState == 0) {
                   ChoiceMade = 1;
                   LCD_noCursor();
                   LCD_setCursor(0, 1); LCD_print("                ");
                 }
                 if (ClickerY > ClickerMaxThreshold) {
                     if (candidateSettingsFileChar[CursorPos]  == 65) { // Skip from ASCii A to 9
                      candidateSettingsFileChar[CursorPos] = 57;
                     } else if (candidateSettingsFileChar[CursorPos]  == 48){ // Wrap from ASCii 0 to underscore
                      candidateSettingsFileChar[CursorPos] = 95;
                     } else if (candidateSettingsFileChar[CursorPos]  == 95){ // Skip from ASCii underscore to Z
                      candidateSettingsFileChar[CursorPos] = 90;
                     } else {
                      candidateSettingsFileChar[CursorPos] = candidateSettingsFileChar[CursorPos] - 1;
                     }
                     LCD_noCursor();
                     write2Screen("<Click to save >", candidateSettingsFileChar);
                     delayMicroseconds(200000); 
                 } else if (ClickerY < ClickerMinThreshold) {
                    if (candidateSettingsFileChar[CursorPos]  == 57) { // Skip from ASCii 9 to A
                      candidateSettingsFileChar[CursorPos] = 65;
                    } else if (candidateSettingsFileChar[CursorPos]  == 90){ // Wrap from ASCii Z to underscore
                      candidateSettingsFileChar[CursorPos] = 95;
                    } else if (candidateSettingsFileChar[CursorPos]  == 95){ // Skip from ASCii underscore to 0
                      candidateSettingsFileChar[CursorPos] = 48;
                    } else {
                    candidateSettingsFileChar[CursorPos] = candidateSettingsFileChar[CursorPos] + 1;
                    }
                    LCD_noCursor();
                    write2Screen("<Click to save >", candidateSettingsFileChar);
                    delayMicroseconds(200000); 
                 } else if (ClickerX < ClickerMinThreshold) {
                    if (CursorPos > 0) {
                      for (int i = CursorPos; i < 16; i++) {
                         candidateSettingsFileChar[i] = candidateSettingsFileChar[i+1];
                      }
                      CursorPos--;
                      write2Screen("<Click to save >", candidateSettingsFileChar);
                      LCD_setCursor(CursorPos, 1); 
                      delayMicroseconds(300000); 
                    }
                 } else if (ClickerX > ClickerMaxThreshold) {
                    if (CursorPos < 11) {
                      for (int i = 16; i > CursorPos; i--) {
                        candidateSettingsFileChar[i] = candidateSettingsFileChar[i-1];
                      }
                      CursorPos++;
                      candidateSettingsFileChar[CursorPos] = 'A';
                      write2Screen("<Click to save >", candidateSettingsFileChar);
                      LCD_setCursor(CursorPos, 1); 
                      delayMicroseconds(300000); 
                    }
                 }
              }
              currentSettingsFileName = "";
              for (int i = 0; i < CursorPos+5; i++) {
                currentSettingsFileName = currentSettingsFileName + candidateSettingsFileChar[i];
              }
              if (isDefaultSettingsFile(currentSettingsFileName.c_str())) {
                nameIsReserved = true;
              } else {
                settingsFile.close();
                currentSettingsFileName.toCharArray(currentSettingsFileNameChar, sizeof(currentSettingsFileNameChar));
              }
            } else { // Overwrite the file selected in RefreshFileMenu()
              strcpy(currentSettingsFileNameChar, candidateSettingsFileChar);
            }
            if (nameIsReserved) {
              write2Screen("Name reserved.", " ");
            } else {
              SaveCurrentProgram2SD();
              write2Screen("Settings saved."," ");
            }
            delayMicroseconds(1000000);
            inMenu = MENU_CHANNEL_LIST;
            SelectedChannel = 7;
            write2Screen(" SAVE SETTINGS  ","<Click to save >");
            NeedUpdate = 1;
            myFilePos = 2;
          }
        } break;
        case MENU_FILE_DELETE: { // Handle click in delete menu
          if (myFilePos < 1) {
            inMenu = MENU_CHANNEL_LIST;
            SelectedChannel = 9;
            write2Screen(" ERASE SETTINGS ","<Click to erase>");
            NeedUpdate = 1;
            myFilePos = 1;
          } else {
            if (!isDefaultSettingsFile(candidateSettingsFileChar)) { // Selected in RefreshFileMenu(), which does not list the default file
              sd.remove(candidateSettingsFileChar);
            }
            write2Screen("Settings erased."," ");
            delayMicroseconds(1000000);
            inMenu = MENU_CHANNEL_LIST;
            SelectedChannel = 9;
            write2Screen(" ERASE SETTINGS ","<Click to erase>");
            NeedUpdate = 1;
          }
        } break;
     }
    }
    if (ClickerButtonState == 0 && LastClickerButtonState == 1) {
      LastClickerButtonState = 0;
    }
    if (LastClickerXState != 1 && ClickerX < 200) {
      LastClickerXState = 1;
      NeedUpdate = 1;
      if (inMenu == MENU_CHANNEL_LIST) {SelectedChannel = SelectedChannel - 1;}
      if (inMenu == MENU_OUTPUT_CHANNEL) {scrollOutputAction(-1);}
      if (inMenu == MENU_OUTPUT_TRIGGER) {SelectedStimMode = SelectedStimMode - 1;}
      if (inMenu == MENU_TRIGGER_CHANNEL) {SelectedInputAction = SelectedInputAction - 1;}
      if ((inMenu >= MENU_FILE_LOAD) && (inMenu <= MENU_FILE_DELETE)) {
        if (myFilePos > 0) {myFilePos = myFilePos - 1;}
      }
      if (SelectedInputAction == 0) {SelectedInputAction = 3;}
      if (SelectedChannel == 0) {SelectedChannel = 12;}
      if (SelectedStimMode == 0) {SelectedStimMode = 4;}
    }
    if (LastClickerXState != 2 && ClickerX > ClickerMaxThreshold) {
      LastClickerXState = 2;
      NeedUpdate = 1;
      if (inMenu == MENU_CHANNEL_LIST) {SelectedChannel++;}
      if (inMenu == MENU_OUTPUT_CHANNEL) {scrollOutputAction(1);}
      if (inMenu == MENU_OUTPUT_TRIGGER) {SelectedStimMode++;}
      if (inMenu == MENU_TRIGGER_CHANNEL) {SelectedInputAction++;}
      if ((inMenu >= MENU_FILE_LOAD) && (inMenu <= MENU_FILE_DELETE)) {
        myFilePos++;
      }
      if (SelectedInputAction == 4) {SelectedInputAction = 1;}
      if (SelectedChannel == 13) {SelectedChannel = 1;}
      if (SelectedStimMode == 5) {SelectedStimMode = 1;}
    }
    if (LastClickerXState != 0 && ClickerX < ClickerMaxThreshold && ClickerX > ClickerMinThreshold) {
      LastClickerXState = 0;
    }
    if (NeedUpdate == 1) {
      switch (inMenu) {
        case MENU_CHANNEL_LIST: {
          RefreshChannelMenu(SelectedChannel);
        } break;
        case MENU_OUTPUT_CHANNEL: {
          RefreshActionMenu(SelectedAction);
        } break; 
        case MENU_OUTPUT_TRIGGER: {
          switch (SelectedStimMode) {
            case 1: {write2Screen("< Single Train >", " ");} break;
            case 2: {write2Screen("< Single Pulse >", " ");} break;
            case 3: {
            if (ContinuousLoopMode[SelectedChannel-1] == false) {
                 write2Screen("<  Continuous  >","      Off");
               } else {
                 write2Screen("<  Continuous  >","      On");
               }
            } break;
            case 4: {write2Screen("<     Exit     >"," ");} break;
          }
        } break;
        case MENU_TRIGGER_CHANNEL: {
          RefreshTriggerMenu(SelectedInputAction); 
        } break;
        case MENU_FILE_LOAD:
        case MENU_FILE_SAVE:
        case MENU_FILE_DELETE: {
          RefreshFileMenu();
        } break;
    }
    NeedUpdate = 0;
  }

}

// Returns the parameter that an output channel menu action edits. See the parameter table in PulsePal3.ino.
const OutputParam& menuActionParam(int action) {
  return outputParams[menuActionParams[action - MENU_ACTION_FIRST_PARAM] - 1];
}

// Reads one output channel parameter (channel 0-3) through the parameter table
uint32_t getOutputParam(const OutputParam &param, byte channel) {
  switch (param.type) {
    case PARAM_TYPE_BYTE: return ((byte*)param.values)[channel];
    case PARAM_TYPE_UINT16: return ((uint16_t*)param.values)[channel];
  }
  return ((uint32_t*)param.values)[channel];
}

// Writes one output channel parameter (channel 0-3) through the parameter table
void setOutputParam(const OutputParam &param, byte channel, uint32_t value) {
  switch (param.type) {
    case PARAM_TYPE_BYTE: {((byte*)param.values)[channel] = (byte)value;} break;
    case PARAM_TYPE_UINT16: {((uint16_t*)param.values)[channel] = (uint16_t)value;} break;
    default: {((uint32_t*)param.values)[channel] = value;} break;
  }
}

// Moves the selection in the output channel menu by one step, wrapping at the ends and skipping the parameters
// that do not apply to a monophasic channel
void scrollOutputAction(int8_t direction) {
  for (byte step = 0; step < MENU_ACTION_EXIT; step++) { // Always ends: at worst it returns to where it started
    SelectedAction += direction;
    if (SelectedAction < MENU_ACTION_TRIGGER) {SelectedAction = MENU_ACTION_EXIT;}
    if (SelectedAction > MENU_ACTION_EXIT) {SelectedAction = MENU_ACTION_TRIGGER;}
    if ((SelectedAction == MENU_ACTION_TRIGGER) || (SelectedAction == MENU_ACTION_EXIT)) {return;}
    if (!menuActionParam(SelectedAction).biphasicOnly || IsBiphasic[SelectedChannel-1]) {return;}
  }
}

void centerText(char myText[]) {
  byte spaceCounter = 0;
  for (int i = 0; i < 16; i++) {
    if (myText[i] == 0) {spaceCounter++;}
    tempText[i] = 32;
  }
  fileNameOffset = spaceCounter/2;
    for (int i = fileNameOffset; i < 16; i++) {
      tempText[i] = myText[i-fileNameOffset];
    }
    for (int i = 0; i < 16; i++) {
      centeredText[i] = tempText[i];
    }
}

void RefreshChannelMenu(int ThisChannel) {
  switch (SelectedChannel) {
        case 1: {write2Screen("Output Channels","<  Channel 1  >");} break;
        case 2: {write2Screen("Output Channels","<  Channel 2  >");} break;
        case 3: {write2Screen("Output Channels","<  Channel 3  >");} break;
        case 4: {write2Screen("Output Channels","<  Channel 4  >");} break;
        case 5: {write2Screen("Trigger Channels","<  Channel 1  >");} break;
        case 6: {write2Screen("Trigger Channels","<  Channel 2  >");} break;
        case 7: {write2Screen(" SAVE SETTINGS  ","< Select File >");} break;
        case 8: {write2Screen(" LOAD SETTINGS  ","< Select File >");} break;
        case 9: {write2Screen(" ERASE SETTINGS ","< Select File >");} break;
        case 10: {write2Screen("  Device Info  ","<Click to view>");} break;
        case 11: {write2Screen("    -RESET-       ","<Click to reset>");} break;
        case 12: {write2Screen("<Click to exit>"," ");} break;
  }
}
void RefreshActionMenu(int ThisAction) {
  if (SelectedAction == MENU_ACTION_TRIGGER) {
    write2Screen("< Trigger Now  >"," ");
  } else if (SelectedAction == MENU_ACTION_EXIT) {
    write2Screen("<     Exit     >"," ");
  } else { // Label and value of the parameter this action edits, from the table in PulsePal3.ino
    const OutputParam &param = menuActionParam(SelectedAction);
    write2Screen(param.label, FormatNumberForDisplay(getOutputParam(param, SelectedChannel-1), param.units));
  }
  isNegativeZero = 0;
}
void RefreshTriggerMenu(int ThisAction) {
    switch (SelectedInputAction) {
          case 1: {write2Screen("< Trigger Now  >"," ");} break;
          case 2: {write2Screen("< Trigger Mode >",FormatNumberForDisplay(TriggerMode[SelectedChannel-1], UNITS_TRIGGER_MODE));} break;
          case 3: {write2Screen("<     Exit     >"," ");} break;
     }
}

// Draws the file load, save or erase menu (depending on inMenu) at position myFilePos. See the menu map above.
// The name of the file shown is left in candidateSettingsFileChar, for the click handler.
// If myFilePos is past the end of the file list, it steps back to the last file (or to the option before the list).
void RefreshFileMenu() {
  const char* header = "<Click to load >";
  uint16_t firstFilePos = 1; // Menu position of the first file in the list
  bool includeDefault = false;
  switch (inMenu) {
    case MENU_FILE_LOAD: {header = "<Click to load >"; includeDefault = true;} break;
    case MENU_FILE_SAVE: {header = "<Click to save >"; firstFilePos = 2;} break;
    case MENU_FILE_DELETE: {header = "<Click to erase>";} break;
  }
  if (myFilePos >= firstFilePos) {
    if (!findListedFile(myFilePos - firstFilePos + 1, includeDefault)) {
      myFilePos--;
    }
    if ((myFilePos >= firstFilePos) && findListedFile(myFilePos - firstFilePos + 1, includeDefault)) {
      centerText(candidateSettingsFileChar);
      write2Screen(header, centeredText);
      return;
    }
  }
  memset(candidateSettingsFileChar, 0, sizeof(candidateSettingsFileChar));
  if (myFilePos == 0) {
    write2Screen("<    Cancel    >", " ");
  } else {
    write2Screen("<   New File   >", ""); // Position 1 of the save menu
  }
}

// Shows the message for playback stopped with the joystick button, and returns to the menu. Called from loop()
// after handler() has stopped playback and set abortRequested.
void ShowAbortMessage() {
  write2Screen("   PULSE TRAIN","   TERMINATED");
  delayMicroseconds(1500000);
  if (inMenu == MENU_TOP) {
    write2Screen(CommanderString," Click for menu");
  } else {
    inMenu = MENU_CHANNEL_LIST;
    RefreshChannelMenu(SelectedChannel);
  }
}

boolean ReadDebouncedButton() {
  DebounceTime = millis();
  ClickerButtonState = digitalRead(ClickerButtonLine);
    if (ClickerButtonState != lastButtonState) {lastDebounceTime = DebounceTime;}
    lastButtonState = ClickerButtonState;
   if (((DebounceTime - lastDebounceTime) > 75) && (ClickerButtonState == 0)) {
      return 1;
   } else {
     return 0;
   }
}

const char* FormatNumberForDisplay(unsigned int InputNumber, int Units) {
  // Units: see enum DisplayUnits. Times are shown with leading zeros when inMenu is MENU_OUTPUT_TRIGGER,
  // which ReturnUserValue() sets temporarily while a value is being edited.
  // Clear var
  for (int x = 0; x < 17; x++) {
    Value2Display[x] = ' ';
  }
  // Figure out how many digits
double InputNum = double(InputNumber);
  if (Units == UNITS_TIME) {
  InputNum = InputNum/CycleFrequency;
  }
if (Units == UNITS_VOLTS) {
  // Convert volts from bytes to volts
  InputNum = (((InputNum/65536)*10)*2 - 10);
}
  switch (Units) {
    case UNITS_INDEX: {sprintf (Value2Display, "       %.0f", InputNum);} break;
    case UNITS_TIME: {
      if (inMenu == MENU_OUTPUT_TRIGGER) {
        sprintf (Value2Display, "  %010.5f s ", InputNum);
      } else {
        if (InputNum < 100) {
          sprintf (Value2Display, "    %.5f s ", InputNum);
        } else {
          sprintf (Value2Display, "   %.5f s ", InputNum);
        }
      }
    } break;
    case UNITS_VOLTS: {
        if (InputNum == 0) {
          if (isNegativeZero) {
            InputNum = InputNum-0.000001;
          }
        }
        if (InputNum >= 0) {
          sprintf (Value2Display, "     %04.2f V ", InputNum);
        } else {
          sprintf (Value2Display, "    %05.2f V ", InputNum);
        }
    } break;
    case UNITS_OFF_ON:{
      if (InputNum == 0) {
        sprintf(Value2Display, "      Off");
      } else if (InputNum == 1) {
        sprintf(Value2Display, "       On");
      } else {
        sprintf(Value2Display, "Error");
      }
    } break;
    case UNITS_PULSES_BURSTS: {
      if (InputNum == 0) {
        sprintf(Value2Display, "     Pulses");
      } else if (InputNum == 1) {
        sprintf(Value2Display, "     Bursts");
      } else {
        sprintf(Value2Display, "     Error");
      }
    } break;
    case UNITS_TRIGGER_MODE: {
      if (InputNum == 0) {
        sprintf(Value2Display, "     Normal   ");
      } else if (InputNum == 1) {
        sprintf(Value2Display, "     Toggle   ");
      } else if (InputNum == 2) {
        sprintf(Value2Display, "  Pulse Gated  ");
      } else {
        sprintf(Value2Display, "     Error   ");
      }
    } break;
  }
  return Value2Display;
}

// Places the screen cursor under the digit being edited in ReturnUserValue(). On Pulse Pal 3 the value is centered,
// so a minus sign shifts the digits right by one character.
void placeEditCursor() {
  #if (HARDWARE_VERSION < 3)
    const uint8_t negSignOffset = 0;
  #else
    const uint8_t negSignOffset = 1;
  #endif
  if (Digits[0] < 0 || isNegativeZero) {
    LCD_setCursor(ValidCursorPositions[CursorPos] + negSignOffset, 1);
  } else {
    LCD_setCursor(ValidCursorPositions[CursorPos], 1);
  }
}

// Redraws the value being edited in ReturnUserValue() on the second line of the screen, with the cursor under the digit being edited
void redrawEditValue(byte Units) {
  LCD_noCursor();
  #if (HARDWARE_VERSION == 3)
    LCD_setCursor(0, 1);
    LCD_print_no_trim_no_render("                ");
  #endif
  LCD_setCursor(0, 1);
  LCD_print(FormatNumberForDisplay(UserValue, Units));
  placeEditCursor();
  LCD_cursor();
  CursorOn = 1;
}

// Returns the voltage set by the digits being edited in ReturnUserValue(), and updates isNegativeZero.
// Digits[0] is the ones digit (255 codes for -0), Digits[1] is tenths and Digits[2] is hundredths.
float digitsToVolts() {
  float volts = 0;
  if (Digits[0] != 255) {
    volts = volts + ((float)Digits[0]);
  }
  if ((Digits[0] < 0) || (Digits[0] == 255)) {
    volts = volts - ((float)Digits[1]*0.1);
    volts = volts - ((float)Digits[2]*0.01);
  } else {
    volts = volts + ((float)Digits[1]*0.1);
    volts = volts + ((float)Digits[2]*0.01);
  }
  if (((Digits[0] == 255) && (volts == 0)) || (volts < 0)) {
    isNegativeZero = 1;
  } else {
    isNegativeZero = 0;
  }
  return volts;
}

unsigned int ReturnUserValue(unsigned int startValue, unsigned long LowerLimit, unsigned long UpperLimit, byte Units) {
      // This function returns a value that the user chooses by scrolling up and down a number list with the joystick, and clicks to select the desired number.
      // Editing starts at startValue. LowerLimit and UpperLimit are the limits for this selection. Units: see enum DisplayUnits.
      // This function blocks until the joystick is clicked. It sets inMenu to MENU_OUTPUT_TRIGGER while editing (so times are shown with leading zeros),
      // and to MENU_TRIGGER_CHANNEL or MENU_OUTPUT_CHANNEL on return.
     unsigned long ValueToAdd = 0;
     CursorPos = 0;
     isNegativeZero = 0;
     for (int i = 0; i < 9; i++) {
       Digits[i] = 0;
       ValidCursorPositions[i] = 0;
     }
      float CandidateVoltage = 0; // used to see if voltage will go over limits for DAC
      
     UserValue = startValue;
     long UVTemp = UserValue;
     inMenu = MENU_OUTPUT_TRIGGER; // Temporarily goes a menu layer deeper so leading zeros are displayed by FormatNumberForDisplay
     LCD_setCursor(0, 1); LCD_print_no_trim_no_render("                ");
     delayMicroseconds(100000);
     LCD_setCursor(0, 1); LCD_print(FormatNumberForDisplay(UserValue, Units));
     ChoiceMade = 0;
    // Read digits from User Value
    int x = 0;
    if (Units == UNITS_TIME) {
      UVTemp = UVTemp / 2;
      while (UVTemp > 0) {
        Digits[7-x] = (UVTemp % 10);
        UVTemp = UVTemp / 10;
        x++;
      }
    }
    if (Units == UNITS_VOLTS) {
      UVTemp = round(((((float)UVTemp/DACBits)*20) - 10)*100);
      if (UVTemp < 0) {isNegativeZero = 1;}
      Digits[2] = (UVTemp % 10);
      UVTemp = UVTemp/10;
      Digits[1] = (UVTemp % 10);
      UVTemp = UVTemp/10;
      Digits[0] = UVTemp;
      if (isNegativeZero && (Digits[0] == 0)) {Digits[0] = 255;} // 255 codes for -0, required because interface changes value by digit
      if (Digits[1] < 0) {Digits[1] = Digits[1]*-1;}
      if (Digits[2] < 0) {Digits[2] = Digits[2]*-1;}
    }
    
     // Assign valid cursor positions by unit type
     #if (HARDWARE_VERSION < 3)
      switch(Units) {
        case UNITS_INDEX: {ValidCursorPositions[0] = 7;} break;
        case UNITS_TIME: {ValidCursorPositions[0] = 2; ValidCursorPositions[1] = 3; ValidCursorPositions[2] = 4; ValidCursorPositions[3] = 5; ValidCursorPositions[4] = 7; ValidCursorPositions[5] = 8; ValidCursorPositions[6] = 9; ValidCursorPositions[7] = 10; ValidCursorPositions[8] = 11;} break;
        case UNITS_VOLTS: {ValidCursorPositions[0] = 5; ValidCursorPositions[1] = 7; ValidCursorPositions[2] = 8;} break;
        case UNITS_OFF_ON: {ValidCursorPositions[0] = 7;} break;
        case UNITS_PULSES_BURSTS: {ValidCursorPositions[0] = 7;} break;
        case UNITS_TRIGGER_MODE: {ValidCursorPositions[0] = 7;} break;
      }
     #else
      switch(Units) {
        case UNITS_INDEX: {ValidCursorPositions[0] = 0;} break;
        case UNITS_TIME: {ValidCursorPositions[0] = 0; ValidCursorPositions[1] = 1; ValidCursorPositions[2] = 2; ValidCursorPositions[3] = 3; ValidCursorPositions[4] = 5; ValidCursorPositions[5] = 6; ValidCursorPositions[6] = 7; ValidCursorPositions[7] = 8; ValidCursorPositions[8] = 9;} break;
        case UNITS_VOLTS: {ValidCursorPositions[0] = 0; ValidCursorPositions[1] = 2; ValidCursorPositions[2] = 3;} break;
        case UNITS_OFF_ON: {ValidCursorPositions[0] = 0;} break;
        case UNITS_PULSES_BURSTS: {ValidCursorPositions[0] = 0;} break;
        case UNITS_TRIGGER_MODE: {ValidCursorPositions[0] = 0;} break;
      }
     #endif
     // Initialize cursor starting positions and limits by unit type
     switch (Units) {
       case UNITS_INDEX: {CursorPos = 0; CursorPosLeftLimit = 0; CursorPosRightLimit = 0;} break; // Format for Index
       case UNITS_TIME: {CursorPos = 3; CursorPosLeftLimit = 0; CursorPosRightLimit = 7;} break; // Format for seconds
       case UNITS_VOLTS: {
        if (abs(Digits[0]) == 10) {
          CursorPos = 0; CursorPosLeftLimit = 0; CursorPosRightLimit = 0;
        } else {
          CursorPos = 0; CursorPosLeftLimit = 0; CursorPosRightLimit = 2;
        }
        } break; // Format for volts
       case UNITS_OFF_ON: {CursorPos = 0; CursorPosLeftLimit = 0; CursorPosRightLimit = 0;} break; // Format for Off/On
       case UNITS_PULSES_BURSTS: {CursorPos = 0; CursorPosLeftLimit = 0; CursorPosRightLimit = 0;} break; // Format for Pulses/Bursts
       case UNITS_TRIGGER_MODE: {CursorPos = 0; CursorPosLeftLimit = 0; CursorPosRightLimit = 0;} break; // Format for trigger mode
       }
      CursorToggleTimer = 0;
      CursorOn = 1;   // Cursor starts visible
      CursorToggleThreshold = CURSOR_BLINK_CYCLES;

      // Show cursor immediately on entry
      placeEditCursor();

      LCD_cursor();

      delayMicroseconds(75000);
     while (ChoiceMade == 0) {
       CursorToggleTimer++;
       if (CursorToggleTimer == CursorToggleThreshold) {
         switch (CursorOn) {
           case 0: {
            placeEditCursor();
            LCD_cursor(); CursorOn = 1;
            } break;
           case 1: {
            LCD_noCursor(); CursorOn = 0;
            } break;
         }
         CursorToggleTimer = 0;
       }
       ClickerX = analogRead(ClickerXLine);
       ClickerY = analogRead(ClickerYLine);
       ClickerButtonState = digitalRead(ClickerButtonLine);
       if (ClickerButtonState == 0) {
         ChoiceMade = 1;
       }       
       if (ClickerY < ClickerMinThreshold) {
          switch(Units) {
            case UNITS_INDEX: {
              if (UserValue < UpperLimit) {
                UserValue = UserValue + 1;
              }
            } break;
            case UNITS_TIME: {
                ValueToAdd = 2*(pow(10, ((5-CursorPos)+2)));
                if ((Digits[CursorPos] < 9) && ((UserValue+ValueToAdd) <= UpperLimit)) {
                 UserValue = UserValue + ValueToAdd;
                 Digits[CursorPos] = Digits[CursorPos] + 1;
                }
            } break;
            case UNITS_VOLTS: {
                if (((CursorPos > 0) && (Digits[CursorPos] < 9)) || ((CursorPos == 0) && ((Digits[0] < 10) || (Digits[0] == 255)))) {
                    if (UserValue < DACBits) {
                      if (Digits[CursorPos] == 255) {Digits[CursorPos] = 0;}
                      else if (Digits[CursorPos] == -1) {Digits[CursorPos] = 255;}
                      else {Digits[CursorPos] = Digits[CursorPos] + 1;}
                      if (abs(Digits[0]) == 10) {
                        CursorPos = 0; CursorPosLeftLimit = 0; CursorPosRightLimit = 0;
                      } else {
                        CursorPosLeftLimit = 0; CursorPosRightLimit = 2;
                      }
                      CandidateVoltage = digitsToVolts(); // Also updates isNegativeZero
                      
                      if (CandidateVoltage > 10) {
                        Digits[CursorPos] = Digits[CursorPos] - 1;
                      } else if (CandidateVoltage == 10) {
                        UserValue = 65535; // Top of DAC range (0-65535; 65536 is out of range)
                      } else {
                        CandidateVoltage = ((CandidateVoltage+10)/20)*DACBits;
                        UserValue = (unsigned int)CandidateVoltage;
                      }
                      delayMicroseconds(1000);
                    }
                } 
            } break;
            default: {
              if (UserValue < UpperLimit) {
                UserValue = UserValue + 1;
              }
            } break;
          }
          ScrollSpeedDelay = 200000;
          redrawEditValue(Units);
       }
      else if (ClickerY > ClickerMaxThreshold) {
         switch(Units) {
            case UNITS_INDEX: {
              if (UserValue > LowerLimit) {
                UserValue = UserValue - 1;
              }
            } break;
            case UNITS_TIME: {
                if (Digits[CursorPos] > 0)  {
                 UserValue = UserValue - 2*(pow(10, ((5-CursorPos)+2)));
                  Digits[CursorPos] = Digits[CursorPos] - 1;
                }
            } break;
            case UNITS_VOLTS: {
              if (((CursorPos > 0) && (Digits[CursorPos] > 0)) || ((CursorPos == 0) && ((Digits[0] > -10) || (Digits[0] == 255)))) {
                    if (UserValue > 0) {
                      if (Digits[CursorPos] == 255) {Digits[CursorPos] = -1;}
                      else if (Digits[CursorPos] == 0) {Digits[CursorPos] = 255;}
                      else {Digits[CursorPos] = Digits[CursorPos] - 1;}
                      if (abs(Digits[0]) == 10) {
                        CursorPos = 0; CursorPosLeftLimit = 0; CursorPosRightLimit = 0;
                      } else {
                        CursorPosLeftLimit = 0; CursorPosRightLimit = 2;
                      }
                      CandidateVoltage = digitsToVolts(); // Also updates isNegativeZero
                      CandidateVoltage = ((CandidateVoltage+10)/20)*DACBits;
                      UserValue = (unsigned int)CandidateVoltage;
                      delayMicroseconds(1000);
//                  }
                  }
                } 
            } break;
            default: {
              if (UserValue > LowerLimit) {
                UserValue = UserValue - 1;
              }
            } break;
          }
          ScrollSpeedDelay = 200000;
          redrawEditValue(Units);


       } else {
         ScrollSpeedDelay = 0;
       }
       if ((ClickerX > ClickerMaxThreshold) && (CursorPos < CursorPosRightLimit)) {
         CursorPos = CursorPos + 1;
         ScrollSpeedDelay = 200000;
         redrawEditValue(Units);
       }
       if ((ClickerX < ClickerMinThreshold) && (CursorPos > CursorPosLeftLimit)) {
         CursorPos = CursorPos - 1;
         ScrollSpeedDelay = 200000;
         redrawEditValue(Units);
       }
     delayMicroseconds(ScrollSpeedDelay);  
     }
     LCD_noCursor();
     LCD_setCursor(0, 1); 
     LCD_print_no_trim_no_render("                ");
     if (Units == UNITS_TRIGGER_MODE) {
       inMenu = MENU_TRIGGER_CHANNEL;
     } else {
       inMenu = MENU_OUTPUT_CHANNEL;
     }
     delayMicroseconds(200000);
     LCD_setCursor(0, 1); LCD_print(FormatNumberForDisplay(UserValue, Units));
     //LCD_noCursor();
     return UserValue;
} 

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


// Thumb joystick menu. UpdateMenu() is called from loop() on every pass, also during playback, so it must never wait:
// each call reads the joystick once, acts on a click or a move, and redraws the screen only when something changed.
// A wait here would delay refilling the playback buffers (see Storage.ino).
//
// Functions in this file:
//   UpdateMenu()
//   onMenuClick()
//   scrollMenu()
//   channelMenuState()
//   RefreshMenuList()
//   ReadDebouncedButton()
//
// ---------------------------------------------------------------------------------------------------------------
// Thumb joystick menu map. Joystick left/right scrolls through the options at the current level (inMenu), wrapping at
// the ends, and a click selects.
//
// MENU_TOP          "Wave Pal v3.0" / "Click for menu". Click -> MENU_LIST, at channel 1
// MENU_LIST         menuItem  1-4  Output channels 1-4. A click plays the channel's waveform, or stops it while it
//                                  plays; the second line says which, or "No waveform". The trigger mode does not
//                                  apply here.
//                             5    Device info -> MENU_DEVICE_INFO (MENU_ITEM_DEVICE_INFO)
//                             6    Reboot (MENU_ITEM_REBOOT)
//                             7    Exit -> MENU_TOP (MENU_ITEM_EXIT)
// MENU_DEVICE_INFO  Hardware and firmware versions. Click -> MENU_LIST, at item 5
//
// To add an option to MENU_LIST: add it to enum MenuItem in WavePal.ino (the last item must stay MENU_ITEM_EXIT, which
// the scroll wraps at), then add its click in onMenuClick() and its screen in RefreshMenuList().
// ---------------------------------------------------------------------------------------------------------------

void UpdateMenu() {
  ClickerX = analogRead(ClickerXLine);
  ClickerButtonState = ReadDebouncedButton();
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
  if ((inMenu == MENU_LIST) && (menuItem <= N_CHANNELS) && (channelMenuState(menuItem - 1) != shownChannelState)) {
    RefreshMenuList(); // The channel shown started or stopped playing, e.g. from a trigger or at the end of its waveform
  }
}

void onMenuClick() {
  switch (inMenu) {
    case MENU_TOP: {
      inMenu = MENU_LIST;
      menuItem = 1;
      RefreshMenuList();
    } break;
    case MENU_LIST: {
      if (menuItem <= N_CHANNELS) { // Play or stop an output channel
        byte channel = menuItem - 1;
        noInterrupts();
        if (playing[channel] && !stopAfterWrite[channel]) {
          stopChannels(bit(channel));
        } else {
          startChannels(bit(channel)); // Does nothing if the channel has no waveform
        }
        interrupts();
        RefreshMenuList();
      } else if (menuItem == MENU_ITEM_DEVICE_INFO) {
        inMenu = MENU_DEVICE_INFO;
        write2Screen("Hardware v" TOSTRING(HARDWARE_VERSION), "Firmware v" TOSTRING(FIRMWARE_VERSION));
      } else if (menuItem == MENU_ITEM_REBOOT) {
        noInterrupts();
        triggersEnabled = false;
        stopChannels(ALL_CHANNELS);
        interrupts();
        write2Screen(" ", " ");
        delay(1000);
        Software_Reset();
      } else { // MENU_ITEM_EXIT
        showTopScreen();
      }
    } break;
    case MENU_DEVICE_INFO: {
      inMenu = MENU_LIST; // Back to the device info item
      RefreshMenuList();
    } break;
  }
}

// Moves the selection in MENU_LIST by one item (direction -1 or 1), wrapping at the ends
void scrollMenu(int8_t direction) {
  if (inMenu != MENU_LIST) {
    return;
  }
  if ((direction < 0) && (menuItem == 1)) {
    menuItem = MENU_ITEM_EXIT;
  } else if ((direction > 0) && (menuItem == MENU_ITEM_EXIT)) {
    menuItem = 1;
  } else {
    menuItem += direction;
  }
  RefreshMenuList();
}

// What a channel's menu item shows: 0 = no waveform, 1 = can be played, 2 = playing
int32_t channelMenuState(byte channel) {
  if (nSamples[channel] == 0) {
    return 0;
  }
  return (playing[channel] && !stopAfterWrite[channel]) ? 2 : 1;
}

// Draws the selected MENU_LIST item. See the menu map above.
void RefreshMenuList() {
  static const char* channelTitles[N_CHANNELS] = {"<  Channel 1   >", "<  Channel 2   >", "<  Channel 3   >", "<  Channel 4   >"};
  static const char* channelActions[3] = {"No waveform", "Click to play", "Click to stop"}; // Indexed by channelMenuState()
  shownChannelState = -1;
  switch (menuItem) {
    case MENU_ITEM_DEVICE_INFO: {write2Screen("< Device Info  >", "Click to view");} break;
    case MENU_ITEM_REBOOT: {write2Screen("<    Reboot    >", " ");} break;
    case MENU_ITEM_EXIT: {write2Screen("<     Exit     >", " ");} break;
    default: { // Output channels 1-4
      shownChannelState = channelMenuState(menuItem - 1);
      write2Screen(channelTitles[menuItem - 1], channelActions[shownChannelState]);
    } break;
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

#pragma once

#include <string.h>
#include <stdio.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "config.h"

// clkAfter = 400 kHz: o default da Adafruit restaura 100 kHz e atrasa a matriz.
static Adafruit_SSD1306 oled(OLED_WIDTH, OLED_HEIGHT, &Wire, -1, 400000UL, 400000UL);
static bool oledOk = false;
static bool oledDirty = true;
static uint32_t lastOledDrawMs = 0;

struct OledStatus {
  int8_t octave;
  uint8_t volume;
  uint8_t program;
  uint8_t typedDigits;
  uint8_t typedProgram;
  bool sustain;
  bool usbMounted;
  bool mcpOk;
  uint8_t perfBank;  // 0..7
  uint8_t padBank;   // 0..7
  bool selHeld;
  bool bankFocusPad;  // which bank box to emphasize
  bool bankSelectActive;
  bool clockRunning;
  bool transportPlaying;
  uint8_t beat;  // 0..3
  uint16_t bpm;  // 0 = unknown
  uint32_t playSeconds;
  const char *trackName;   // DAW page / track
  const char *chordName;   // accumulated chord stack
};

static void oledMark() {
  oledDirty = true;
}

static void oledDrawBankBox(int16_t x, int16_t y, char kind, uint8_t bank1to8, bool emphasize) {
  char buf[4];
  snprintf(buf, sizeof(buf), "%c%u", kind, static_cast<unsigned>(bank1to8));
  const int16_t w = 18;
  const int16_t h = 10;
  if (emphasize) {
    oled.fillRect(x, y, w, h, SSD1306_WHITE);
    oled.setTextColor(SSD1306_BLACK);
  } else {
    oled.drawRect(x, y, w, h, SSD1306_WHITE);
    oled.setTextColor(SSD1306_WHITE);
  }
  oled.setCursor(static_cast<int16_t>(x + 2), static_cast<int16_t>(y + 1));
  oled.print(buf);
  oled.setTextColor(SSD1306_WHITE);
}

static void oledDraw(const OledStatus &st) {
  if (!oledOk) {
    return;
  }

  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);

  if (!st.mcpOk) {
    oled.setCursor(0, 0);
    oled.print(F("MCP 0x"));
    oled.print(MCP_ADDR, HEX);
    oled.println(F(" falhou"));
    oled.print(F("RESET=3V3 A0-2=GND"));
    oled.display();
    return;
  }

  if (!st.usbMounted) {
    oled.setCursor(0, 4);
    oled.println(F("Casio SA-1 MIDI"));
    oled.print(F("aguardando USB"));
    oled.display();
    return;
  }

  // Overlay: program digit entry
  if (st.typedDigits == 1) {
    oled.setCursor(0, 0);
    oled.print(F("PGM "));
    oled.print(st.typedProgram / 10);
    oled.print('_');
    oled.setCursor(0, 16);
    oled.print(F("O"));
    if (st.octave >= 0) {
      oled.print('+');
    }
    oled.print(st.octave);
    oled.print(F("  vol "));
    oled.print(st.volume);
    oled.display();
    return;
  }

  // Line 0: [P#][B#] — both filled at rest; SEL/select fills only the focus.
  bool empP = true;
  bool empB = true;
  if (st.selHeld) {
    empP = false;
    empB = true;
  } else if (st.bankSelectActive) {
    empP = !st.bankFocusPad;
    empB = st.bankFocusPad;
  }
  oledDrawBankBox(0, 0, 'P', static_cast<uint8_t>(st.perfBank + 1), empP);
  oledDrawBankBox(20, 0, 'B', static_cast<uint8_t>(st.padBank + 1), empB);

  const char *track = st.trackName ? st.trackName : "--";
  char tshow[11];
  {
    const size_t tlen = strlen(track);
    if (tlen <= 10) {
      strncpy(tshow, track, sizeof(tshow));
      tshow[10] = '\0';
    } else {
      memcpy(tshow, track, 7);
      tshow[7] = '.';
      tshow[8] = '.';
      tshow[9] = '.';
      tshow[10] = '\0';
    }
  }
  oled.setTextColor(SSD1306_WHITE);
  oled.setCursor(42, 1);
  oled.print(tshow);

  // Line 1: chord stack (size 2 if short)
  const char *chord = (st.chordName && st.chordName[0]) ? st.chordName : "";
  const uint8_t clen = static_cast<uint8_t>(strlen(chord));
  if (clen > 0 && clen <= 5) {
    oled.setTextSize(2);
    oled.setCursor(0, 12);
    oled.print(chord);
    oled.setTextSize(1);
  } else if (clen > 0) {
    oled.setCursor(0, 12);
    oled.print(chord);
  } else {
    oled.setCursor(0, 12);
    oled.print(F("-"));
  }

  // Line 2: BPM + playhead mm:ss (bottom)
  char left[8];
  if (st.bpm > 0) {
    snprintf(left, sizeof(left), "%u", static_cast<unsigned>(st.bpm > 999 ? 999 : st.bpm));
  } else {
    snprintf(left, sizeof(left), "--");
  }
  const uint32_t sec = st.playSeconds;
  const uint32_t mm = sec / 60UL;
  const uint32_t ss = sec % 60UL;
  char right[8];
  if (mm < 10) {
    snprintf(right, sizeof(right), "%lu:%02lu", static_cast<unsigned long>(mm),
             static_cast<unsigned long>(ss));
  } else {
    snprintf(right, sizeof(right), "%lu:%02lu", static_cast<unsigned long>(mm),
             static_cast<unsigned long>(ss));
  }

  oled.setCursor(0, 24);
  oled.print(left);
  if (st.transportPlaying || st.clockRunning) {
    oled.print(F(" >"));
  }
  const int16_t rw = static_cast<int16_t>(strlen(right) * 6);
  oled.setCursor(static_cast<int16_t>(128 - rw), 24);
  oled.print(right);

  if (st.sustain) {
    oled.fillRect(100, 12, 16, 8, SSD1306_WHITE);
    oled.setTextColor(SSD1306_BLACK);
    oled.setCursor(104, 12);
    oled.print(F("S"));
    oled.setTextColor(SSD1306_WHITE);
  }

  oled.display();
}

static bool setupOled() {
  oledOk = oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  if (!oledOk) {
    return false;
  }
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);
  oled.setCursor(0, 4);
  oled.println(F("Casio SA-1 MIDI"));
  oled.print(F("OLED 0.91  128x32"));
  oled.display();
  oledDirty = true;
  return true;
}

static void oledTick(const OledStatus &st) {
  if (!oledOk || !oledDirty) {
    return;
  }
  const uint32_t now = millis();
  if (now - lastOledDrawMs < OLED_PERIOD_MS) {
    return;
  }
  lastOledDrawMs = now;
  oledDirty = false;
  oledDraw(st);
}

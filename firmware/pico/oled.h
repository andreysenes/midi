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

static const int16_t OLED_BANK_W = 18;
static const int16_t OLED_BANK_H = 10;
static const int16_t OLED_ROLL_X = 20;
static const int16_t OLED_ROLL_Y = 0;
static const int16_t OLED_ROLL_W = 108;
static const int16_t OLED_ROLL_H = 21;
static const int16_t OLED_HEAD_H = 4;
static const int16_t OLED_STATUS_Y = 23;
static const uint8_t OLED_ROLL_CAP = 48;
static const uint32_t OLED_ROLL_WIN_MS = 4000;
static const uint32_t OLED_ROLL_WIN_CLK = 96;  // 4 beats @ 24 PPQN

struct OledStatus {
  int8_t octave;
  uint8_t volume;
  uint8_t program;
  uint8_t typedDigits;
  uint8_t typedProgram;
  bool sustain;
  bool usbMounted;
  bool mcpOk;
  uint8_t perfBank;
  uint8_t padBank;
  bool selHeld;
  bool bankFocusPad;
  bool bankSelectActive;
  bool clockRunning;
  bool transportPlaying;
  bool recording;
  bool looping;
  uint8_t beat;
  uint32_t playheadClocks;
  uint32_t nowMs;
  const char *timeText;
  const char *trackName;
  const char *chordName;
  const char *patchName;
  uint8_t audioMode;
};

struct OledRollEv {
  uint32_t startMs;
  uint32_t endMs;
  uint32_t startClk;
  uint32_t endClk;
  uint8_t note;
  bool open;
};

static OledRollEv oledRoll[OLED_ROLL_CAP];
static uint32_t oledRollNowClk = 0;

static void oledMark() {
  oledDirty = true;
}

static void oledRollAllOff() {
  const uint32_t now = millis();
  for (uint8_t i = 0; i < OLED_ROLL_CAP; i++) {
    if (oledRoll[i].open) {
      oledRoll[i].open = false;
      oledRoll[i].endMs = now;
      oledRoll[i].endClk = oledRollNowClk;
    }
  }
  oledMark();
}

static void oledRollSetClock(uint32_t clk) {
  oledRollNowClk = clk;
}

static void oledRollNote(uint8_t note, bool on) {
  const uint32_t now = millis();
  if (on) {
    int8_t slot = -1;
    uint32_t oldest = 0xffffffffUL;
    for (uint8_t i = 0; i < OLED_ROLL_CAP; i++) {
      if (!oledRoll[i].open && oledRoll[i].endMs == 0 && oledRoll[i].startMs == 0) {
        slot = static_cast<int8_t>(i);
        break;
      }
      const uint32_t t = oledRoll[i].startMs;
      if (!oledRoll[i].open && t < oldest) {
        oldest = t;
        slot = static_cast<int8_t>(i);
      }
    }
    if (slot < 0) {
      slot = 0;
    }
    oledRoll[slot].note = note;
    oledRoll[slot].startMs = now;
    oledRoll[slot].endMs = 0;
    oledRoll[slot].startClk = oledRollNowClk;
    oledRoll[slot].endClk = 0;
    oledRoll[slot].open = true;
  } else {
    int8_t slot = -1;
    for (uint8_t i = 0; i < OLED_ROLL_CAP; i++) {
      if (oledRoll[i].open && oledRoll[i].note == note) {
        slot = static_cast<int8_t>(i);
      }
    }
    if (slot >= 0) {
      oledRoll[slot].open = false;
      oledRoll[slot].endMs = now;
      oledRoll[slot].endClk = oledRollNowClk;
    }
  }
  oledMark();
}

static bool oledRollBusy() {
  for (uint8_t i = 0; i < OLED_ROLL_CAP; i++) {
    if (oledRoll[i].open) {
      return true;
    }
    if (oledRoll[i].startMs != 0) {
      const uint32_t end = oledRoll[i].open ? millis() : oledRoll[i].endMs;
      if ((millis() - end) < OLED_ROLL_WIN_MS) {
        return true;
      }
    }
  }
  return false;
}

static void oledDrawBankBox(int16_t x, int16_t y, char kind, uint8_t bank1to8, bool emphasize) {
  char buf[4];
  snprintf(buf, sizeof(buf), "%c%u", kind, static_cast<unsigned>(bank1to8));
  if (emphasize) {
    oled.fillRect(x, y, OLED_BANK_W, OLED_BANK_H, SSD1306_WHITE);
    oled.setTextColor(SSD1306_BLACK);
  } else {
    oled.drawRect(x, y, OLED_BANK_W, OLED_BANK_H, SSD1306_WHITE);
    oled.setTextColor(SSD1306_WHITE);
  }
  oled.setCursor(static_cast<int16_t>(x + 2), static_cast<int16_t>(y + 1));
  oled.print(buf);
  oled.setTextColor(SSD1306_WHITE);
}

static void oledDrawPlayIcon(int16_t x, int16_t y) {
  oled.fillTriangle(
      x, y,
      x, static_cast<int16_t>(y + 6),
      static_cast<int16_t>(x + 5), static_cast<int16_t>(y + 3),
      SSD1306_WHITE);
}

static void oledDrawStopIcon(int16_t x, int16_t y) {
  oled.fillRect(x, y, 6, 6, SSD1306_WHITE);
}

static void oledDrawRecIcon(int16_t x, int16_t y) {
  oled.fillCircle(static_cast<int16_t>(x + 3), static_cast<int16_t>(y + 3), 3, SSD1306_WHITE);
}

static void oledDrawLoopIcon(int16_t x, int16_t y) {
  oled.drawFastHLine(x, static_cast<int16_t>(y + 1), 5, SSD1306_WHITE);
  oled.drawFastVLine(x, static_cast<int16_t>(y + 1), 2, SSD1306_WHITE);
  oled.fillTriangle(
      static_cast<int16_t>(x + 4), y,
      static_cast<int16_t>(x + 7), static_cast<int16_t>(y + 2),
      static_cast<int16_t>(x + 4), static_cast<int16_t>(y + 4),
      SSD1306_WHITE);
  oled.drawFastHLine(static_cast<int16_t>(x + 2), static_cast<int16_t>(y + 5), 5, SSD1306_WHITE);
  oled.drawFastVLine(static_cast<int16_t>(x + 7), static_cast<int16_t>(y + 4), 2, SSD1306_WHITE);
  oled.fillTriangle(
      x, static_cast<int16_t>(y + 4),
      static_cast<int16_t>(x + 3), static_cast<int16_t>(y + 2),
      static_cast<int16_t>(x + 3), static_cast<int16_t>(y + 6),
      SSD1306_WHITE);
}

static int16_t oledBarX(uint32_t clocksInBar) {
  if (clocksInBar >= OLED_ROLL_WIN_CLK) {
    clocksInBar = OLED_ROLL_WIN_CLK;
  }
  return static_cast<int16_t>(
      OLED_ROLL_X + (clocksInBar * static_cast<uint32_t>(OLED_ROLL_W)) / OLED_ROLL_WIN_CLK);
}

static int16_t oledPitchRowY(uint8_t note, uint8_t minN, uint8_t maxN) {
  const int16_t rowH = 3;
  const int16_t noteY = static_cast<int16_t>(OLED_ROLL_Y + OLED_HEAD_H);
  const int16_t noteH = static_cast<int16_t>(OLED_ROLL_H - OLED_HEAD_H);
  const int16_t nRows = noteH / rowH;
  int span = static_cast<int>(maxN) - static_cast<int>(minN);
  if (span < 1) {
    span = 1;
  }
  int row;
  if (span < nRows) {
    row = static_cast<int>(note) - static_cast<int>(minN);
  } else {
    row = (static_cast<int>(note) - static_cast<int>(minN)) * (nRows - 1) / span;
  }
  if (row < 0) {
    row = 0;
  }
  if (row >= nRows) {
    row = nRows - 1;
  }
  return static_cast<int16_t>(noteY + (nRows - 1 - row) * rowH);
}

static void oledDrawPlayhead(int16_t px) {
  if (px < OLED_ROLL_X) {
    px = OLED_ROLL_X;
  }
  if (px > OLED_ROLL_X + OLED_ROLL_W - 1) {
    px = static_cast<int16_t>(OLED_ROLL_X + OLED_ROLL_W - 1);
  }
  int16_t t = px;
  if (t < OLED_ROLL_X + 2) {
    t = static_cast<int16_t>(OLED_ROLL_X + 2);
  }
  if (t > OLED_ROLL_X + OLED_ROLL_W - 3) {
    t = static_cast<int16_t>(OLED_ROLL_X + OLED_ROLL_W - 3);
  }
  oled.fillTriangle(
      static_cast<int16_t>(t - 2), OLED_ROLL_Y,
      static_cast<int16_t>(t + 2), OLED_ROLL_Y,
      t, static_cast<int16_t>(OLED_ROLL_Y + 3),
      SSD1306_WHITE);
  oled.drawFastVLine(
      px,
      static_cast<int16_t>(OLED_ROLL_Y + OLED_HEAD_H),
      static_cast<int16_t>(OLED_ROLL_H - OLED_HEAD_H),
      SSD1306_WHITE);
}

static void oledDrawRoll(const OledStatus &st) {
  const uint32_t nowClk = st.playheadClocks;
  const uint32_t phase = nowClk % OLED_ROLL_WIN_CLK;
  const uint32_t barStart = nowClk - phase;

  uint8_t minN = 127;
  uint8_t maxN = 0;
  bool any = false;
  for (uint8_t i = 0; i < OLED_ROLL_CAP; i++) {
    const OledRollEv &e = oledRoll[i];
    if (e.startMs == 0 && e.startClk == 0 && !e.open) {
      continue;
    }
    const uint32_t t0 = e.startClk;
    const uint32_t t1 = e.open ? nowClk : e.endClk;
    if (t1 < barStart || t0 >= barStart + OLED_ROLL_WIN_CLK) {
      continue;
    }
    any = true;
    if (e.note < minN) {
      minN = e.note;
    }
    if (e.note > maxN) {
      maxN = e.note;
    }
  }

  if (any) {
    for (uint8_t i = 0; i < OLED_ROLL_CAP; i++) {
      const OledRollEv &e = oledRoll[i];
      if (e.startMs == 0 && e.startClk == 0 && !e.open) {
        continue;
      }
      const uint32_t t0 = e.startClk;
      const uint32_t t1 = e.open ? nowClk : e.endClk;
      int32_t rel0 = static_cast<int32_t>(t0) - static_cast<int32_t>(barStart);
      int32_t rel1 = static_cast<int32_t>(t1) - static_cast<int32_t>(barStart);
      if (rel1 <= 0 || rel0 >= static_cast<int32_t>(OLED_ROLL_WIN_CLK)) {
        continue;
      }
      if (rel0 < 0) {
        rel0 = 0;
      }
      if (rel1 > static_cast<int32_t>(OLED_ROLL_WIN_CLK)) {
        rel1 = static_cast<int32_t>(OLED_ROLL_WIN_CLK);
      }
      const int16_t x0 = oledBarX(static_cast<uint32_t>(rel0));
      int16_t x1 = oledBarX(static_cast<uint32_t>(rel1));
      int16_t w = static_cast<int16_t>(x1 - x0);
      if (w < 2) {
        w = 2;
      } else {
        w = static_cast<int16_t>(w - 1);
      }
      if (x0 + w > OLED_ROLL_X + OLED_ROLL_W) {
        w = static_cast<int16_t>(OLED_ROLL_X + OLED_ROLL_W - x0);
      }
      if (w < 1) {
        continue;
      }
      oled.fillRect(x0, oledPitchRowY(e.note, minN, maxN), w, 2, SSD1306_WHITE);
    }
  }

  oledDrawPlayhead(oledBarX(phase));
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

  if (!st.usbMounted && !st.patchName) {
    oled.setCursor(0, 4);
    oled.println(F("Casio SA-1 MIDI"));
    oled.print(F("aguardando USB"));
    oled.display();
    return;
  }

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
  oledDrawBankBox(0, OLED_BANK_H, 'B', static_cast<uint8_t>(st.padBank + 1), empB);

  oledDrawRoll(st);

  oled.drawFastHLine(0, 21, 128, SSD1306_WHITE);

  const char *chord = (st.chordName && st.chordName[0]) ? st.chordName : "";
  char cshow[7];
  strncpy(cshow, chord, 6);
  cshow[6] = '\0';
  {
    const size_t n = strlen(cshow);
    if (n > 0 && cshow[n - 1] == '/') {
      cshow[n - 1] = '\0';
    }
  }
  oled.setCursor(0, OLED_STATUS_Y);
  if (cshow[0]) {
    oled.print(cshow);
  } else if (st.patchName && st.patchName[0] && st.audioMode == 0) {
    oled.print(st.patchName);
  }

  const int16_t iconX = 120;
  if (st.looping) {
    oledDrawLoopIcon(110, 24);
  }
  if (st.recording) {
    oledDrawRecIcon(iconX, 24);
  } else if (st.transportPlaying || st.clockRunning) {
    oledDrawPlayIcon(iconX, 24);
  } else {
    oledDrawStopIcon(iconX, 24);
  }

  const char *track = (st.trackName && st.trackName[0]) ? st.trackName : "";
  if (track[0] && strcmp(track, "--") != 0) {
    char tr[8];
    strncpy(tr, track, 7);
    tr[7] = '\0';
    const int16_t tw = static_cast<int16_t>(strlen(tr) * 6);
    int16_t tx = static_cast<int16_t>((128 - tw) / 2);
    if (tx < 0) {
      tx = 0;
    }
    oled.setCursor(tx, OLED_STATUS_Y);
    oled.print(tr);
  }

  if (st.sustain && !cshow[0]) {
    oled.fillRect(0, 23, 7, 8, SSD1306_WHITE);
    oled.setTextColor(SSD1306_BLACK);
    oled.setCursor(1, 24);
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
  memset(oledRoll, 0, sizeof(oledRoll));
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
  if (!oledOk) {
    return;
  }
  oledRollNowClk = st.playheadClocks;
  if (st.transportPlaying || st.clockRunning || oledRollBusy()) {
    oledDirty = true;
  }
  if (!oledDirty) {
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

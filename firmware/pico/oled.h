#pragma once

#include <string.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "config.h"

// clkAfter = 400 kHz: o default da Adafruit restaura 100 kHz e atrasa a matriz.
static Adafruit_SSD1306 oled(OLED_WIDTH, OLED_HEIGHT, &Wire, -1, 400000UL, 400000UL);
static bool oledOk = false;
static bool oledDirty = true;
static uint32_t lastOledDrawMs = 0;

static const char *const NOTE_NAMES[12] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B",
};

struct OledStatus {
  int8_t octave;
  uint8_t volume;
  uint8_t program;
  uint8_t typedDigits;
  uint8_t typedProgram;
  uint8_t lastNote;
  bool haveNote;
  bool sustain;
  bool usbMounted;
  bool mcpOk;
  uint8_t perfBank;  // 0..7
  uint8_t padBank;   // 0..7
  bool selHeld;
  bool clockRunning;
  bool transportPlaying;
  uint8_t beat;  // 0..3 = tempos 1..4
};

static void oledMark() {
  oledDirty = true;
}

static void formatNoteName(uint8_t note, char *buf, size_t n) {
  const uint8_t pc = note % 12;
  const int oct = static_cast<int>(note / 12) - 1;
  snprintf(buf, n, "%s%d", NOTE_NAMES[pc], oct);
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

  // Bancos na UI: 1..8 (internos 0..7)
  oled.setCursor(0, 0);
  if (st.selHeld) {
    oled.print(F("BANK "));
    oled.print(st.padBank + 1);
  } else {
    oled.print(F("P"));
    oled.print(st.perfBank + 1);
    oled.print(F(" B"));
    oled.print(st.padBank + 1);
  }
  if (st.transportPlaying || st.clockRunning) {
    oled.print(F(" >"));
  }
  oled.setCursor(74, 0);
  oled.print(F("O"));
  if (st.octave >= 0) {
    oled.print('+');
  }
  oled.print(st.octave);

  // Duas barras verticais de volume no canto direito (128×32).
  {
    const int x0 = 118;
    const int y0 = 2;
    const int bw = 4;
    const int bh = 28;
    const int gap = 2;
    const int fill = map(st.volume, 0, 127, 0, bh - 2);
    for (uint8_t k = 0; k < 2; k++) {
      const int x = x0 + k * (bw + gap);
      oled.drawRect(x, y0, bw, bh, SSD1306_WHITE);
      if (fill > 0) {
        oled.fillRect(x + 1, y0 + bh - 1 - fill, bw - 2, fill, SSD1306_WHITE);
      }
    }
  }

  oled.setCursor(0, 22);
  if (st.clockRunning) {
    for (uint8_t i = 0; i < 4; i++) {
      if (i) {
        oled.print('.');
      }
      oled.print(i == st.beat ? static_cast<char>('1' + i) : '-');
    }
  } else if (st.typedDigits == 1) {
    oled.print(F("PGM "));
    oled.print(st.typedProgram / 10);
    oled.print('_');
  } else {
    oled.print(F("PGM "));
    if (st.program < 100) {
      oled.print('0');
    }
    if (st.program < 10) {
      oled.print('0');
    }
    oled.print(st.program);
  }

  if (st.haveNote) {
    char name[6];
    formatNoteName(st.lastNote, name, sizeof(name));
    const uint8_t nlen = static_cast<uint8_t>(strlen(name));
    oled.setCursor(static_cast<int16_t>(80 - nlen * 6), 22);
    oled.print(name);
  }

  if (st.sustain) {
    oled.fillRect(100, 22, 16, 8, SSD1306_WHITE);
    oled.setTextColor(SSD1306_BLACK);
    oled.setCursor(104, 22);
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

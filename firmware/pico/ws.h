#pragma once

#include <string.h>
#include <Adafruit_NeoPixel.h>
#include "config.h"

static Adafruit_NeoPixel wsBar(WS_COUNT, WS_PIN, NEO_GRB + NEO_KHZ800);
static bool wsOk = false;
static uint8_t wsRgb[WS_COUNT][3];
static uint8_t wsAlpha[WS_COUNT];
static uint8_t wsMaster = 255;
static bool wsRainbowOn = false;
static uint8_t wsRainbowHue = 0;
static uint32_t lastRainbowMs = 0;

// Lab serial L* takes over until clock/SEL refreshes performance LEDs.
static bool wsLabOverride = false;

enum WsPerfMode : uint8_t { WS_PERF_IDLE = 0, WS_PERF_TEMPO = 1, WS_PERF_BANK = 2 };
static WsPerfMode wsPerfMode = WS_PERF_IDLE;
static uint8_t wsTempoStep = 0;  // 0..7 = colcheias no 4/4 (2 por tempo)
static uint8_t wsPerfBankLed = 0;
static uint8_t wsPadBankLed = 0;
static bool wsBankFocusPad = true;  // [] = pad (azul) vs performance (vermelho)
static uint8_t wsIdlePhase = 0;
static uint32_t lastIdleMs = 0;

// Performance = red, pads = blue. Wave = soft warm amber (not bank colors).
static const uint8_t WS_PERF_R = 56, WS_PERF_G = 0, WS_PERF_B = 4;
static const uint8_t WS_PERF_SEL_R = 110, WS_PERF_SEL_G = 0, WS_PERF_SEL_B = 6;
static const uint8_t WS_PAD_R = 0, WS_PAD_G = 8, WS_PAD_B = 36;
static const uint8_t WS_PAD_SEL_R = 0, WS_PAD_SEL_G = 18, WS_PAD_SEL_B = 70;
static const uint8_t WS_WAVE_R = 22, WS_WAVE_G = 14, WS_WAVE_B = 6;

static void wsApply() {
  if (!wsOk) {
    return;
  }
  for (uint8_t i = 0; i < WS_COUNT; i++) {
    const uint16_t a = (static_cast<uint16_t>(wsAlpha[i]) * wsMaster) / 255;
    const uint8_t phys = WS_REV ? static_cast<uint8_t>(WS_COUNT - 1 - i) : i;
    wsBar.setPixelColor(phys, wsBar.Color(
        static_cast<uint8_t>((wsRgb[i][0] * a) / 255),
        static_cast<uint8_t>((wsRgb[i][1] * a) / 255),
        static_cast<uint8_t>((wsRgb[i][2] * a) / 255)));
  }
  wsBar.show();
}

static void wsSetLed(uint8_t i, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
  if (i >= WS_COUNT) {
    return;
  }
  wsRgb[i][0] = r;
  wsRgb[i][1] = g;
  wsRgb[i][2] = b;
  wsAlpha[i] = a;
}

static void wsSetAll(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
  for (uint8_t i = 0; i < WS_COUNT; i++) {
    wsSetLed(i, r, g, b, a);
  }
}

static void wsClear() {
  wsSetAll(0, 0, 0, 0);
  wsApply();
}

// Smooth traveling wave across all 8 LEDs (one hump spanning the bar).
static uint8_t wsWaveAlpha(uint8_t i, uint8_t lo, uint8_t span) {
  // 256 / 8 = 32 → contiguous wave using every LED together.
  const uint8_t pos = static_cast<uint8_t>(wsIdlePhase + i * 32);
  const uint8_t tri = pos < 128 ? pos : static_cast<uint8_t>(255 - pos);
  // Ease: square the triangle for softer peaks (smoothstep-ish).
  const uint16_t eased = (static_cast<uint16_t>(tri) * tri) / 127;
  return static_cast<uint8_t>(lo + (eased * span) / 127);
}

// Same LED for P+B: alternate red ↔ blue (bit 0x10 ≈ 0.27 s por cor).
static bool wsSameBankShowPad() {
  return (wsIdlePhase & 0x10) != 0;
}

// Idle: amber wave on all LEDs; selected banks stay red/blue at 15.
static void wsRenderIdleBanks() {
  for (uint8_t i = 0; i < WS_COUNT; i++) {
    const uint8_t bgA = wsWaveAlpha(i, 3, 7);  // 3–10
    const bool isPerf = (i == wsPerfBankLed);
    const bool isPad = (i == wsPadBankLed);
    if (isPerf && isPad) {
      if (wsSameBankShowPad()) {
        wsSetLed(i, WS_PAD_R, WS_PAD_G, WS_PAD_B, 15);
      } else {
        wsSetLed(i, WS_PERF_R, WS_PERF_G, WS_PERF_B, 15);
      }
    } else if (isPerf) {
      wsSetLed(i, WS_PERF_R, WS_PERF_G, WS_PERF_B, 15);
    } else if (isPad) {
      wsSetLed(i, WS_PAD_R, WS_PAD_G, WS_PAD_B, 15);
    } else {
      wsSetLed(i, WS_WAVE_R, WS_WAVE_G, WS_WAVE_B, bgA);
    }
  }
  wsApply();
}

// Select: [] pulses ~20–30; * at 15; amber wave on the rest.
static void wsRenderBank() {
  const uint8_t pulsePos = wsIdlePhase;
  const uint8_t pulse = pulsePos < 128 ? pulsePos : static_cast<uint8_t>(255 - pulsePos);
  const uint8_t selA = static_cast<uint8_t>(20 + (static_cast<uint16_t>(pulse) * 10) / 127);
  const uint8_t focusLed = wsBankFocusPad ? wsPadBankLed : wsPerfBankLed;
  const uint8_t otherLed = wsBankFocusPad ? wsPerfBankLed : wsPadBankLed;
  const bool same = (focusLed == otherLed);

  for (uint8_t i = 0; i < WS_COUNT; i++) {
    const uint8_t bgA = wsWaveAlpha(i, 3, 7);
    if (i == focusLed) {
      if (same && wsSameBankShowPad() != wsBankFocusPad) {
        if (wsBankFocusPad) {
          wsSetLed(i, WS_PERF_R, WS_PERF_G, WS_PERF_B, 15);
        } else {
          wsSetLed(i, WS_PAD_R, WS_PAD_G, WS_PAD_B, 15);
        }
      } else if (wsBankFocusPad) {
        wsSetLed(i, WS_PAD_SEL_R, WS_PAD_SEL_G, WS_PAD_SEL_B, selA);
      } else {
        wsSetLed(i, WS_PERF_SEL_R, WS_PERF_SEL_G, WS_PERF_SEL_B, selA);
      }
    } else if (!same && i == otherLed) {
      if (wsBankFocusPad) {
        wsSetLed(i, WS_PERF_R, WS_PERF_G, WS_PERF_B, 15);
      } else {
        wsSetLed(i, WS_PAD_R, WS_PAD_G, WS_PAD_B, 15);
      }
    } else {
      wsSetLed(i, WS_WAVE_R, WS_WAVE_G, WS_WAVE_B, bgA);
    }
  }
  wsApply();
}

static void wsRenderPerf() {
  if (!wsOk || wsLabOverride || wsRainbowOn) {
    return;
  }
  if (wsPerfMode == WS_PERF_BANK) {
    wsRenderBank();
    return;
  }
  if (wsPerfMode == WS_PERF_IDLE) {
    return;
  }
  if (wsPerfMode == WS_PERF_TEMPO) {
    wsSetAll(0, 0, 0, 0);
    for (uint8_t i = 0; i < WS_COUNT; i++) {
      const uint8_t a = (i == wsTempoStep) ? 15 : 5;
      wsSetLed(i, 0, 8, 36, a);
    }
    wsApply();
    return;
  }
  wsClear();
}

static void wsShowTempo(uint8_t step0to7) {
  wsLabOverride = false;
  wsRainbowOn = false;
  wsPerfMode = WS_PERF_TEMPO;
  wsTempoStep = step0to7 & 7;
  wsRenderPerf();
}

static void wsShowBank(bool focusPad, uint8_t perf0to7, uint8_t pad0to7) {
  wsLabOverride = false;
  wsRainbowOn = false;
  wsPerfMode = WS_PERF_BANK;
  wsBankFocusPad = focusPad;
  wsPerfBankLed = perf0to7 < WS_COUNT ? perf0to7 : 0;
  wsPadBankLed = pad0to7 < WS_COUNT ? pad0to7 : 0;
  wsRenderPerf();
}

static void wsRenderIdle() {
  if (!wsOk || wsLabOverride || wsRainbowOn || wsPerfMode != WS_PERF_IDLE) {
    return;
  }
  wsRenderIdleBanks();
}

static void wsIdleTick() {
  if (!wsOk || wsLabOverride || wsRainbowOn) {
    return;
  }
  if (wsPerfMode != WS_PERF_IDLE && wsPerfMode != WS_PERF_BANK) {
    return;
  }
  const uint32_t now = millis();
  if (now - lastIdleMs < 25) {
    return;
  }
  lastIdleMs = now;
  wsIdlePhase = static_cast<uint8_t>(wsIdlePhase + 2);
  if (wsPerfMode == WS_PERF_BANK) {
    wsRenderBank();
  } else {
    wsRenderIdle();
  }
}

static void wsShowIdle(uint8_t perf0to7, uint8_t pad0to7) {
  wsLabOverride = false;
  wsRainbowOn = false;
  wsPerfMode = WS_PERF_IDLE;
  wsPerfBankLed = perf0to7 < WS_COUNT ? perf0to7 : 0;
  wsPadBankLed = pad0to7 < WS_COUNT ? pad0to7 : 0;
  lastIdleMs = 0;
  wsIdleTick();
}

static void wsShowOff() {
  wsShowIdle(wsPerfBankLed, wsPadBankLed);
}

static void wsWheel(uint8_t pos, uint8_t &r, uint8_t &g, uint8_t &b) {
  pos = static_cast<uint8_t>(255 - pos);
  if (pos < 85) {
    r = static_cast<uint8_t>(255 - pos * 3);
    g = 0;
    b = static_cast<uint8_t>(pos * 3);
    return;
  }
  if (pos < 170) {
    pos = static_cast<uint8_t>(pos - 85);
    r = 0;
    g = static_cast<uint8_t>(pos * 3);
    b = static_cast<uint8_t>(255 - pos * 3);
    return;
  }
  pos = static_cast<uint8_t>(pos - 170);
  r = static_cast<uint8_t>(pos * 3);
  g = static_cast<uint8_t>(255 - pos * 3);
  b = 0;
}

static void wsRainbowTick() {
  if (!wsOk || !wsRainbowOn) {
    return;
  }
  const uint32_t now = millis();
  if (now - lastRainbowMs < 32) {
    return;
  }
  lastRainbowMs = now;
  wsRainbowHue = static_cast<uint8_t>(wsRainbowHue + 3);
  for (uint8_t i = 0; i < WS_COUNT; i++) {
    uint8_t r, g, b;
    wsWheel(static_cast<uint8_t>(wsRainbowHue + i * 32), r, g, b);
    wsSetLed(i, r, g, b, 220);
  }
  wsApply();
}

static bool setupWs() {
  memset(wsRgb, 0, sizeof(wsRgb));
  memset(wsAlpha, 0, sizeof(wsAlpha));
  wsBar.begin();
  wsBar.clear();
  wsBar.show();
  wsOk = true;
  wsShowIdle(0, 0);
  return true;
}

static void handleLedCommand(char *line) {
  while (*line == ' ') {
    line++;
  }
  if (*line == '\0') {
    return;
  }
  const char target = *line++;
  while (*line == ' ') {
    line++;
  }
  if (target == 'r' || target == 'R') {
    wsLabOverride = true;
    wsRainbowOn = (*line == '1') ? true : (*line == '0') ? false : !wsRainbowOn;
    if (!wsRainbowOn) {
      wsShowIdle(wsPerfBankLed, wsPadBankLed);
    } else {
      lastRainbowMs = 0;
      wsRainbowTick();
    }
    Serial.println(wsRainbowOn ? F("WS  rainbow") : F("WS  rainbow off"));
    return;
  }
  if (target == 'm' || target == 'M') {
    int master = atoi(line);
    wsMaster = static_cast<uint8_t>(constrain(master, 0, 255));
    wsApply();
    return;
  }
  char hex[8] = {};
  uint8_t hi = 0;
  while (*line && *line != ' ' && hi < 6) {
    hex[hi++] = *line++;
  }
  while (*line == ' ') {
    line++;
  }
  int alpha = *line ? atoi(line) : 255;
  auto nib = [](char c) -> int {
    if (c >= '0' && c <= '9') {
      return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
      return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
      return c - 'A' + 10;
    }
    return 0;
  };
  if (hi != 6) {
    return;
  }
  const uint8_t r = static_cast<uint8_t>((nib(hex[0]) << 4) | nib(hex[1]));
  const uint8_t g = static_cast<uint8_t>((nib(hex[2]) << 4) | nib(hex[3]));
  const uint8_t b = static_cast<uint8_t>((nib(hex[4]) << 4) | nib(hex[5]));
  const uint8_t a = static_cast<uint8_t>(constrain(alpha, 0, 255));
  wsRainbowOn = false;
  wsLabOverride = true;
  if (target == 'a' || target == 'A') {
    wsSetAll(r, g, b, a);
  } else if (target >= '0' && target <= '7') {
    wsSetLed(static_cast<uint8_t>(target - '0'), r, g, b, a);
  }
  wsApply();
}

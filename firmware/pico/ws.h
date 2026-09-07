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
static uint8_t wsBankLed = 0;    // 0..7 = banks 1..8
static uint8_t wsIdlePhase = 0;
static uint32_t lastIdleMs = 0;

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

static void wsRenderPerf() {
  if (!wsOk || wsLabOverride || wsRainbowOn) {
    return;
  }
  if (wsPerfMode == WS_PERF_BANK) {
    wsSetAll(0, 0, 0, 0);
    wsSetLed(wsBankLed, 255, 0, 0, 255);
    wsApply();
    return;
  }
  if (wsPerfMode == WS_PERF_IDLE) {
    return;
  }
  if (wsPerfMode == WS_PERF_TEMPO) {
    wsSetAll(0, 0, 0, 0);
    for (uint8_t i = 0; i < WS_COUNT; i++) {
      if (i == wsTempoStep) {
        // Tempo (colcheia no tempo): mais claro. E (off-beat): um pouco mais baixo.
        if ((i & 1) == 0) {
          wsSetLed(i, 0, 20, 90, 255);
        } else {
          wsSetLed(i, 0, 10, 70, 200);
        }
      } else {
        wsSetLed(i, 0, 4, 24, 40);
      }
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

static void wsShowBank(uint8_t bank0to7) {
  wsLabOverride = false;
  wsRainbowOn = false;
  wsPerfMode = WS_PERF_BANK;
  wsBankLed = bank0to7 < WS_COUNT ? bank0to7 : 0;
  wsRenderPerf();
}

static void wsRenderIdle() {
  if (!wsOk || wsLabOverride || wsRainbowOn || wsPerfMode != WS_PERF_IDLE) {
    return;
  }
  for (uint8_t i = 0; i < WS_COUNT; i++) {
    const uint8_t pos = static_cast<uint8_t>(wsIdlePhase + i * 28);
    const uint8_t tri = pos < 128 ? pos : static_cast<uint8_t>(255 - pos);
    const uint8_t a = static_cast<uint8_t>(8 + (static_cast<uint16_t>(tri) * 22) / 127);
    wsSetLed(i, 56, 0, 4, a);
  }
  wsApply();
}

static void wsIdleTick() {
  if (!wsOk || wsLabOverride || wsRainbowOn || wsPerfMode != WS_PERF_IDLE) {
    return;
  }
  const uint32_t now = millis();
  if (now - lastIdleMs < 50) {
    return;
  }
  lastIdleMs = now;
  wsIdlePhase = static_cast<uint8_t>(wsIdlePhase + 2);
  wsRenderIdle();
}

static void wsShowIdle() {
  wsLabOverride = false;
  wsRainbowOn = false;
  wsPerfMode = WS_PERF_IDLE;
  lastIdleMs = 0;
  wsIdleTick();
}

static void wsShowOff() {
  wsShowIdle();
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
  wsShowIdle();
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
      wsShowIdle();
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

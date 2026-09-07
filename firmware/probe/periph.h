#pragma once

#include <Adafruit_GFX.h>
#include <Adafruit_NeoPixel.h>
#include <Adafruit_SSD1306.h>
#include <I2S.h>
#include <hardware/structs/sio.h>
#include <math.h>

// Pinagem = firmware/pico/config.h. Sonda testa o hardware, nao manda MIDI.
// I2S (PCM5102A) e WS2812 usam PIO distintos no RP2040.

static const uint8_t ENC1_A = 18; // EC11-1 oitava
static const uint8_t ENC1_B = 19;
static const bool ENC1_REV = true;  // horário = +
static const bool ENC2_REV = false;
static const uint8_t ENC2_A = 21; // EC11-2 volume
static const uint8_t ENC2_B = 22;
static const uint8_t JOY_X_PIN = 26; // A0
static const uint8_t JOY_Y_PIN = 27; // A1 (silk; é o GP27)
static const uint8_t JOY_SW_PIN = 20;
static int joyAdcMax = 4095;
static int joyDead = 16;
static int joyCircleR = 0;
static const uint8_t WS_PIN = 16; // WS2812 IN (silk SND IN VCC GND)
static const uint8_t WS_COUNT = 8;
static const bool WS_REV = true; // IN no LED 8 — lógico 0 = pixel físico 7
static const uint8_t I2S_BCK_PIN = 10;  // PCM5102A BCK; LRCK = BCK+1 = GP11
static const uint8_t I2S_LRCK_PIN = 11;
static const uint8_t I2S_DOUT_PIN = 12;
static const uint8_t I2S_DIN_PIN = 13;
static const uint32_t AUDIO_SR = 44100;

static I2S i2sOut(OUTPUT, I2S_BCK_PIN, I2S_DOUT_PIN);
static bool i2sOk = false;

enum AudioProbeMode : uint8_t {
  AUDIO_MUTE = 0,
  AUDIO_TONE = 1,
  AUDIO_KEYS = 2,
  AUDIO_CLOCK = 3,
};

static AudioProbeMode audioMode = AUDIO_TONE;
static uint8_t audioVol = 80;
static uint16_t audioToneHz = 440;
static uint32_t audioTonePhase = 0;
static uint32_t audioToneIncr = 0;
static uint32_t audioClockTick = 0;
static uint16_t audioEdgeBck = 0;
static uint16_t audioEdgeLrck = 0;
static uint16_t audioEdgeDout = 0;
static uint16_t audioEdgeDin = 0;
static bool audioPinBck = false;
static bool audioPinLrck = false;
static bool audioPinDout = false;
static bool audioPinDin = false;
static bool audioLinkOk = false;
static bool audioUnderrun = false;
static bool audioChecked = false;

struct AudioVoice {
  uint32_t phase;
  uint32_t incr;
  uint8_t note;
  bool on;
};
static const uint8_t AUDIO_VOICES = 4;
static AudioVoice audioVoices[AUDIO_VOICES];

static void audioSetKey(uint8_t ko, uint8_t ki, bool down);
static void audioTick();
static void audioCheckLink(bool verbose);

// KO0-KO3 = firmware/pico/matrix.h (o que o MIDI vai tocar).
static const uint8_t PIANO_MIDI[4][8] = {
    {53, 54, 55, 56, 57, 58, 59, 60},
    {81, 80, 79, 78, 77, 82, 83, 84},
    {73, 72, 71, 70, 69, 74, 75, 76},
    {65, 64, 63, 62, 61, 66, 67, 68},
};

static Adafruit_SSD1306 oled(128, 32, &Wire, -1, 400000UL, 400000UL);
static bool oledOk = false;
static uint8_t oledAddr = 0;
static bool oledDirty = true;
static uint32_t lastOledMs = 0;

static Adafruit_NeoPixel ws(WS_COUNT, WS_PIN, NEO_GRB + NEO_KHZ800);
static bool wsOk = false;
static uint8_t wsRgb[WS_COUNT][3];
static uint8_t wsAlpha[WS_COUNT];
static uint8_t wsMaster = 255;
static bool wsRainbowOn = false;
static uint8_t wsRainbowHue = 0;
static uint32_t lastRainbowMs = 0;

struct EncProbe {
  uint8_t a;
  uint8_t b;
  uint8_t prev;
  int8_t accum;
  int16_t count;
  int8_t lastDir;
};

static EncProbe enc1 = {ENC1_A, ENC1_B, 0, 0, 0, 0};
static EncProbe enc2 = {ENC2_A, ENC2_B, 0, 0, 0, 0};
static bool encSwap = true;

static int joyCenterX = 2048;
static int joyCenterY = 2048;
static int joyX = 2048;
static int joyY = 2048;
static int joyRawX = 2048;
static int joyRawY = 2048;
static int joyRawCx = 2048;
static int joyRawCy = 2048;
static int joyFiltX = 2048;
static int joyFiltY = 2048;
static int joyMinX = 2048;
static int joyMaxX = 2048;
static int joyMinY = 2048;
static int joyMaxY = 2048;
static bool joySw = false;
static bool joyInvX = true;
static bool joyInvY = false;
static bool joySwapXY = true;
static uint32_t lastJoyPrintMs = 0;

static uint8_t lastKo = 255;
static uint8_t lastKi = 255;
static bool lastKeyDown = false;
static const char *lastKeyName = "-";

static bool mcpAlive = false;
static bool uiStream = false;
static uint32_t lastJsonMs = 0;

static uint8_t matrixHeldMask[7] = {};
static uint8_t matrixStuckMask[7] = {};
static uint8_t lastKoPairWarn = 0;
static uint32_t lastKoPairMs = 0;

static void periphMark() {
  oledDirty = true;
}

static void periphSetMatrixMasks(const uint8_t heldMask[7], const uint8_t stuckMask[7]) {
  memcpy(matrixHeldMask, heldMask, 7);
  memcpy(matrixStuckMask, stuckMask, 7);
}

static void periphWarnKoPair(uint8_t ki) {
  const uint8_t bit = static_cast<uint8_t>(1u << (ki & 7));
  const uint32_t now = millis();
  if ((lastKoPairWarn & bit) && (now - lastKoPairMs < 800)) {
    return;
  }
  lastKoPairWarn |= bit;
  lastKoPairMs = now;
  Serial.print(F("WARN  KO0+KO4 no KI"));
  Serial.print(ki);
  Serial.println(F(" — conferir GPB0/GPB4, tocos 30 e 26"));
  if (uiStream) {
    Serial.print(F("J{\"t\":\"w\",\"ki\":"));
    Serial.print(ki);
    Serial.println(F("}"));
  }
}

static void periphClearKoPairWarn(uint8_t ki) {
  lastKoPairWarn &= static_cast<uint8_t>(~(1u << (ki & 7)));
}

static void periphNoteMatrix(uint8_t ko, uint8_t ki, bool down, const char *name) {
  lastKo = ko;
  lastKi = ki;
  lastKeyDown = down;
  lastKeyName = name;
  audioSetKey(ko, ki, down);
  periphMark();
  if (uiStream) {
    Serial.print(F("J{\"t\":\"k\",\"ko\":"));
    Serial.print(ko);
    Serial.print(F(",\"ki\":"));
    Serial.print(ki);
    Serial.print(F(",\"dn\":"));
    Serial.print(down ? 1 : 0);
    Serial.print(F(",\"n\":\""));
    Serial.print(name);
    Serial.println(F("\"}"));
  }
}

static void emitJsonState() {
  Serial.print(F("J{\"t\":\"s\",\"mcp\":"));
  Serial.print(mcpAlive ? 1 : 0);
  Serial.print(F(",\"oled\":"));
  Serial.print(oledOk ? 1 : 0);
  Serial.print(F(",\"oa\":"));
  Serial.print(oledAddr);
  Serial.print(F(",\"ws\":"));
  Serial.print(wsOk ? 1 : 0);
  Serial.print(F(",\"dac\":"));
  Serial.print(i2sOk ? 1 : 0);
  Serial.print(F(",\"aum\":"));
  Serial.print(static_cast<uint8_t>(audioMode));
  Serial.print(F(",\"av\":"));
  Serial.print(audioVol);
  Serial.print(F(",\"ah\":"));
  Serial.print(audioToneHz);
  Serial.print(F(",\"wr\":"));
  Serial.print(wsRainbowOn ? 1 : 0);
  Serial.print(F(",\"aok\":"));
  Serial.print(audioLinkOk ? 1 : 0);
  Serial.print(F(",\"abk\":"));
  Serial.print(audioPinBck ? 1 : 0);
  Serial.print(F(",\"alr\":"));
  Serial.print(audioPinLrck ? 1 : 0);
  Serial.print(F(",\"ado\":"));
  Serial.print(audioPinDout ? 1 : 0);
  Serial.print(F(",\"adi\":"));
  Serial.print(audioPinDin ? 1 : 0);
  Serial.print(F(",\"aeb\":"));
  Serial.print(audioEdgeBck);
  Serial.print(F(",\"ael\":"));
  Serial.print(audioEdgeLrck);
  Serial.print(F(",\"aed\":"));
  Serial.print(audioEdgeDout);
  Serial.print(F(",\"aei\":"));
  Serial.print(audioEdgeDin);
  Serial.print(F(",\"auf\":"));
  Serial.print(audioUnderrun ? 1 : 0);
  Serial.print(F(",\"ackd\":"));
  Serial.print(audioChecked ? 1 : 0);
  Serial.print(F(",\"e1\":"));
  Serial.print(enc1.count);
  Serial.print(F(",\"e2\":"));
  Serial.print(enc2.count);
  Serial.print(F(",\"es\":"));
  Serial.print(encSwap ? 1 : 0);
  Serial.print(F(",\"x\":"));
  Serial.print(joyX);
  Serial.print(F(",\"y\":"));
  Serial.print(joyY);
  Serial.print(F(",\"sw\":"));
  Serial.print(joySw ? 1 : 0);
  Serial.print(F(",\"cx\":"));
  Serial.print(joyCenterX);
  Serial.print(F(",\"cy\":"));
  Serial.print(joyCenterY);
  Serial.print(F(",\"xn\":"));
  Serial.print(joyMinX);
  Serial.print(F(",\"xx\":"));
  Serial.print(joyMaxX);
  Serial.print(F(",\"yn\":"));
  Serial.print(joyMinY);
  Serial.print(F(",\"yx\":"));
  Serial.print(joyMaxY);
  Serial.print(F(",\"am\":"));
  Serial.print(joyAdcMax);
  Serial.print(F(",\"dz\":"));
  Serial.print(joyDead);
  Serial.print(F(",\"cr\":"));
  Serial.print(joyCircleR);
  Serial.print(F(",\"rx\":"));
  Serial.print(joyRawX);
  Serial.print(F(",\"ry\":"));
  Serial.print(joyRawY);
  Serial.print(F(",\"ix\":"));
  Serial.print(joyInvX ? 1 : 0);
  Serial.print(F(",\"iy\":"));
  Serial.print(joyInvY ? 1 : 0);
  Serial.print(F(",\"xy\":"));
  Serial.print(joySwapXY ? 1 : 0);
  Serial.print(F(",\"ko\":"));
  Serial.print(lastKo == 255 ? -1 : lastKo);
  Serial.print(F(",\"ki\":"));
  Serial.print(lastKi == 255 ? -1 : lastKi);
  Serial.print(F(",\"dn\":"));
  Serial.print(lastKeyDown ? 1 : 0);
  Serial.print(F(",\"n\":\""));
  Serial.print(lastKeyName);
  Serial.print(F("\",\"h\":["));
  for (uint8_t r = 0; r < 7; r++) {
    if (r) {
      Serial.print(',');
    }
    Serial.print(matrixHeldMask[r]);
  }
  Serial.print(F("],\"s\":["));
  for (uint8_t r = 0; r < 7; r++) {
    if (r) {
      Serial.print(',');
    }
    Serial.print(matrixStuckMask[r]);
  }
  Serial.println(F("]}"));
}

static void periphSetUi(bool on) {
  uiStream = on;
  if (on) {
    Serial.println(F("UI on"));
    emitJsonState();
  } else {
    Serial.println(F("UI off"));
  }
}

static void wsApply() {
  if (!wsOk) {
    return;
  }
  for (uint8_t i = 0; i < WS_COUNT; i++) {
    const uint16_t a = (static_cast<uint16_t>(wsAlpha[i]) * wsMaster) / 255;
    const uint8_t r = static_cast<uint8_t>((wsRgb[i][0] * a) / 255);
    const uint8_t g = static_cast<uint8_t>((wsRgb[i][1] * a) / 255);
    const uint8_t b = static_cast<uint8_t>((wsRgb[i][2] * a) / 255);
    const uint8_t phys = WS_REV ? static_cast<uint8_t>(WS_COUNT - 1 - i) : i;
    ws.setPixelColor(phys, ws.Color(r, g, b));
  }
  ws.show();
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

static void wsRainbowStop() {
  wsRainbowOn = false;
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

static bool setupWsProbe() {
  memset(wsRgb, 0, sizeof(wsRgb));
  memset(wsAlpha, 0, sizeof(wsAlpha));
  wsMaster = 255;
  wsRainbowOn = false;
  wsRainbowHue = 0;
  ws.begin();
  ws.clear();
  ws.setBrightness(64);
  wsOk = true;
  // Flash curto — chip é write-only; "ok" = driver a correr.
  for (uint8_t i = 0; i < WS_COUNT; i++) {
    ws.setPixelColor(i, ws.Color(20, 20, 20));
  }
  ws.show();
  delay(120);
  ws.clear();
  ws.show();
  ws.setBrightness(255);
  return wsOk;
}

// L <i|a|m> <rrggbb|0-255> [0-255]
// i = LED 0-7 + cor + alpha; a = todos; m = master alpha
static void handleLedCommand(char *line) {
  while (*line == ' ') {
    line++;
  }
  if (*line == '\0') {
    Serial.println(F("L  uso: L <0-7|a|m|r> <rrggbb|0-255> [0-255]"));
    return;
  }
  const char target = *line++;
  while (*line == ' ') {
    line++;
  }
  if (target == 'r' || target == 'R') {
    if (*line == '1') {
      wsRainbowOn = true;
    } else if (*line == '0') {
      wsRainbowOn = false;
    } else {
      wsRainbowOn = !wsRainbowOn;
    }
    Serial.println(wsRainbowOn ? F("WS  rainbow") : F("WS  rainbow off"));
    if (!wsRainbowOn) {
      wsSetAll(0, 0, 0, 0);
      wsApply();
    } else {
      lastRainbowMs = 0;
      wsRainbowTick();
    }
    return;
  }
  if (target == 'm' || target == 'M') {
    int master = atoi(line);
    if (master < 0) {
      master = 0;
    }
    if (master > 255) {
      master = 255;
    }
    wsMaster = static_cast<uint8_t>(master);
    wsApply();
    Serial.print(F("WS  master="));
    Serial.println(wsMaster);
    return;
  }

  char hex[8] = {};
  uint8_t hi = 0;
  while (*line && *line != ' ' && hi < 6) {
    hex[hi++] = *line++;
  }
  hex[hi] = '\0';
  while (*line == ' ') {
    line++;
  }
  int alpha = 255;
  if (*line) {
    alpha = atoi(line);
    if (alpha < 0) {
      alpha = 0;
    }
    if (alpha > 255) {
      alpha = 255;
    }
  }

  auto hexNibble = [](char c) -> int {
    if (c >= '0' && c <= '9') {
      return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
      return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
      return c - 'A' + 10;
    }
    return -1;
  };
  if (hi != 6) {
    Serial.println(F("L  cor hex rrggbb (6 digitos)"));
    return;
  }
  int nib[6];
  for (uint8_t i = 0; i < 6; i++) {
    nib[i] = hexNibble(hex[i]);
    if (nib[i] < 0) {
      Serial.println(F("L  hex invalido"));
      return;
    }
  }
  const uint8_t r = static_cast<uint8_t>((nib[0] << 4) | nib[1]);
  const uint8_t g = static_cast<uint8_t>((nib[2] << 4) | nib[3]);
  const uint8_t b = static_cast<uint8_t>((nib[4] << 4) | nib[5]);
  const uint8_t a = static_cast<uint8_t>(alpha);

  if (target == 'a' || target == 'A') {
    wsRainbowStop();
    wsSetAll(r, g, b, a);
    wsApply();
    Serial.print(F("WS  all #"));
    Serial.print(hex);
    Serial.print(F(" a="));
    Serial.println(a);
    return;
  }
  if (target >= '0' && target <= '7') {
    wsRainbowStop();
    const uint8_t i = static_cast<uint8_t>(target - '0');
    wsSetLed(i, r, g, b, a);
    wsApply();
    Serial.print(F("WS  led"));
    Serial.print(i);
    Serial.print(F(" #"));
    Serial.print(hex);
    Serial.print(F(" a="));
    Serial.println(a);
    return;
  }
  Serial.println(F("L  alvo: 0-7 | a | m | r"));
}

static void initEnc(EncProbe &e) {
  pinMode(e.a, INPUT_PULLUP);
  pinMode(e.b, INPUT_PULLUP);
  e.prev = (digitalRead(e.a) << 1) | digitalRead(e.b);
  e.accum = 0;
  e.count = 0;
  e.lastDir = 0;
}

static int8_t encStep(EncProbe &e, bool rev) {
  const uint8_t current = (digitalRead(e.a) << 1) | digitalRead(e.b);
  const uint8_t pack = (e.prev << 2) | current;
  int8_t dir = 0;
  if (pack == 0b0001 || pack == 0b0111 || pack == 0b1110 || pack == 0b1000) {
    dir = 1;
  } else if (pack == 0b0010 || pack == 0b0100 || pack == 0b1101 || pack == 0b1011) {
    dir = -1;
  }
  if (rev) {
    dir = static_cast<int8_t>(-dir);
  }
  e.prev = current;
  if (dir == 0) {
    return 0;
  }
  e.accum += dir;
  if (e.accum >= 4) {
    e.accum = 0;
    e.lastDir = 1;
    e.count++;
    return 1;
  }
  if (e.accum <= -4) {
    e.accum = 0;
    e.lastDir = -1;
    e.count--;
    return -1;
  }
  return 0;
}

static void printEncLine(const char *tag, const EncProbe &e) {
  const uint8_t a = digitalRead(e.a);
  const uint8_t b = digitalRead(e.b);
  Serial.print(tag);
  Serial.print(F("  A="));
  Serial.print(a);
  Serial.print(F(" B="));
  Serial.print(b);
  Serial.print(F("  count="));
  Serial.print(e.count);
  if (a == 0 && b == 0) {
    Serial.print(F("  << ambos LOW: curto a GND ou pinos trocados"));
  } else if (a == 1 && b == 1) {
    Serial.print(F("  (gire para testar)"));
  }
  Serial.println();
}

static void scanEncodersProbe() {
  const int8_t s1 = encStep(enc1, ENC1_REV);
  const int8_t s2 = encStep(enc2, ENC2_REV);
  if (s1 != 0) {
    Serial.print(F("EC11-1  "));
    Serial.print(s1 > 0 ? '+' : '-');
    Serial.print(F("  count="));
    Serial.println(enc1.count);
    periphMark();
  }
  if (s2 != 0) {
    Serial.print(F("EC11-2  "));
    Serial.print(s2 > 0 ? '+' : '-');
    Serial.print(F("  count="));
    Serial.println(enc2.count);
    periphMark();
  }
}

static void joyMapPair(int rx, int ry, int *outX, int *outY) {
  if (joySwapXY) {
    const int t = rx;
    rx = ry;
    ry = t;
  }
  if (joyInvX) {
    rx = joyAdcMax - rx;
  }
  if (joyInvY) {
    ry = joyAdcMax - ry;
  }
  *outX = rx;
  *outY = ry;
}

static void joyApplyMap() {
  joyMapPair(joyRawX, joyRawY, &joyX, &joyY);
  joyMapPair(joyRawCx, joyRawCy, &joyCenterX, &joyCenterY);
}

static void joyResetRange() {
  joyMinX = joyCenterX;
  joyMaxX = joyCenterX;
  joyMinY = joyCenterY;
  joyMaxY = joyCenterY;
}

static void joyExpandRange() {
  if (abs(joyX - joyCenterX) <= joyDead && abs(joyY - joyCenterY) <= joyDead) {
    return;
  }
  if (joyX < joyMinX) {
    joyMinX = joyX;
  }
  if (joyX > joyMaxX) {
    joyMaxX = joyX;
  }
  if (joyY < joyMinY) {
    joyMinY = joyY;
  }
  if (joyY > joyMaxY) {
    joyMaxY = joyY;
  }
}

static void joyCaptureExtreme() {
  joyExpandRange();
  Serial.print(F("JOY  extremo  X="));
  Serial.print(joyMinX);
  Serial.print(F("..."));
  Serial.print(joyMaxX);
  Serial.print(F("  Y="));
  Serial.print(joyMinY);
  Serial.print(F("..."));
  Serial.println(joyMaxY);
}

static void printJoyMap() {
  Serial.print(F("JOY  mapa  invX="));
  Serial.print(joyInvX ? 1 : 0);
  Serial.print(F(" invY="));
  Serial.print(joyInvY ? 1 : 0);
  Serial.print(F(" swap="));
  Serial.println(joySwapXY ? 1 : 0);
  Serial.print(F("JOY  alcance  X="));
  Serial.print(joyMinX);
  Serial.print(F("..."));
  Serial.print(joyMaxX);
  Serial.print(F("  Y="));
  Serial.print(joyMinY);
  Serial.print(F("..."));
  Serial.println(joyMaxY);
  Serial.println(F("// cola em firmware/pico/config.h"));
  Serial.print(F("static const bool JOY_INVERT_X = "));
  Serial.print(joyInvX ? F("true") : F("false"));
  Serial.println(F(";"));
  Serial.print(F("static const bool JOY_INVERT_Y = "));
  Serial.print(joyInvY ? F("true") : F("false"));
  Serial.println(F(";"));
  Serial.print(F("static const bool JOY_SWAP_XY = "));
  Serial.print(joySwapXY ? F("true") : F("false"));
  Serial.println(F(";"));
  Serial.print(F("static const int JOY_MIN_X = "));
  Serial.print(joyMinX);
  Serial.println(F(";"));
  Serial.print(F("static const int JOY_MAX_X = "));
  Serial.print(joyMaxX);
  Serial.println(F(";"));
  Serial.print(F("static const int JOY_MIN_Y = "));
  Serial.print(joyMinY);
  Serial.println(F(";"));
  Serial.print(F("static const int JOY_MAX_Y = "));
  Serial.print(joyMaxY);
  Serial.println(F(";"));
  Serial.print(F("static const int JOY_ADC_MAX = "));
  Serial.print(joyAdcMax);
  Serial.println(F(";"));
  Serial.print(F("static const int JOY_DEADZONE = "));
  Serial.print(joyDead);
  Serial.println(F(";"));
  Serial.print(F("static const int JOY_CIRCLE_R = "));
  Serial.print(joyCircleR);
  Serial.println(F(";"));
}

static void calibrateJoyProbe();

static void joyUiKick() {
  periphMark();
  emitJsonState();
}

// j c recentrar | j x/y/s eixos | j e extremo | j g reset alcance
// j m xmin xmax ymin ymax | j 0 reset mapa
static void handleJoyCommand(char *line) {
  while (*line == ' ') {
    line++;
  }
  if (*line == '\0') {
    Serial.println(F("j  uso: j c | j x | j y | j s | j e | j g | j m xn xx yn yx | j 0"));
    printJoyMap();
    return;
  }
  const char cmd = *line++;
  while (*line == ' ') {
    line++;
  }
  if (cmd == 'c' || cmd == 'C') {
    Serial.println(F("JOY  recentrar (stick solto)"));
    calibrateJoyProbe();
    joyResetRange();
    Serial.print(F("JOY  centro X="));
    Serial.print(joyCenterX);
    Serial.print(F(" Y="));
    Serial.println(joyCenterY);
    printJoyMap();
    joyUiKick();
    return;
  }
  if (cmd == 'x' || cmd == 'X') {
    joyInvX = !joyInvX;
    joyApplyMap();
    joyResetRange();
    printJoyMap();
    joyUiKick();
    return;
  }
  if (cmd == 'y' || cmd == 'Y') {
    joyInvY = !joyInvY;
    joyApplyMap();
    joyResetRange();
    printJoyMap();
    joyUiKick();
    return;
  }
  if (cmd == 's' || cmd == 'S') {
    joySwapXY = !joySwapXY;
    joyApplyMap();
    joyResetRange();
    printJoyMap();
    joyUiKick();
    return;
  }
  if (cmd == 'e' || cmd == 'E') {
    joyCaptureExtreme();
    printJoyMap();
    joyUiKick();
    return;
  }
  if (cmd == 'g' || cmd == 'G') {
    joyResetRange();
    Serial.println(F("JOY  alcance reset — move o stick ate as bordas ou j e"));
    printJoyMap();
    joyUiKick();
    return;
  }
  if (cmd == 'm' || cmd == 'M') {
    int xn = atoi(line);
    while (*line && *line != ' ') {
      line++;
    }
    while (*line == ' ') {
      line++;
    }
    int xx = atoi(line);
    while (*line && *line != ' ') {
      line++;
    }
    while (*line == ' ') {
      line++;
    }
    int yn = atoi(line);
    while (*line && *line != ' ') {
      line++;
    }
    while (*line == ' ') {
      line++;
    }
    int yx = atoi(line);
    if (xn > xx) {
      const int t = xn;
      xn = xx;
      xx = t;
    }
    if (yn > yx) {
      const int t = yn;
      yn = yx;
      yx = t;
    }
    joyMinX = xn;
    joyMaxX = xx;
    joyMinY = yn;
    joyMaxY = yx;
    Serial.println(F("JOY  alcance definido"));
    printJoyMap();
    joyUiKick();
    return;
  }
  if (cmd == 'r' || cmd == 'R') {
    const int r = atoi(line);
    if (r > 20) {
      joyCircleR = r;
    }
    Serial.print(F("JOY  raio "));
    Serial.println(joyCircleR);
    joyUiKick();
    return;
  }
  if (cmd == '0') {
    joyInvX = false;
    joyInvY = false;
    joySwapXY = false;
    joyApplyMap();
    joyResetRange();
    printJoyMap();
    joyUiKick();
    return;
  }
  Serial.println(F("j  uso: j c | j x | j y | j s | j e | j g | j m xn xx yn yx | j 0"));
}

static int joyReadPin(uint8_t pin) {
  analogReadResolution(12);
  long s = 0;
  for (uint8_t i = 0; i < 8; i++) {
    s += analogRead(pin);
  }
  return static_cast<int>(s / 8);
}

static void joyDetectAdc(int ax, int ay) {
  if (ax < 1400 && ay < 1400) {
    joyAdcMax = 1023;
    joyDead = 12;
  } else {
    joyAdcMax = 4095;
    joyDead = 24;
  }
}

static int joyApplyDead(int v, int center) {
  if (abs(v - center) <= joyDead) {
    return center;
  }
  return v;
}

static void calibrateJoyProbe() {
  analogReadResolution(12);
  analogRead(JOY_X_PIN);
  analogRead(JOY_Y_PIN);
  delay(8);
  long sx = 0;
  long sy = 0;
  for (uint8_t i = 0; i < 32; i++) {
    sx += analogRead(JOY_X_PIN);
    sy += analogRead(JOY_Y_PIN);
    delay(2);
  }
  joyRawCx = static_cast<int>(sx / 32);
  joyRawCy = static_cast<int>(sy / 32);
  joyDetectAdc(joyRawCx, joyRawCy);
  joyFiltX = joyRawCx;
  joyFiltY = joyRawCy;
  joyRawX = joyRawCx;
  joyRawY = joyRawCy;
  joyApplyMap();
  joyX = joyApplyDead(joyX, joyCenterX);
  joyY = joyApplyDead(joyY, joyCenterY);
  joyResetRange();
  pinMode(JOY_SW_PIN, INPUT_PULLUP);
  joySw = digitalRead(JOY_SW_PIN) == LOW;
  Serial.print(F("JOY  adc "));
  Serial.print(joyAdcMax == 1023 ? F("10-bit") : F("12-bit"));
  Serial.print(F("  max="));
  Serial.print(joyAdcMax);
  Serial.print(F("  dead="));
  Serial.println(joyDead);
}

static void scanJoyProbe() {
  const int rx = joyReadPin(JOY_X_PIN);
  const int ry = joyReadPin(JOY_Y_PIN);
  joyFiltX = (joyFiltX * 3 + rx) / 4;
  joyFiltY = (joyFiltY * 3 + ry) / 4;
  joyRawX = joyFiltX;
  joyRawY = joyFiltY;
  joyApplyMap();
  joyX = joyApplyDead(joyX, joyCenterX);
  joyY = joyApplyDead(joyY, joyCenterY);
  joyExpandRange();
  const bool sw = digitalRead(JOY_SW_PIN) == LOW;
  const bool moved =
      abs(joyX - joyCenterX) > joyDead || abs(joyY - joyCenterY) > joyDead || sw != joySw;
  if (sw != joySw) {
    joySw = sw;
    periphMark();
  }
  if (!moved) {
    return;
  }
  joySw = sw;
  const uint32_t now = millis();
  if (now - lastJoyPrintMs < 80) {
    return;
  }
  lastJoyPrintMs = now;
  Serial.print(F("JOY  X="));
  Serial.print(joyX);
  Serial.print(F(" Y="));
  Serial.print(joyY);
  Serial.print(F(" SW="));
  Serial.print(sw ? 1 : 0);
  Serial.print(F("  ix="));
  Serial.print(joyInvX ? 1 : 0);
  Serial.print(F(" iy="));
  Serial.print(joyInvY ? 1 : 0);
  Serial.print(F(" xy="));
  Serial.println(joySwapXY ? 1 : 0);
  periphMark();
}

static void oledDrawProbe() {
  if (!oledOk) {
    return;
  }
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);

  oled.setCursor(0, 0);
  const int16_t left = encSwap ? enc2.count : enc1.count;
  const int16_t right = encSwap ? enc1.count : enc2.count;
  oled.print(encSwap ? F("E2") : F("E1"));
  if (left >= 0) {
    oled.print('+');
  }
  oled.print(left);
  oled.setCursor(64, 0);
  oled.print(encSwap ? F("E1") : F("E2"));
  if (right >= 0) {
    oled.print('+');
  }
  oled.print(right);

  oled.setCursor(0, 8);
  oled.print(F("X"));
  oled.print(joyX);
  oled.setCursor(52, 8);
  oled.print(F("Y"));
  oled.print(joyY);
  if (joySw) {
    oled.fillRect(110, 8, 18, 8, SSD1306_WHITE);
    oled.setTextColor(SSD1306_BLACK);
    oled.setCursor(114, 8);
    oled.print(F("S"));
    oled.setTextColor(SSD1306_WHITE);
  }

  oled.setCursor(0, 16);
  if (mcpAlive) {
    oled.print(F("MCP"));
  } else {
    oled.print(F("M--"));
  }
  oled.print(i2sOk ? F(" A") : F(" a"));
  if (audioMode == AUDIO_MUTE) {
    oled.print(F("mute"));
  } else if (audioMode == AUDIO_TONE) {
    oled.print(audioToneHz);
  } else if (audioMode == AUDIO_CLOCK) {
    oled.print(F("clk"));
  } else {
    oled.print(F("key"));
  }

  oled.setCursor(0, 24);
  if (lastKo != 255) {
    oled.print(lastKeyDown ? F("DN ") : F("UP "));
    oled.print(F("KO"));
    oled.print(lastKo);
    oled.print(F(" KI"));
    oled.print(lastKi);
    oled.print(' ');
    oled.print(lastKeyName);
  } else {
    oled.print(F("aperte tecla"));
  }

  oled.display();
}

static void oledTickProbe() {
  if (!oledOk || !oledDirty) {
    return;
  }
  const uint32_t now = millis();
  if (now - lastOledMs < 50) {
    return;
  }
  lastOledMs = now;
  oledDirty = false;
  oledDrawProbe();
}

static void dumpI2c() {
  Serial.println(F("I2C scan:"));
  uint8_t n = 0;
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() != 0) {
      continue;
    }
    Serial.print(F("  0x"));
    Serial.println(a, HEX);
    n++;
  }
  if (n == 0) {
    Serial.println(F("  nada"));
  }
}

static bool setupOledProbe() {
  oledAddr = 0;
  for (uint8_t a = 0x3C; a <= 0x3D; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      oledAddr = a;
      break;
    }
  }
  if (oledAddr == 0) {
    Serial.println(F("OLED ausente (0x3C/0x3D)"));
    oledOk = false;
    return false;
  }
  oledOk = oled.begin(SSD1306_SWITCHCAPVCC, oledAddr);
  if (!oledOk) {
    Serial.print(F("OLED 0x"));
    Serial.print(oledAddr, HEX);
    Serial.println(F(" no barramento mas begin() falhou"));
    return false;
  }
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);
  oled.setCursor(0, 4);
  oled.println(F("sonda hardware"));
  oled.print(F("OLED 0x"));
  oled.print(oledAddr, HEX);
  oled.print(F(" 128x32"));
  oled.display();
  Serial.print(F("OLED OK  0x"));
  Serial.println(oledAddr, HEX);
  oledDirty = true;
  return true;
}

static void printJoyBoot() {
  Serial.print(F("JOY  centro X="));
  Serial.print(joyCenterX);
  Serial.print(F(" Y="));
  Serial.print(joyCenterY);
  Serial.print(F("  SW="));
  Serial.print(joySw ? 1 : 0);
  if (joySw) {
    Serial.print(F("  << SW LOW no boot: clique colado ou fio no GND"));
  } else {
    Serial.print(F("  (solto ok)"));
  }
  Serial.println();
  printJoyMap();
  if (joyCenterX < 40 || joyCenterX > joyAdcMax - 40 ||
      joyCenterY < 40 || joyCenterY > joyAdcMax - 40) {
    Serial.println(F("JOY  centro nas pontas: VCC/GND do KY-023 ou eixo desligado"));
  }
}

static void printSelfTest() {
  Serial.println(F("--- self-test ---"));
  dumpI2c();
  Serial.print(F("MCP   "));
  Serial.println(mcpAlive ? F("OK") : F("FALHOU  RESET=3V3 A0-A2=GND"));
  Serial.print(F("OLED  "));
  if (oledOk) {
    Serial.print(F("OK  0x"));
    Serial.println(oledAddr, HEX);
  } else {
    Serial.println(F("FALHOU  VCC=3V3 SDA=GP0 SCK=GP1"));
  }
  printEncLine("EC11-1 oitava GP18/19", enc1);
  printEncLine("EC11-2 volume GP21/22", enc2);
  Serial.println(encSwap
                     ? F("EC11  painel 2|1  esquerda=EC11-2 GP21/22  direita=EC11-1 GP18/19")
                     : F("EC11  painel 1|2  esquerda=EC11-1 GP18/19  direita=EC11-2 GP21/22"));
  printJoyBoot();
  Serial.print(F("WS    "));
  if (wsOk) {
    Serial.print(F("OK  GP"));
    Serial.print(WS_PIN);
    Serial.print(F("  n="));
    Serial.print(WS_COUNT);
    Serial.println(F("  VCC=VBUS 5V  (L 0 rrggbb alpha)"));
  } else {
    Serial.println(F("FALHOU"));
  }
  Serial.print(F("I2S   "));
  Serial.print(i2sOk ? F("OK  ") : F("FALHOU  "));
  Serial.print(F("BCK=GP"));
  Serial.print(I2S_BCK_PIN);
  Serial.print(F(" LRCK=GP"));
  Serial.print(I2S_LRCK_PIN);
  Serial.print(F(" DOUT=GP"));
  Serial.print(I2S_DOUT_PIN);
  Serial.print(F(" DIN=GP"));
  Serial.print(I2S_DIN_PIN);
  Serial.println(F("  PCM5102A jack  (A m 0|1|2|3  mute|tom|teclas|clock  | A c verifica)"));
  audioCheckLink(true);
  Serial.println(F("teste: gire E1/E2 | mexa stick | clique SW | L r 1 | A m 1 | A c"));
  Serial.println(F("--- fim self-test ---"));
}

static void printPeriphStatus() {
  Serial.println(F("--- perifericos agora ---"));
  printEncLine("EC11-1 oitava GP18/19", enc1);
  printEncLine("EC11-2 volume GP21/22", enc2);
  Serial.println(encSwap ? F("EC11  2|1") : F("EC11  1|2"));
  Serial.print(F("JOY  X="));
  Serial.print(joyX);
  Serial.print(F(" Y="));
  Serial.print(joyY);
  Serial.print(F(" SW="));
  Serial.print(joySw ? 1 : 0);
  Serial.print(F("  centro "));
  Serial.print(joyCenterX);
  Serial.print('/');
  Serial.println(joyCenterY);
  printJoyMap();
  Serial.print(F("OLED "));
  Serial.println(oledOk ? F("ok") : F("falhou"));
  Serial.print(F("WS   "));
  Serial.print(wsOk ? F("ok") : F("falhou"));
  Serial.print(F("  master="));
  Serial.print(wsMaster);
  Serial.print(F("  rainbow="));
  Serial.println(wsRainbowOn ? 1 : 0);
  Serial.print(F("AUD  "));
  Serial.print(i2sOk ? F("ok") : F("falhou"));
  Serial.print(F("  modo="));
  Serial.print(static_cast<uint8_t>(audioMode));
  Serial.print(F("  vol="));
  Serial.println(audioVol);
  Serial.println(F("---"));
  periphMark();
}

static uint32_t audioHzToIncr(uint16_t hz) {
  return static_cast<uint32_t>((static_cast<uint64_t>(hz) << 32) / AUDIO_SR);
}

static void audioRecalcTone() {
  audioToneIncr = audioHzToIncr(audioToneHz);
}

static int16_t audioNextSample() {
  const int16_t amp = static_cast<int16_t>((static_cast<uint16_t>(audioVol) * 28000) / 127);
  if (audioMode == AUDIO_MUTE || !i2sOk) {
    return 0;
  }
  if (audioMode == AUDIO_TONE) {
    audioTonePhase += audioToneIncr;
    return (audioTonePhase & 0x80000000u) ? amp : static_cast<int16_t>(-amp);
  }
  if (audioMode == AUDIO_CLOCK) {
    audioClockTick++;
    const uint32_t period = AUDIO_SR / 2;
    const uint32_t pos = audioClockTick % period;
    return (pos < (AUDIO_SR / 400)) ? amp : 0;
  }
  int32_t mix = 0;
  uint8_t n = 0;
  for (uint8_t i = 0; i < AUDIO_VOICES; i++) {
    if (!audioVoices[i].on) {
      continue;
    }
    audioVoices[i].phase += audioVoices[i].incr;
    mix += (audioVoices[i].phase & 0x80000000u) ? amp : -amp;
    n++;
  }
  if (n == 0) {
    return 0;
  }
  return static_cast<int16_t>(mix / n);
}

static void audioPushFrame(int16_t s) {
  i2sOut.write16(s, s);
}

static void audioFill(uint16_t frames) {
  if (!i2sOk) {
    return;
  }
  uint16_t n = 0;
  while (n < frames && i2sOut.availableForWrite() >= 4) {
    audioPushFrame(audioNextSample());
    n++;
  }
}

static void audioTick() {
  audioFill(256);
}

static void audioSetKey(uint8_t ko, uint8_t ki, bool down) {
  if (ko > 3) {
    return;
  }
  const uint8_t note = PIANO_MIDI[ko][ki];
  if (down) {
    int8_t slot = -1;
    for (uint8_t i = 0; i < AUDIO_VOICES; i++) {
      if (audioVoices[i].on && audioVoices[i].note == note) {
        return;
      }
      if (!audioVoices[i].on && slot < 0) {
        slot = static_cast<int8_t>(i);
      }
    }
    if (slot < 0) {
      slot = 0;
    }
    // 440 * 2^((note-69)/12) ≈ tabela inteira via pow
    const float freq = 440.0f * powf(2.0f, (static_cast<int>(note) - 69) / 12.0f);
    audioVoices[slot].note = note;
    audioVoices[slot].incr = audioHzToIncr(static_cast<uint16_t>(freq + 0.5f));
    audioVoices[slot].phase = 0;
    audioVoices[slot].on = true;
    return;
  }
  for (uint8_t i = 0; i < AUDIO_VOICES; i++) {
    if (audioVoices[i].on && audioVoices[i].note == note) {
      audioVoices[i].on = false;
    }
  }
}

static void audioSampleEdges(uint16_t us) {
  const uint32_t mb = 1u << I2S_BCK_PIN;
  const uint32_t ml = 1u << I2S_LRCK_PIN;
  const uint32_t md = 1u << I2S_DOUT_PIN;
  const uint32_t mi = 1u << I2S_DIN_PIN;
  uint32_t prev = sio_hw->gpio_in;
  uint16_t eb = 0, el = 0, ed = 0, ei = 0;
  const uint32_t t0 = micros();
  while (static_cast<uint16_t>(micros() - t0) < us) {
    const uint32_t now = sio_hw->gpio_in;
    const uint32_t diff = now ^ prev;
    if (diff & mb) {
      eb++;
    }
    if (diff & ml) {
      el++;
    }
    if (diff & md) {
      ed++;
    }
    if (diff & mi) {
      ei++;
    }
    prev = now;
    if (eb > 60000) {
      break;
    }
  }
  audioEdgeBck = eb;
  audioEdgeLrck = el;
  audioEdgeDout = ed;
  audioEdgeDin = ei;
}

static void audioCheckLink(bool verbose) {
  const AudioProbeMode savedMode = audioMode;
  const uint8_t savedVol = audioVol;
  audioMode = AUDIO_TONE;
  if (audioVol < 48) {
    audioVol = 48;
  }
  audioRecalcTone();
  audioFill(1024);
  delay(6);
  audioFill(256);
  audioSampleEdges(4000);
  audioUnderrun = i2sOk && i2sOut.getUnderflow();
  audioPinBck = audioEdgeBck >= 24;
  audioPinLrck = audioEdgeLrck >= 8;
  audioPinDout = audioEdgeDout >= 8;
  audioPinDin = audioEdgeDin >= 20 && audioEdgeDin * 4u >= audioEdgeDout;
  audioLinkOk = i2sOk && audioPinBck && audioPinLrck && audioPinDout;
  audioChecked = true;
  audioMode = savedMode;
  audioVol = savedVol;
  if (!verbose) {
    return;
  }
  Serial.print(F("AUD  check  i2s="));
  Serial.print(i2sOk ? 1 : 0);
  Serial.print(F("  bck="));
  Serial.print(audioPinBck ? F("OK") : F("PARADO"));
  Serial.print(F(":"));
  Serial.print(audioEdgeBck);
  Serial.print(F("  lrck="));
  Serial.print(audioPinLrck ? F("OK") : F("PARADO"));
  Serial.print(F(":"));
  Serial.print(audioEdgeLrck);
  Serial.print(F("  dout="));
  Serial.print(audioPinDout ? F("OK") : F("MUDO"));
  Serial.print(F(":"));
  Serial.print(audioEdgeDout);
  Serial.print(F("  din="));
  Serial.print(audioPinDin ? F("LOOP") : F("NADA"));
  Serial.print(F(":"));
  Serial.print(audioEdgeDin);
  Serial.print(F("  und="));
  Serial.println(audioUnderrun ? 1 : 0);
  if (!i2sOk) {
    Serial.println(F("AUD  I2S nao arrancou — PIO/driver"));
  } else if (!audioPinBck || !audioPinLrck) {
    Serial.println(F("AUD  relogio parado: GP10 BCK / GP11 LRCK. Conferir fios no PCM5102A"));
  } else if (!audioPinDout) {
    Serial.println(F("AUD  GP12 DOUT sem transicoes — sem dados a sair"));
  } else {
    Serial.println(F("AUD  Pico a enviar I2S (BCK+LRCK+DOUT). Analogico no jack nao se le daqui."));
  }
  if (audioPinDin) {
    Serial.println(F("AUD  DIN GP13 ouve o DOUT — loopback digital ok"));
  } else {
    Serial.println(F("AUD  DIN sem retorno (normal: PCM5102A so recebe). Loop = jumper GP12→GP13"));
  }
  if (uiStream) {
    emitJsonState();
  }
}

static bool setupAudioProbe() {
  pinMode(I2S_DIN_PIN, INPUT);
  i2sOut.setBCLK(I2S_BCK_PIN);
  i2sOut.setDOUT(I2S_DOUT_PIN);
  i2sOut.setBitsPerSample(16);
  i2sOut.setBuffers(6, 256);
  i2sOk = i2sOut.begin(static_cast<long>(AUDIO_SR));
  audioMode = AUDIO_TONE;
  audioVol = 80;
  audioToneHz = 440;
  audioTonePhase = 0;
  audioClockTick = 0;
  audioRecalcTone();
  memset(audioVoices, 0, sizeof(audioVoices));
  if (i2sOk) {
    audioFill(768);
  }
  Serial.print(F("AUD  I2S "));
  Serial.println(i2sOk ? F("ok  44.1k/16  GP10/11/12  tom 440") : F("falhou"));
  return i2sOk;
}

static void handleAudioCommand(char *line) {
  while (*line == ' ') {
    line++;
  }
  if (*line == '\0') {
    Serial.println(F("A  uso: A m 0|1|2|3 | A v 0-127 | A t 110-1760 | A c"));
    return;
  }
  const char cmd = *line++;
  while (*line == ' ') {
    line++;
  }
  if (cmd == 'm' || cmd == 'M') {
    const int m = atoi(line);
    if (m >= AUDIO_MUTE && m <= AUDIO_CLOCK) {
      audioMode = static_cast<AudioProbeMode>(m);
      audioTonePhase = 0;
      audioClockTick = 0;
    }
    Serial.print(F("AUD  modo="));
    Serial.println(static_cast<uint8_t>(audioMode));
    periphMark();
    return;
  }
  if (cmd == 'v' || cmd == 'V') {
    int v = atoi(line);
    if (v < 0) {
      v = 0;
    }
    if (v > 127) {
      v = 127;
    }
    audioVol = static_cast<uint8_t>(v);
    Serial.print(F("AUD  vol="));
    Serial.println(audioVol);
    periphMark();
    return;
  }
  if (cmd == 'c' || cmd == 'C' || cmd == 'k' || cmd == 'K') {
    audioCheckLink(true);
    return;
  }
  if (cmd == 't' || cmd == 'T') {
    int hz = atoi(line);
    if (hz < 110) {
      hz = 110;
    }
    if (hz > 1760) {
      hz = 1760;
    }
    audioToneHz = static_cast<uint16_t>(hz);
    audioRecalcTone();
    Serial.print(F("AUD  tom="));
    Serial.println(audioToneHz);
    periphMark();
    return;
  }
  Serial.println(F("A  uso: A m 0|1|2|3 | A v 0-127 | A t 110-1760 | A c"));
}

static void setupPeripherals(bool mcpOkFlag) {
  mcpAlive = mcpOkFlag;

  initEnc(enc1);
  initEnc(enc2);
  calibrateJoyProbe();

  setupOledProbe();
  setupWsProbe();
  setupAudioProbe();
  printSelfTest();
  periphMark();
}

static void pollPeripherals() {
  audioTick();
  wsRainbowTick();
  scanEncodersProbe();
  scanJoyProbe();
  oledTickProbe();
  if (uiStream && millis() - lastJsonMs >= 50) {
    lastJsonMs = millis();
    emitJsonState();
  }
}

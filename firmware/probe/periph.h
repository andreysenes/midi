#pragma once

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Pinagem = firmware/pico/config.h. Sonda testa o hardware, nao manda MIDI.

static const uint8_t ENC1_A = 18; // EC11-1 oitava
static const uint8_t ENC1_B = 19;
static const uint8_t ENC2_A = 21; // EC11-2 volume
static const uint8_t ENC2_B = 22;
static const uint8_t JOY_X_PIN = 26;
static const uint8_t JOY_Y_PIN = 27;
static const uint8_t JOY_SW_PIN = 20;
static const uint8_t WS_PIN = 16;
static const uint8_t WS_COUNT = 8;

static uint8_t wsRgb[WS_COUNT][3];
static uint8_t wsMode = 0;
static uint8_t wsChase = 0;
static uint32_t lastWsMs = 0;
static bool wsReady = false;

static bool (*gHeld)[8] = nullptr;
static bool (*gStuck)[8] = nullptr;

static Adafruit_SSD1306 oled(128, 32, &Wire, -1, 400000UL, 400000UL);
static bool oledOk = false;
static uint8_t oledAddr = 0;
static bool oledDirty = true;
static uint32_t lastOledMs = 0;

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

static int joyCenterX = 2048;
static int joyCenterY = 2048;
static int joyX = 2048;
static int joyY = 2048;
static bool joySw = false;
static uint32_t lastJoyPrintMs = 0;

static uint8_t lastKo = 255;
static uint8_t lastKi = 255;
static bool lastKeyDown = false;
static const char *lastKeyName = "-";

static bool mcpAlive = false;
static bool uiStream = false;
static uint32_t lastJsonMs = 0;

static void periphMark() {
  oledDirty = true;
}

static void periphBindMatrix(bool held[7][8], bool stuck[7][8]) {
  gHeld = held;
  gStuck = stuck;
}

static void ws2812Bit(uint8_t bit) {
  if (bit) {
    digitalWrite(WS_PIN, HIGH);
    delayMicroseconds(1);
    digitalWrite(WS_PIN, LOW);
    delayMicroseconds(0);
  } else {
    digitalWrite(WS_PIN, HIGH);
    delayMicroseconds(0);
    digitalWrite(WS_PIN, LOW);
    delayMicroseconds(1);
  }
}

static void ws2812Byte(uint8_t value) {
  for (int8_t i = 7; i >= 0; i--) {
    ws2812Bit((value >> i) & 1);
  }
}

static void wsShow() {
  if (!wsReady) {
    return;
  }
  noInterrupts();
  for (uint8_t i = 0; i < WS_COUNT; i++) {
    ws2812Byte(wsRgb[i][1]);
    ws2812Byte(wsRgb[i][0]);
    ws2812Byte(wsRgb[i][2]);
  }
  interrupts();
  delayMicroseconds(60);
}

static void wsFill(uint8_t r, uint8_t g, uint8_t b) {
  for (uint8_t i = 0; i < WS_COUNT; i++) {
    wsRgb[i][0] = r;
    wsRgb[i][1] = g;
    wsRgb[i][2] = b;
  }
  wsShow();
}

static void wsOff() {
  wsFill(0, 0, 0);
}

static void wsApplyMode() {
  switch (wsMode) {
  case 0:
    wsOff();
    break;
  case 1:
    wsFill(24, 24, 24);
    break;
  case 2:
    wsFill(48, 0, 0);
    break;
  case 3:
    wsFill(0, 48, 0);
    break;
  case 4:
    wsFill(0, 0, 48);
    break;
  case 5:
    for (uint8_t i = 0; i < WS_COUNT; i++) {
      wsRgb[i][0] = 0;
      wsRgb[i][1] = 0;
      wsRgb[i][2] = 0;
    }
    wsRgb[wsChase % WS_COUNT][2] = 64;
    wsShow();
    break;
  default:
    wsMode = 0;
    wsOff();
    break;
  }
}

static void wsCycleMode() {
  wsMode = static_cast<uint8_t>((wsMode + 1) % 6);
  wsChase = 0;
  wsApplyMode();
  Serial.print(F("WS2812 modo "));
  Serial.print(wsMode);
  Serial.println(F("  (w=proximo  w0=apagar)"));
}

static void wsTick() {
  if (!wsReady || wsMode != 5) {
    return;
  }
  const uint32_t now = millis();
  if (now - lastWsMs < 120) {
    return;
  }
  lastWsMs = now;
  wsChase = static_cast<uint8_t>((wsChase + 1) % WS_COUNT);
  wsApplyMode();
}

static bool setupWsProbe() {
  pinMode(WS_PIN, OUTPUT);
  digitalWrite(WS_PIN, LOW);
  wsReady = true;
  wsMode = 0;
  wsOff();
  return true;
}

static bool periphHandleChar(char c) {
  if (c == 'w') {
    wsCycleMode();
    periphMark();
    return true;
  }
  if (c == '0') {
    wsMode = 0;
    wsOff();
    Serial.println(F("WS2812 apagado"));
    periphMark();
    return true;
  }
  return false;
}

static void periphNoteMatrix(uint8_t ko, uint8_t ki, bool down, const char *name) {
  lastKo = ko;
  lastKi = ki;
  lastKeyDown = down;
  lastKeyName = name;
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
  Serial.print(F(",\"e1\":"));
  Serial.print(enc1.count);
  Serial.print(F(",\"e2\":"));
  Serial.print(enc2.count);
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
  Serial.print(F(",\"ko\":"));
  Serial.print(lastKo == 255 ? -1 : lastKo);
  Serial.print(F(",\"ki\":"));
  Serial.print(lastKi == 255 ? -1 : lastKi);
  Serial.print(F(",\"dn\":"));
  Serial.print(lastKeyDown ? 1 : 0);
  Serial.print(F(",\"n\":\""));
  Serial.print(lastKeyName);
  Serial.print(F("\",\"hb\":["));
  for (uint8_t row = 0; row < 7; row++) {
    uint8_t mask = 0;
    if (gHeld != nullptr) {
      for (uint8_t col = 0; col < 8; col++) {
        if ((*gHeld)[row][col]) {
          mask |= static_cast<uint8_t>(1 << col);
        }
      }
    }
    Serial.print(mask);
    if (row + 1 < 7) {
      Serial.print(',');
    }
  }
  Serial.print(F("],\"sb\":["));
  for (uint8_t row = 0; row < 7; row++) {
    uint8_t mask = 0;
    if (gStuck != nullptr) {
      for (uint8_t col = 0; col < 8; col++) {
        if ((*gStuck)[row][col]) {
          mask |= static_cast<uint8_t>(1 << col);
        }
      }
    }
    Serial.print(mask);
    if (row + 1 < 7) {
      Serial.print(',');
    }
  }
  Serial.print(F("],\"wsm\":"));
  Serial.print(wsMode);
  Serial.print(F(",\"ws\":["));
  for (uint8_t i = 0; i < WS_COUNT; i++) {
    Serial.print('[');
    Serial.print(wsRgb[i][0]);
    Serial.print(',');
    Serial.print(wsRgb[i][1]);
    Serial.print(',');
    Serial.print(wsRgb[i][2]);
    Serial.print(']');
    if (i + 1 < WS_COUNT) {
      Serial.print(',');
    }
  }
  Serial.print(F("],\"led\":"));
  Serial.print(digitalRead(25) == HIGH ? 1 : 0);
  Serial.println(F("}"));
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

static void initEnc(EncProbe &e) {
  pinMode(e.a, INPUT_PULLUP);
  pinMode(e.b, INPUT_PULLUP);
  e.prev = (digitalRead(e.a) << 1) | digitalRead(e.b);
  e.accum = 0;
  e.count = 0;
  e.lastDir = 0;
}

static int8_t encStep(EncProbe &e) {
  const uint8_t current = (digitalRead(e.a) << 1) | digitalRead(e.b);
  const uint8_t pack = (e.prev << 2) | current;
  int8_t dir = 0;
  if (pack == 0b0001 || pack == 0b0111 || pack == 0b1110 || pack == 0b1000) {
    dir = 1;
  } else if (pack == 0b0010 || pack == 0b0100 || pack == 0b1101 || pack == 0b1011) {
    dir = -1;
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
  } else if (a == 1 && b == 1 && e.count == 0) {
    Serial.print(F("  (gire para testar)"));
  }
  Serial.println();
}

static void scanEncodersProbe() {
  const int8_t s1 = encStep(enc1);
  if (s1 != 0) {
    Serial.print(F("EC11-1  "));
    Serial.print(s1 > 0 ? '+' : '-');
    Serial.print(F("  count="));
    Serial.println(enc1.count);
    periphMark();
  }
  const int8_t s2 = encStep(enc2);
  if (s2 != 0) {
    Serial.print(F("EC11-2  "));
    Serial.print(s2 > 0 ? '+' : '-');
    Serial.print(F("  count="));
    Serial.println(enc2.count);
    periphMark();
  }
}

static void calibrateJoyProbe() {
  analogReadResolution(12);
  pinMode(JOY_SW_PIN, INPUT_PULLUP);
  analogRead(JOY_X_PIN);
  analogRead(JOY_Y_PIN);
  delay(10);
  long sx = 0;
  long sy = 0;
  for (uint8_t i = 0; i < 32; i++) {
    sx += analogRead(JOY_X_PIN);
    sy += analogRead(JOY_Y_PIN);
    delay(2);
  }
  joyCenterX = static_cast<int>(sx / 32);
  joyCenterY = static_cast<int>(sy / 32);
  joyX = joyCenterX;
  joyY = joyCenterY;
  joySw = digitalRead(JOY_SW_PIN) == LOW;
}

static void scanJoyProbe() {
  const int x = analogRead(JOY_X_PIN);
  const int y = analogRead(JOY_Y_PIN);
  const bool sw = digitalRead(JOY_SW_PIN) == LOW;
  const bool moved = abs(x - joyX) > 24 || abs(y - joyY) > 24 || sw != joySw;
  const bool swChanged = sw != joySw;
  joyX = x;
  joyY = y;
  joySw = sw;
  if (!moved) {
    return;
  }
  periphMark();

  const uint32_t now = millis();
  if (!swChanged && now - lastJoyPrintMs < 200) {
    return;
  }
  lastJoyPrintMs = now;
  Serial.print(F("JOY  X="));
  Serial.print(x);
  Serial.print(F(" Y="));
  Serial.print(y);
  Serial.print(F(" SW="));
  Serial.println(sw ? 1 : 0);
}

static void oledDrawProbe() {
  if (!oledOk) {
    return;
  }
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);

  oled.setCursor(0, 0);
  oled.print(F("E1"));
  if (enc1.count >= 0) {
    oled.print('+');
  }
  oled.print(enc1.count);
  oled.setCursor(64, 0);
  oled.print(F("E2"));
  if (enc2.count >= 0) {
    oled.print('+');
  }
  oled.print(enc2.count);

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
    oled.print(F("MCP ok"));
  } else {
    oled.print(F("MCP --"));
  }

  oled.setCursor(0, 24);
  if (mcpAlive && lastKo != 255) {
    oled.print(lastKeyDown ? F("DN ") : F("UP "));
    oled.print(F("KO"));
    oled.print(lastKo);
    oled.print(F(" KI"));
    oled.print(lastKi);
    oled.print(' ');
    oled.print(lastKeyName);
  } else if (mcpAlive) {
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

static uint8_t probeI2cAddr(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission();
}

static void dumpI2c() {
  Serial.println(F("I2C scan:"));
  uint8_t n = 0;
  for (uint8_t a = 0x08; a < 0x78; a++) {
    if (probeI2cAddr(a) != 0) {
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
  if (probeI2cAddr(0x3C) == 0) {
    oledAddr = 0x3C;
  } else if (probeI2cAddr(0x3D) == 0) {
    oledAddr = 0x3D;
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
  delay(250);
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
  if (joyCenterX < 200 || joyCenterX > 3900 || joyCenterY < 200 || joyCenterY > 3900) {
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
  printJoyBoot();
  Serial.print(F("WS    DIN=GP"));
  Serial.print(WS_PIN);
  Serial.print(F("  n="));
  Serial.print(WS_COUNT);
  Serial.println(F("  VCC=VBUS 5V  (w=ciclar teste  w0=apagar)"));
  Serial.println(F("teste: gire E1/E2 | mexa stick | clique SW | w=LED"));
  Serial.println(F("--- fim self-test ---"));
}

static void printPeriphStatus() {
  Serial.println(F("--- perifericos agora ---"));
  printEncLine("EC11-1", enc1);
  printEncLine("EC11-2", enc2);
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
  Serial.print(F("OLED "));
  Serial.println(oledOk ? F("ok") : F("falhou"));
  Serial.print(F("WS2812 "));
  Serial.print(wsReady ? F("ok") : F("falhou"));
  Serial.print(F("  modo "));
  Serial.println(wsMode);
  Serial.println(F("---"));
  periphMark();
}

static void setupPeripherals(bool mcpOk) {
  mcpAlive = mcpOk;

  initEnc(enc1);
  initEnc(enc2);
  calibrateJoyProbe();

  setupOledProbe();
  setupWsProbe();
  printSelfTest();
  periphMark();
}

static void pollPeripherals() {
  scanEncodersProbe();
  scanJoyProbe();
  wsTick();
  oledTickProbe();
  if (uiStream && millis() - lastJsonMs >= 50) {
    lastJsonMs = millis();
    emitJsonState();
  }
}

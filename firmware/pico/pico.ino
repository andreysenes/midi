#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_MCP23X17.h>
#include <Adafruit_TinyUSB.h>
#include <MIDI.h>
#include "config.h"
#include "oled.h"
#include "audio.h"
#include "midi_map.h"
#include "ws.h"

Adafruit_MCP23X17 mcp;
Adafruit_USBD_MIDI usbMidi;
MIDI_CREATE_INSTANCE(Adafruit_USBD_MIDI, usbMidi, MIDI);

static bool held[KO_COUNT][KI_COUNT];
static int8_t octave = 0;
static uint8_t volume = 100;
static uint8_t program = 0;
static uint8_t typedProgram = 0;
static uint8_t typedDigits = 0;
static uint32_t lastDigitMs = 0;
static uint32_t lastButtonMs = 0;
static uint8_t lastNoteShown = 0;
static bool haveLastNote = false;
static bool lastJoySw = false;
static bool padDown[PAD_COUNT];
static uint32_t lastSerialMs = 0;

struct EncoderState {
  uint8_t prev;
  int8_t accum;
};

static EncoderState encOctave;
static EncoderState encVolume;
static int16_t encCount[2];
static int joyCenterX = 2048;
static int joyCenterY = 2048;
static int joyAdcMax = 4095;
static int lastJoyX = 2048;
static int lastJoyY = 2048;
static bool uiStream = false;
static uint32_t lastJsonMs = 0;

// MIDI clock from DAW (24 PPQN). Beat 0..3 = tempos 1..4.
// Clock sozinho já conta — muitas DAWs não reenviam Start se o Pico ligou a meio.
// Sem clock USB, o Play local (STOP) leva um metrônomo a 120 BPM só para os LEDs.
static bool midiClockRunning = false;
static uint8_t midiClockCount = 0;
static uint8_t midiBeat = 0;
static uint8_t midiStep = 0;  // 0..7 colcheias (LED 1..8)
static uint16_t midiSongPos = 0;
static bool transportPlaying = false;  // local play/stop for STOP key
static uint32_t lastDawClockMs = 0;
static uint32_t lastInternalBeatMs = 0;
static const uint16_t INTERNAL_BPM = 120;
static const uint16_t DAW_CLOCK_HOLD_MS = 200;

static void resetTempoPos() {
  midiClockCount = 0;
  midiBeat = 0;
  midiStep = 0;
}

static void refreshWsFromState() {
  if (selHeld) {
    wsShowBank(padBank);
  } else if (midiClockRunning || transportPlaying) {
    wsShowTempo(midiStep);
  } else {
    wsShowOff();
  }
}

static void onMidiClock() {
  lastDawClockMs = millis();
  midiClockRunning = true;
  midiClockCount++;
  if (midiClockCount == 12) {
    midiStep = static_cast<uint8_t>((midiBeat * 2 + 1) & 7);
    if (!selHeld) {
      wsShowTempo(midiStep);
    }
  } else if (midiClockCount >= 24) {
    midiClockCount = 0;
    midiBeat = static_cast<uint8_t>((midiBeat + 1) & 3);
    midiStep = static_cast<uint8_t>((midiBeat * 2) & 7);
    oledMark();
    if (!selHeld) {
      wsShowTempo(midiStep);
    }
  }
}

static void onMidiStart() {
  midiClockRunning = true;
  transportPlaying = true;
  resetTempoPos();
  midiSongPos = 0;
  lastInternalBeatMs = millis();
  oledMark();
  refreshWsFromState();
}

static void onMidiContinue() {
  midiClockRunning = true;
  transportPlaying = true;
  lastInternalBeatMs = millis();
  oledMark();
  refreshWsFromState();
}

static void onMidiStop() {
  midiClockRunning = false;
  transportPlaying = false;
  resetTempoPos();
  lastDawClockMs = 0;
  oledMark();
  refreshWsFromState();
}

static void tickInternalTempo() {
  if (selHeld || !transportPlaying) {
    return;
  }
  const uint32_t now = millis();
  if (lastDawClockMs != 0 && (now - lastDawClockMs) < DAW_CLOCK_HOLD_MS) {
    return;
  }
  const uint32_t stepMs = 60000UL / INTERNAL_BPM / 2;
  if (now - lastInternalBeatMs < stepMs) {
    return;
  }
  lastInternalBeatMs = now;
  midiStep = static_cast<uint8_t>((midiStep + 1) & 7);
  midiBeat = static_cast<uint8_t>(midiStep >> 1);
  midiClockRunning = true;
  if ((midiStep & 1) == 0) {
    oledMark();
  }
  wsShowTempo(midiStep);
}

static void onMidiSongPos(unsigned int beats) {
  // Song Position Pointer: MIDI beats (16th notes). 4 sixteenths = 1 quarter.
  midiSongPos = static_cast<uint16_t>(beats);
  midiBeat = static_cast<uint8_t>((beats / 4) & 3);
  midiStep = static_cast<uint8_t>((beats / 2) & 7);
  midiClockCount = static_cast<uint8_t>((beats % 4) * 6);
  oledMark();
  refreshWsFromState();
}

static uint8_t clampU7(int value) {
  if (value < 0) {
    return 0;
  }
  if (value > 127) {
    return 127;
  }
  return static_cast<uint8_t>(value);
}

static uint8_t applyOctave(uint8_t note) {
  const int shifted = static_cast<int>(note) + (octave * 12);
  if (shifted < 0) {
    return 0;
  }
  if (shifted > 127) {
    return 127;
  }
  return static_cast<uint8_t>(shifted);
}

static void emitNoteOn(uint8_t note, uint8_t velocity, uint8_t ch) {
  MIDI.sendNoteOn(note, velocity, ch);
}

static void emitNoteOff(uint8_t note, uint8_t ch) {
  MIDI.sendNoteOff(note, 0, ch);
}

static void emitCc(uint8_t cc, uint8_t value, uint8_t ch) {
  MIDI.sendControlChange(cc, value, ch);
}

static void emitProgram(uint8_t value, uint8_t ch) {
  MIDI.sendProgramChange(value, ch);
}

static void emitPitchBend(int16_t value, uint8_t ch) {
  MIDI.sendPitchBend(value, ch);
}

static OledStatus currentOledStatus(bool mcpOk) {
  OledStatus st;
  st.octave = octave;
  st.volume = volume;
  st.program = program;
  st.typedDigits = typedDigits;
  st.typedProgram = typedProgram;
  st.lastNote = lastNoteShown;
  st.haveNote = haveLastNote;
  st.sustain = lastJoySw;
  st.usbMounted = TinyUSBDevice.mounted();
  st.mcpOk = mcpOk;
  st.perfBank = perfBank;
  st.padBank = padBank;
  st.selHeld = selHeld;
  st.clockRunning = midiClockRunning;
  st.transportPlaying = transportPlaying;
  st.beat = midiBeat;
  return st;
}

static void emitJsonState() {
  uint8_t h[7] = {};
  uint8_t pads = 0;
  for (uint8_t r = 0; r < KO_COUNT; r++) {
    for (uint8_t c = 0; c < KI_COUNT; c++) {
      if (held[r][c]) {
        h[r] |= static_cast<uint8_t>(1u << c);
      }
    }
  }
  for (uint8_t d = 0; d < PAD_COUNT; d++) {
    if (padDown[d]) {
      pads |= static_cast<uint8_t>(1u << d);
    }
  }
  Serial.print(F("J{\"t\":\"s\",\"e1\":"));
  Serial.print(encCount[0]);
  Serial.print(F(",\"e2\":"));
  Serial.print(encCount[1]);
  Serial.print(F(",\"x\":"));
  Serial.print(lastJoyX);
  Serial.print(F(",\"y\":"));
  Serial.print(lastJoyY);
  Serial.print(F(",\"sw\":"));
  Serial.print(lastJoySw ? 1 : 0);
  Serial.print(F(",\"cx\":"));
  Serial.print(joyCenterX);
  Serial.print(F(",\"cy\":"));
  Serial.print(joyCenterY);
  Serial.print(F(",\"oct\":"));
  Serial.print(octave);
  Serial.print(F(",\"vol\":"));
  Serial.print(volume);
  Serial.print(F(",\"pb\":"));
  Serial.print(perfBank);
  Serial.print(F(",\"bb\":"));
  Serial.print(padBank);
  Serial.print(F(",\"sel\":"));
  Serial.print(selHeld ? 1 : 0);
  Serial.print(F(",\"note\":"));
  Serial.print(haveLastNote ? lastNoteShown : -1);
  Serial.print(F(",\"pads\":"));
  Serial.print(pads);
  Serial.print(F(",\"ws\":"));
  Serial.print(wsOk ? 1 : 0);
  Serial.print(F(",\"wr\":"));
  Serial.print(wsRainbowOn ? 1 : 0);
  Serial.print(F(",\"beat\":"));
  Serial.print(midiBeat);
  Serial.print(F(",\"clk\":"));
  Serial.print(midiClockRunning ? 1 : 0);
  Serial.print(F(",\"play\":"));
  Serial.print(transportPlaying ? 1 : 0);
  Serial.print(F(",\"h\":["));
  for (uint8_t r = 0; r < 7; r++) {
    if (r) {
      Serial.print(',');
    }
    Serial.print(h[r]);
  }
  Serial.println(F("]}"));
}

static void sendNote(uint8_t baseNote, bool on) {
  const uint8_t note = applyOctave(baseNote);
  lastNoteShown = note;
  haveLastNote = true;
  Serial.print(on ? F("KEY on ") : F("KEY off "));
  Serial.println(note);
  oledMark();
  if (on) {
    emitNoteOn(note, midiMap.vel, midiMap.ch);
  } else {
    emitNoteOff(note, midiMap.ch);
  }
}

static void allNotesOff() {
  emitCc(123, 0, midiMap.ch);
  emitCc(120, 0, midiMap.ch);
  // Pads (canal 2) — limpa notas/CC residuais.
  emitCc(123, 0, 2);
  emitCc(120, 0, 2);
  emitCc(midiMap.tempoCcUp, 0, midiMap.tempoCh);
  emitCc(midiMap.tempoCcDn, 0, midiMap.tempoCh);
}

static void sendEncCc(uint8_t i) {
  const EncSlot &e = encSlot(i);
  if (e.mode != ENC_MODE_CC) {
    return;
  }
  emitCc(e.cc, encVal[perfBank][i], e.ch);
}

static void handlePad(uint8_t digit, bool pressed) {
  const PadSlot &p = padSlot(digit);
  if (p.mode == PAD_OFF) {
    return;
  }
  if (p.mode == PAD_DUAL) {
    if (pressed) {
      emitNoteOn(p.note, midiMap.vel, p.ch);
      emitCc(p.cc, 127, p.ch);
    } else {
      emitNoteOff(p.note, p.ch);
      emitCc(p.cc, 0, p.ch);
    }
    return;
  }
  if (p.mode == PAD_CC) {
    emitCc(p.cc, pressed ? 127 : 0, p.ch);
    return;
  }
  if (p.mode == PAD_NOTE) {
    if (pressed) {
      emitNoteOn(p.note, midiMap.vel, p.ch);
    } else {
      emitNoteOff(p.note, p.ch);
    }
    return;
  }
  if (p.mode == PAD_PROG && pressed) {
    program = p.note;
    emitProgram(program, p.ch);
    oledMark();
  }
}

static void changeBank(bool pad, int8_t dir) {
  uint8_t *b = pad ? &padBank : &perfBank;
  int next = static_cast<int>(*b) + dir;
  if (next < 0) {
    next = BANK_COUNT - 1;
  }
  if (next >= BANK_COUNT) {
    next = 0;
  }
  *b = static_cast<uint8_t>(next);
  if (!pad) {
    sendEncCc(0);
    sendEncCc(1);
  }
  oledMark();
  refreshWsFromState();
  Serial.print(F("BANK pb="));
  Serial.print(perfBank);
  Serial.print(F(" bb="));
  Serial.println(padBank);
}

static bool tempoHeld[2];

static void emitTempoCc(uint8_t which, bool pressed) {
  const uint8_t cc = which ? midiMap.tempoCcDn : midiMap.tempoCcUp;
  emitCc(cc, pressed ? 127 : 0, midiMap.tempoCh);
}

static void handleButton(uint8_t code, bool pressed) {
  if (code == BTN_SEL) {
    selHeld = pressed;
    oledMark();
    refreshWsFromState();
    return;
  }

  if (code >= 200 && code <= 209) {
    const uint8_t d = static_cast<uint8_t>(code - 200);
    if (pressed == padDown[d]) {
      return;
    }
    padDown[d] = pressed;
    handlePad(d, pressed);
    return;
  }

  if (code == BTN_TEMPO_UP || code == BTN_TEMPO_DN) {
    const uint8_t which = (code == BTN_TEMPO_DN) ? 1 : 0;
    if (pressed == tempoHeld[which]) {
      return;
    }
    tempoHeld[which] = pressed;
    emitTempoCc(which, pressed);
    if (pressed) {
      changeBank(selHeld, which ? -1 : 1);
    }
    return;
  }

  if (!pressed) {
    return;
  }
  const uint32_t now = millis();
  if (now - lastButtonMs < BUTTON_DEBOUNCE_MS) {
    return;
  }
  lastButtonMs = now;
  if (code == BTN_STOP) {
    // Play / Stop da DAW (MIDI realtime Start/Stop).
    if (transportPlaying || midiClockRunning) {
      MIDI.sendStop();
      transportPlaying = false;
      midiClockRunning = false;
      resetTempoPos();
      allNotesOff();
      Serial.println(F("TRANSPORT stop"));
    } else {
      MIDI.sendStart();
      transportPlaying = true;
      midiClockRunning = true;
      resetTempoPos();
      lastDawClockMs = 0;
      lastInternalBeatMs = millis();
      Serial.println(F("TRANSPORT start"));
    }
    oledMark();
    refreshWsFromState();
    return;
  }
  if (code == BTN_DEMO) {
    allNotesOff();
  }
}

static void handleCell(uint8_t row, uint8_t col, bool pressed) {
  if (MCP_KO_REV) {
    row = static_cast<uint8_t>(KO_COUNT - 1 - row);
  }
  if (MCP_KI_REV) {
    col = static_cast<uint8_t>(KI_COUNT - 1 - col);
  }
  const uint8_t code = noteMatrix[row][col];
  if (code == CELL_EMPTY) {
    return;
  }
  if (code <= 127) {
    sendNote(code, pressed);
    return;
  }
  handleButton(code, pressed);
}

static bool setupMcp() {
  if (!mcp.begin_I2C(MCP_ADDR, &Wire)) {
    return false;
  }

  for (uint8_t i = 0; i < KI_COUNT; i++) {
    mcp.pinMode(i, INPUT_PULLUP);
  }
  for (uint8_t i = 0; i < KO_COUNT; i++) {
    mcp.pinMode(8 + i, OUTPUT);
    mcp.digitalWrite(8 + i, HIGH);
  }
  mcp.pinMode(15, INPUT_PULLUP);
  return true;
}

static void scanMatrix() {
  for (uint8_t row = 0; row < KO_COUNT; row++) {
    mcp.writeGPIO(static_cast<uint8_t>(~(1 << row)) & 0x7F, MCP_KO_PORT);
    delayMicroseconds(SCAN_SETTLE_US);
    const uint8_t cols = mcp.readGPIO(MCP_KI_PORT);

    for (uint8_t col = 0; col < KI_COUNT; col++) {
      const bool pressed = (cols & (1 << col)) == 0;
      if (pressed == held[row][col]) {
        continue;
      }
      held[row][col] = pressed;
      handleCell(row, col, pressed);
    }
  }
  mcp.writeGPIO(0x7F, MCP_KO_PORT);
}

static void initEncoder(const EncoderPins &pins, EncoderState &state) {
  pinMode(pins.a, INPUT_PULLUP);
  pinMode(pins.b, INPUT_PULLUP);
  state.prev = (digitalRead(pins.a) << 1) | digitalRead(pins.b);
  state.accum = 0;
}

static int8_t readEncoderStep(const EncoderPins &pins, EncoderState &state) {
  const uint8_t current = (digitalRead(pins.a) << 1) | digitalRead(pins.b);
  const uint8_t pack = (state.prev << 2) | current;
  int8_t dir = 0;

  if (pack == 0b0001 || pack == 0b0111 || pack == 0b1110 || pack == 0b1000) {
    dir = 1;
  } else if (pack == 0b0010 || pack == 0b0100 || pack == 0b1101 || pack == 0b1011) {
    dir = -1;
  }

  state.prev = current;
  if (dir == 0) {
    return 0;
  }

  state.accum += dir;
  if (state.accum >= 4) {
    state.accum = 0;
    return 1;
  }
  if (state.accum <= -4) {
    state.accum = 0;
    return -1;
  }
  return 0;
}

static int16_t lastBend = 1;
static uint8_t lastJoyCcX = 255;
static uint8_t lastJoyCcY = 255;
static int8_t joyOctGate = 0;

static void readJoyMapped(int *outX, int *outY) {
  int rx = analogRead(JOY_X_PIN);
  int ry = analogRead(JOY_Y_PIN);
  if (JOY_SWAP_XY) {
    const int t = rx;
    rx = ry;
    ry = t;
  }
  if (JOY_INVERT_X) {
    rx = joyAdcMax - rx;
  }
  if (JOY_INVERT_Y) {
    ry = joyAdcMax - ry;
  }
  *outX = rx;
  *outY = ry;
}

static int16_t axisToBend(int raw, int center) {
  const int delta = raw - center;
  const int dead = joyAdcMax / 20;
  if (delta > -dead && delta < dead) {
    return 0;
  }
  if (delta > 0) {
    const int span = joyAdcMax - center - dead;
    if (span < 1) {
      return 0;
    }
    return static_cast<int16_t>(constrain(static_cast<long>(delta - dead) * 8191 / span, 0, 8191));
  }
  const int span = center - dead;
  if (span < 1) {
    return 0;
  }
  return static_cast<int16_t>(constrain(static_cast<long>(delta + dead) * 8192 / span, -8192, 0));
}

static uint8_t axisToCc(int raw) {
  return static_cast<uint8_t>(constrain(map(raw, 0, joyAdcMax, 0, 127), 0, 127));
}

static void calibrateJoystick() {
  analogReadResolution(12);
  analogRead(JOY_X_PIN);
  analogRead(JOY_Y_PIN);
  delay(10);
  const int peekX = analogRead(JOY_X_PIN);
  const int peekY = analogRead(JOY_Y_PIN);
  joyAdcMax = (peekX < 1400 && peekY < 1400) ? 1023 : 4095;

  long sumX = 0;
  long sumY = 0;
  for (uint8_t i = 0; i < 32; i++) {
    int mx, my;
    readJoyMapped(&mx, &my);
    sumX += mx;
    sumY += my;
    delay(2);
  }
  joyCenterX = static_cast<int>(sumX / 32);
  joyCenterY = static_cast<int>(sumY / 32);
}

static void applyJoyAxis(uint8_t mode, uint8_t ch, uint8_t cc, int raw, int center, uint8_t *lastCc) {
  if (mode == JOY_PITCH) {
    const int16_t bend = axisToBend(raw, center);
    if (bend != lastBend) {
      lastBend = bend;
      emitPitchBend(bend, ch);
    }
    return;
  }
  if (mode == JOY_CC) {
    const uint8_t v = axisToCc(raw);
    if (v != *lastCc) {
      *lastCc = v;
      emitCc(cc, v, ch);
    }
    return;
  }
  if (mode == JOY_OCTAVE) {
    const int delta = raw - center;
    const int gate = joyAdcMax / 3;
    int8_t zone = 0;
    if (delta > gate) {
      zone = 1;
    } else if (delta < -gate) {
      zone = -1;
    }
    if (zone != 0 && zone != joyOctGate) {
      int next = static_cast<int>(octave) + zone;
      if (next < OCTAVE_MIN) {
        next = OCTAVE_MIN;
      }
      if (next > OCTAVE_MAX) {
        next = OCTAVE_MAX;
      }
      if (next != octave) {
        allNotesOff();
        octave = static_cast<int8_t>(next);
        oledMark();
      }
    }
    joyOctGate = zone;
  }
}

static void scanJoystick() {
  int rawX = 0;
  int rawY = 0;
  readJoyMapped(&rawX, &rawY);
  lastJoyX = rawX;
  lastJoyY = rawY;
  const bool sw = digitalRead(JOY_SW_PIN) == LOW;

  const JoySlot &j = joySlot();
  applyJoyAxis(j.xMode, j.xCh, j.xCc, rawX, joyCenterX, &lastJoyCcX);
  applyJoyAxis(j.yMode, j.yCh, j.yCc, rawY, joyCenterY, &lastJoyCcY);
  if (sw != lastJoySw) {
    lastJoySw = sw;
    if (j.swMode == SW_CC) {
      emitCc(j.swCc, sw ? 127 : 0, j.swCh);
    } else if (j.swMode == SW_NOTE) {
      if (sw) {
        emitNoteOn(j.swCc, midiMap.vel, j.swCh);
      } else {
        emitNoteOff(j.swCc, j.swCh);
      }
    }
    oledMark();
  }
}

static void applyEncStep(uint8_t i, int8_t step) {
  if (step == 0) {
    return;
  }
  encCount[i & 1] += step;
  if (selHeld) {
    // SEL + ENC esquerda = banco P; SEL + ENC direita = banco B.
    changeBank(i & 1, step > 0 ? 1 : -1);
    return;
  }
  EncSlot &e = encSlot(i);
  if (e.mode == ENC_MODE_OFF) {
    return;
  }
  if (e.mode == ENC_MODE_REL) {
    emitCc(e.cc, step > 0 ? 65 : 63, e.ch);
    oledMark();
    return;
  }
  if (e.mode == ENC_MODE_OCT) {
    int next = octave + step;
    if (next < OCTAVE_MIN) {
      next = OCTAVE_MIN;
    }
    if (next > OCTAVE_MAX) {
      next = OCTAVE_MAX;
    }
    if (next != octave) {
      allNotesOff();
      octave = static_cast<int8_t>(next);
      oledMark();
    }
    return;
  }
  const int inc = (step > 0 ? e.step : -static_cast<int>(e.step));
  encVal[perfBank][i] = clampU7(static_cast<int>(encVal[perfBank][i]) + inc);
  if (e.cc == CC_VOLUME) {
    volume = encVal[perfBank][i];
  }
  sendEncCc(i);
  oledMark();
}

static void scanEncoders() {
  int8_t s0 = readEncoderStep(ENC_OCTAVE, encOctave);
  int8_t s1 = readEncoderStep(ENC_VOLUME, encVolume);
  if (ENC_OCTAVE_REV) {
    s0 = static_cast<int8_t>(-s0);
  }
  if (ENC_VOLUME_REV) {
    s1 = static_cast<int8_t>(-s1);
  }
  applyEncStep(0, s0);
  applyEncStep(1, s1);
}

static void readSerialLine(char *buf, size_t cap) {
  size_t n = 0;
  const uint32_t start = millis();
  while (n + 1 < cap) {
    if (!Serial.available()) {
      if (millis() - start > 250) {
        break;
      }
      delay(1);
      continue;
    }
    const char c = static_cast<char>(Serial.read());
    if (c == '\n' || c == '\r') {
      break;
    }
    buf[n++] = c;
  }
  buf[n] = '\0';
}

static void handleSerial() {
  while (Serial.available()) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\r' || c == '\n' || c == ' ') {
      continue;
    }
    if (c == 'M' || c == 'm') {
      char line[320];
      readSerialLine(line, sizeof(line));
      handleMapCommand(line);
      oledMark();
      refreshWsFromState();
      continue;
    }
    if (c == 'u') {
      uiStream = true;
      Serial.println(F("UI on"));
      emitJsonState();
      continue;
    }
    if (c == 'U') {
      uiStream = false;
      Serial.println(F("UI off"));
      continue;
    }
    if (c == 'L') {
      char line[48];
      readSerialLine(line, sizeof(line));
      handleLedCommand(line);
      continue;
    }
    if (c == 'h' || c == 'H' || c == '?') {
      Serial.println(F("Casio SA-1 MIDI  M dump | MS save | MZ reset | MG ch vel"));
      Serial.println(F("MB p|b n  ME ...  MJ ...  MP ...  MK matriz  u/U live  L leds"));
    }
  }
}

void setup() {
  if (!TinyUSBDevice.isInitialized()) {
    TinyUSBDevice.begin(0);
  }

  TinyUSBDevice.setManufacturerDescriptor("DIY");
  TinyUSBDevice.setProductDescriptor("Casio SA-1 MIDI");
  usbMidi.setStringDescriptor("Casio SA-1 MIDI");
  Serial.begin(115200);
  midiMapLoad();

  MIDI.begin();
  MIDI.turnThruOff();
  MIDI.setHandleClock(onMidiClock);
  MIDI.setHandleStart(onMidiStart);
  MIDI.setHandleContinue(onMidiContinue);
  MIDI.setHandleStop(onMidiStop);
  MIDI.setHandleSongPosition(onMidiSongPos);

  if (TinyUSBDevice.mounted()) {
    TinyUSBDevice.detach();
    delay(10);
    TinyUSBDevice.attach();
  }

  pinMode(LED_PIN, OUTPUT);

  Wire.setSDA(I2C_SDA_PIN);
  Wire.setSCL(I2C_SCL_PIN);
  Wire.begin();
  Wire.setClock(400000);
  setupOled();
  setupWs();
  setupAudio();

  if (!setupMcp()) {
    oledMark();
    while (true) {
      digitalWrite(LED_PIN, (millis() / 100) % 2);
      wsIdleTick();
      oledTick(currentOledStatus(false));
    }
  }

  initEncoder(ENC_OCTAVE, encOctave);
  initEncoder(ENC_VOLUME, encVolume);
  pinMode(JOY_SW_PIN, INPUT_PULLUP);
  calibrateJoystick();
  digitalWrite(LED_PIN, HIGH);
  oledMark();
}

void loop() {
#ifdef TINYUSB_NEED_POLLING_TASK
  TinyUSBDevice.task();
#endif

  if (typedDigits == 1 && (millis() - lastDigitMs > 1500)) {
    typedDigits = 0;
    typedProgram = 0;
    oledMark();
  }

  static bool lastUsbMounted = false;
  const bool usb = TinyUSBDevice.mounted();
  if (usb != lastUsbMounted) {
    lastUsbMounted = usb;
    oledMark();
  }

  handleSerial();
  wsRainbowTick();
  wsIdleTick();
  if (uiStream && millis() - lastJsonMs >= 50) {
    lastJsonMs = millis();
    emitJsonState();
  }

  if (!usb && !uiStream) {
    digitalWrite(LED_PIN, (millis() / 200) % 2);
    oledTick(currentOledStatus(true));
    return;
  }

  digitalWrite(LED_PIN, HIGH);
  while (MIDI.read()) {
  }
  tickInternalTempo();
  scanMatrix();
  scanEncoders();
  scanJoystick();
  oledTick(currentOledStatus(true));
}

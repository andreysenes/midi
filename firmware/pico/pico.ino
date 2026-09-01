#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_MCP23X17.h>
#include <Adafruit_TinyUSB.h>
#include <MIDI.h>
#include "config.h"
#include "oled.h"

Adafruit_MCP23X17 mcp;
Adafruit_USBD_MIDI usbMidi;
MIDI_CREATE_INSTANCE(Adafruit_USBD_MIDI, usbMidi, MIDI);
MIDI_CREATE_INSTANCE(HardwareSerial, Serial1, SerialMIDI);

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

struct EncoderState {
  uint8_t prev;
  int8_t accum;
};

static EncoderState encOctave;
static EncoderState encVolume;

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

static void emitNoteOn(uint8_t note, uint8_t velocity) {
  MIDI.sendNoteOn(note, velocity, MIDI_CHANNEL);
  if (MIRROR_SERIAL_MIDI) {
    SerialMIDI.sendNoteOn(note, velocity, MIDI_CHANNEL);
  }
}

static void emitNoteOff(uint8_t note) {
  MIDI.sendNoteOff(note, 0, MIDI_CHANNEL);
  if (MIRROR_SERIAL_MIDI) {
    SerialMIDI.sendNoteOff(note, 0, MIDI_CHANNEL);
  }
}

static void emitCc(uint8_t cc, uint8_t value) {
  MIDI.sendControlChange(cc, value, MIDI_CHANNEL);
  if (MIRROR_SERIAL_MIDI) {
    SerialMIDI.sendControlChange(cc, value, MIDI_CHANNEL);
  }
}

static void emitProgram(uint8_t value) {
  MIDI.sendProgramChange(value, MIDI_CHANNEL);
  if (MIRROR_SERIAL_MIDI) {
    SerialMIDI.sendProgramChange(value, MIDI_CHANNEL);
  }
}

static void emitPitchBend(int16_t value) {
  MIDI.sendPitchBend(value, MIDI_CHANNEL);
  if (MIRROR_SERIAL_MIDI) {
    SerialMIDI.sendPitchBend(value, MIDI_CHANNEL);
  }
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
  return st;
}

static void sendNote(uint8_t baseNote, bool on) {
  const uint8_t note = applyOctave(baseNote);
  lastNoteShown = note;
  haveLastNote = true;
  oledMark();
  if (on) {
    emitNoteOn(note, NOTE_VELOCITY);
  } else {
    emitNoteOff(note);
  }
}

static void allNotesOff() {
  emitCc(123, 0);
  emitCc(120, 0);
}

static void sendVolume() {
  emitCc(CC_VOLUME, volume);
}

static void handleDigit(uint8_t digit) {
  const uint32_t now = millis();
  if (now - lastDigitMs > 1500) {
    typedDigits = 0;
    typedProgram = 0;
  }
  lastDigitMs = now;

  if (typedDigits == 0) {
    typedProgram = digit * 10;
    typedDigits = 1;
    oledMark();
    return;
  }

  program = clampU7(typedProgram + digit);
  typedDigits = 0;
  emitProgram(program);
  oledMark();
}

static void handleButton(uint8_t code, bool pressed) {
  if (!pressed) {
    return;
  }
  const uint32_t now = millis();
  if (now - lastButtonMs < BUTTON_DEBOUNCE_MS) {
    return;
  }
  lastButtonMs = now;

  if (code >= 200 && code <= 209) {
    handleDigit(code - 200);
    return;
  }

  switch (code) {
    case BTN_STOP:
    case BTN_DEMO:
      allNotesOff();
      break;
    default:
      break;
  }
}

static void handleCell(uint8_t row, uint8_t col, bool pressed) {
  if (MCP_KO_REV) {
    row = static_cast<uint8_t>(KO_COUNT - 1 - row);
  }
  if (MCP_KI_REV) {
    col = static_cast<uint8_t>(KI_COUNT - 1 - col);
  }
  const uint8_t code = MATRIX[row][col];
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

static int joyCenterX = 2048;
static int joyCenterY = 2048;
static int joyAdcMax = 4095;
static int16_t lastBend = 1;
static uint8_t lastMod = 255;

static int readJoyAxis(uint8_t pin, bool invert) {
  int raw = analogRead(pin);
  if (invert) {
    raw = joyAdcMax - raw;
  }
  return raw;
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
  joyAdcMax = 4095;
  analogRead(JOY_X_PIN);
  analogRead(JOY_Y_PIN);
  delay(10);

  long sumX = 0;
  long sumY = 0;
  for (uint8_t i = 0; i < 32; i++) {
    sumX += analogRead(JOY_X_PIN);
    sumY += analogRead(JOY_Y_PIN);
    delay(2);
  }
  joyCenterX = static_cast<int>(sumX / 32);
  joyCenterY = static_cast<int>(sumY / 32);
}

static void scanJoystick() {
  const int rawX = readJoyAxis(JOY_X_PIN, JOY_INVERT_X);
  const int rawY = readJoyAxis(JOY_Y_PIN, JOY_INVERT_Y);
  const int16_t bend = axisToBend(rawX, joyCenterX);
  const uint8_t mod = axisToCc(rawY);
  const bool sw = digitalRead(JOY_SW_PIN) == LOW;

  if (bend != lastBend) {
    lastBend = bend;
    emitPitchBend(bend);
  }
  if (mod != lastMod) {
    lastMod = mod;
    emitCc(CC_MODULATION, mod);
  }
  if (sw != lastJoySw) {
    lastJoySw = sw;
    emitCc(CC_SUSTAIN, sw ? 127 : 0);
    oledMark();
  }
}

static void scanEncoders() {
  const int8_t octStep = readEncoderStep(ENC_OCTAVE, encOctave);
  if (octStep != 0) {
    int next = octave + octStep;
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

  const int8_t volStep = readEncoderStep(ENC_VOLUME, encVolume);
  if (volStep != 0) {
    volume = clampU7(static_cast<int>(volume) + (volStep * 2));
    sendVolume();
    oledMark();
  }
}

void setup() {
  if (!TinyUSBDevice.isInitialized()) {
    TinyUSBDevice.begin(0);
  }

  TinyUSBDevice.setManufacturerDescriptor("DIY");
  TinyUSBDevice.setProductDescriptor("Casio SA-1 MIDI");
  usbMidi.setStringDescriptor("Casio SA-1 MIDI");

  MIDI.begin();
  MIDI.turnThruOff();

  if (MIRROR_SERIAL_MIDI) {
    Serial1.setTX(BT_TX_PIN);
    Serial1.setRX(BT_RX_PIN);
    SerialMIDI.begin(MIDI_CHANNEL_OMNI);
    Serial1.begin(BT_BAUD);
    SerialMIDI.turnThruOff();
  }

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

  if (!setupMcp()) {
    oledMark();
    while (true) {
      digitalWrite(LED_PIN, (millis() / 100) % 2);
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

  if (!usb) {
    digitalWrite(LED_PIN, (millis() / 200) % 2);
    oledTick(currentOledStatus(true));
    return;
  }

  digitalWrite(LED_PIN, HIGH);
  MIDI.read();
  if (MIRROR_SERIAL_MIDI) {
    SerialMIDI.read();
  }
  scanMatrix();
  scanEncoders();
  scanJoystick();
  oledTick(currentOledStatus(true));
}

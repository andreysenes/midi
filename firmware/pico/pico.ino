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
#include "chord.h"
#include "mcu.h"

Adafruit_MCP23X17 mcp;
Adafruit_USBD_MIDI usbMidi(2);  // cable 0 = instrument, cable 1 = Mackie Control
MIDI_CREATE_INSTANCE(Adafruit_USBD_MIDI, usbMidi, MIDI);

static bool held[KO_COUNT][KI_COUNT];
static uint8_t releaseStreak[KO_COUNT][KI_COUNT];
static int8_t drivenKo = -1;
static const uint8_t RELEASE_CONFIRM = 3;
static int8_t octave = 0;
static uint8_t volume = 100;
static uint8_t program = 0;
static uint8_t typedProgram = 0;
static uint8_t typedDigits = 0;
static uint32_t lastDigitMs = 0;
static uint32_t lastButtonMs = 0;
static uint8_t lastNoteShown = 0;
static bool haveLastNote = false;
static uint8_t localNoteHeld[128];
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
static const uint16_t BANK_SELECT_MS = 2000;

static uint32_t bankSelectUntilMs = 0;
static bool bankFocusPad = true;
static uint32_t lastClockTickUs = 0;
static uint32_t lastClockUsAny = 0;
static uint16_t measuredBpmx10 = 0;
static uint16_t lockedBpmx10 = 0;
static uint32_t bpmWindowStartUs = 0;
static uint8_t bpmWindowClocks = 0;
static uint32_t playheadClocks = 0;
static uint32_t lastPlayheadOledMs = 0;
static char lastMcuTrack[12] = "--";
static char oledTimeBuf[16] = "--";

static void resetTempoPos() {
  midiClockCount = 0;
  midiBeat = 0;
  midiStep = 0;
}

static bool bankSelectActive() {
  if (selHeld) {
    return true;
  }
  return bankSelectUntilMs != 0 && static_cast<int32_t>(millis() - bankSelectUntilMs) < 0;
}

static void armBankSelect(bool pad) {
  bankFocusPad = pad;
  bankSelectUntilMs = millis() + BANK_SELECT_MS;
}

static void refreshWsFromState() {
  if (bankSelectActive()) {
    wsShowBank(selHeld ? true : bankFocusPad, perfBank, padBank);
  } else if (midiClockRunning || transportPlaying) {
    wsShowTempo(midiStep);
  } else {
    wsShowIdle(perfBank, padBank);
  }
}

static uint16_t snapBpmx10(uint32_t x10) {
  if (x10 < 200) {
    x10 = 200;
  }
  if (x10 > 4000) {
    x10 = 4000;
  }
  const uint16_t nearestInt = static_cast<uint16_t>(((x10 + 5) / 10) * 10);
  const int di = static_cast<int>(x10) - static_cast<int>(nearestInt);
  if (di <= 2 && di >= -2) {
    return nearestInt;
  }
  const uint16_t nearestHalf = static_cast<uint16_t>(((x10 + 2) / 5) * 5);
  return nearestHalf;
}

static uint16_t currentBpmx10() {
  const bool clockLive = lockedBpmx10 > 0 && lastDawClockMs != 0 &&
                         (millis() - lastDawClockMs) < 2000;
  if (clockLive) {
    return lockedBpmx10;
  }
  const uint16_t fromMcu = mcuBpmx10();
  if (fromMcu > 0) {
    return fromMcu;
  }
  if (transportPlaying &&
      (lastDawClockMs == 0 || (millis() - lastDawClockMs) >= DAW_CLOCK_HOLD_MS)) {
    return static_cast<uint16_t>(INTERNAL_BPM * 10);
  }
  if (lockedBpmx10 > 0) {
    return lockedBpmx10;
  }
  return measuredBpmx10;
}

static uint16_t currentBpm() {
  return static_cast<uint16_t>((currentBpmx10() + 5) / 10);
}

static uint32_t currentPlaySeconds() {
  const bool clockLive = lastDawClockMs != 0 && (millis() - lastDawClockMs) < 2000;
  if (!clockLive && mcuHasPlaySeconds()) {
    return mcuPlaySeconds();
  }
  const uint16_t bpm = currentBpm();
  if (bpm == 0) {
    return 0;
  }
  return (playheadClocks * 60UL) / (static_cast<uint32_t>(bpm) * 24UL);
}

static void noteDawBpmSample() {
  const uint32_t now = micros();
  if (bpmWindowClocks == 0) {
    bpmWindowStartUs = now;
    bpmWindowClocks = 1;
    lastClockTickUs = now;
    return;
  }
  bpmWindowClocks++;
  lastClockTickUs = now;
  if (lockedBpmx10 == 0 && bpmWindowClocks == 25) {
    const uint32_t dt1 = now - bpmWindowStartUs;
    if (dt1 >= 100000UL && dt1 <= 4000000UL) {
      lockedBpmx10 = snapBpmx10(static_cast<uint32_t>(600000000ULL / dt1));
      measuredBpmx10 = lockedBpmx10;
      oledMark();
    }
  }
  // 96 MIDI clock intervals = 4 quarter notes. Endpoints avoid USB jitter summing.
  if (bpmWindowClocks < 97) {
    return;
  }
  const uint32_t dt = now - bpmWindowStartUs;
  bpmWindowStartUs = now;
  bpmWindowClocks = 1;
  if (dt < 400000UL || dt > 15000000UL) {
    return;
  }
  const uint32_t x10 = static_cast<uint32_t>((2400000000ULL + (dt / 2)) / dt);
  measuredBpmx10 = snapBpmx10(x10);
  if (lockedBpmx10 == 0) {
    lockedBpmx10 = measuredBpmx10;
    oledMark();
  } else {
    const int d = static_cast<int>(x10) - static_cast<int>(lockedBpmx10);
    if (d >= 6 || d <= -6) {
      lockedBpmx10 = measuredBpmx10;
      oledMark();
    }
  }
}

static void onMidiClock() {
  const uint32_t us = micros();
  if (lastClockUsAny != 0 && (us - lastClockUsAny) < 200) {
    return;
  }
  lastClockUsAny = us;
  const uint32_t now = millis();
  lastDawClockMs = now;
  midiClockRunning = true;
  noteDawBpmSample();
  playheadClocks++;
  midiClockCount++;
  if (midiClockCount == 12) {
    midiStep = static_cast<uint8_t>((midiBeat * 2 + 1) & 7);
    if (!bankSelectActive()) {
      wsShowTempo(midiStep);
    }
  } else if (midiClockCount >= 24) {
    midiClockCount = 0;
    midiBeat = static_cast<uint8_t>((midiBeat + 1) & 3);
    midiStep = static_cast<uint8_t>((midiBeat * 2) & 7);
    oledMark();
    if (!bankSelectActive()) {
      wsShowTempo(midiStep);
    }
  }
  if (now - lastPlayheadOledMs >= 250) {
    lastPlayheadOledMs = now;
    oledMark();
  }
}

static void onMidiStart() {
  midiClockRunning = true;
  transportPlaying = true;
  resetTempoPos();
  midiSongPos = 0;
  playheadClocks = 0;
  lastClockTickUs = 0;
  bpmWindowStartUs = 0;
  bpmWindowClocks = 0;
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
  lastClockTickUs = 0;
  oledMark();
  refreshWsFromState();
}

static void tickInternalTempo() {
  if (bankSelectActive() || !transportPlaying) {
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
  // Internal metronome: 2 clocks-worth per eighth ≈ 12 MIDI clocks per eighth
  playheadClocks += 12;
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
  // 1 sixteenth = 6 MIDI clocks
  playheadClocks = static_cast<uint32_t>(beats) * 6UL;
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
  synthNoteOn(note, velocity);
  if (localNoteHeld[note] < 255) {
    localNoteHeld[note]++;
  }
  chordNote(note, true);
  oledRollNote(note, true);
  oledMark();
}

static void emitNoteOff(uint8_t note, uint8_t ch) {
  MIDI.sendNoteOff(note, 0, ch);
  synthNoteOff(note);
  if (localNoteHeld[note]) {
    localNoteHeld[note]--;
  }
  chordNote(note, false);
  oledRollNote(note, false);
  oledMark();
}

static void onDawNote(uint8_t note, bool on, uint8_t vel) {
  if (note > 127) {
    return;
  }
  if (localNoteHeld[note]) {
    return;
  }
  if (on) {
    synthNoteOn(note, vel);
  } else {
    synthNoteOff(note);
  }
  oledRollNote(note, on);
}

static void emitCc(uint8_t cc, uint8_t value, uint8_t ch) {
  MIDI.sendControlChange(cc, value, ch);
}

static void emitProgram(uint8_t value, uint8_t ch) {
  MIDI.sendProgramChange(value, ch);
  synthSelectPatch(value % SYNTH_PATCHES);
  oledMark();
}

static void emitPitchBend(int16_t value, uint8_t ch) {
  MIDI.sendPitchBend(value, ch);
  synthSetBend(value);
}

static OledStatus currentOledStatus(bool mcpOk) {
  OledStatus st;
  st.octave = octave;
  st.volume = volume;
  st.program = program;
  st.typedDigits = typedDigits;
  st.typedProgram = typedProgram;
  st.sustain = lastJoySw;
  st.usbMounted = TinyUSBDevice.mounted();
  st.mcpOk = mcpOk;
  st.perfBank = perfBank;
  st.padBank = padBank;
  st.selHeld = selHeld;
  st.bankFocusPad = bankFocusPad;
  st.bankSelectActive = bankSelectActive();
  st.clockRunning = midiClockRunning;
  st.transportPlaying = transportPlaying || mcuIsHostPlaying();
  st.recording = mcuIsHostRecording();
  st.looping = mcuIsHostLooping();
  st.beat = midiBeat;
  // Mackie 7-seg is ~100 ms late and coarse — snapping to it while the
  // clock runs pulls the cursor backwards. Jog/stop uses Mackie; play uses MIDI clock.
  if (mcuHasBarPhase() && !st.transportPlaying && !midiClockRunning) {
    const uint32_t win = OLED_ROLL_WIN_CLK;
    playheadClocks = (playheadClocks / win) * win + mcuBarPhaseClocks();
  }
  st.playheadClocks = playheadClocks;
  st.nowMs = millis();
  oledRollSetClock(playheadClocks);
  if (mcuHasTime()) {
    st.timeText = mcuTimeText();
  } else {
    const uint32_t sec = currentPlaySeconds();
    const uint32_t hh = sec / 3600UL;
    const uint32_t mm = (sec / 60UL) % 60UL;
    const uint32_t ss = sec % 60UL;
    if (hh > 0) {
      snprintf(oledTimeBuf, sizeof(oledTimeBuf), "%lu:%02lu",
               static_cast<unsigned long>(hh), static_cast<unsigned long>(mm));
    } else {
      snprintf(oledTimeBuf, sizeof(oledTimeBuf), "%02lu:%02lu",
               static_cast<unsigned long>(mm), static_cast<unsigned long>(ss));
    }
    st.timeText = oledTimeBuf;
  }
  st.trackName = mcuTrack();
  st.chordName = chordText();
  st.audioMode = static_cast<uint8_t>(audioGetPortMode());
  if (st.audioMode == AUDIO_PORT_CLOCK) {
    st.patchName = "CLK";
  } else if (st.audioMode == AUDIO_PORT_IN) {
    st.patchName = "IN";
  } else {
    st.patchName = synthPatch.name;
  }
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
  Serial.print(F(",\"pch\":"));
  Serial.print(pianoCh());
  Serial.print(F(",\"sel\":"));
  Serial.print(selHeld ? 1 : 0);
  Serial.print(F(",\"note\":"));
  Serial.print(haveLastNote ? lastNoteShown : -1);
  Serial.print(F(",\"pads\":"));
  Serial.print(pads);
  Serial.print(F(",\"ws\":"));
  Serial.print(wsOk ? 1 : 0);
  Serial.print(F(",\"mcp\":1,\"oled\":"));
  Serial.print(oledOk ? 1 : 0);
  Serial.print(F(",\"wr\":"));
  Serial.print(wsRainbowOn ? 1 : 0);
  Serial.print(F(",\"beat\":"));
  Serial.print(midiBeat);
  Serial.print(F(",\"step\":"));
  Serial.print(midiStep);
  Serial.print(F(",\"clk\":"));
  Serial.print(midiClockRunning ? 1 : 0);
  Serial.print(F(",\"play\":"));
  Serial.print(transportPlaying ? 1 : 0);
  Serial.print(F(",\"bpm\":"));
  Serial.print(currentBpm());
  Serial.print(F(",\"play_s\":"));
  Serial.print(currentPlaySeconds());
  Serial.print(F(",\"bf\":"));
  Serial.print(bankFocusPad ? 1 : 0);
  Serial.print(F(",\"bs\":"));
  Serial.print(bankSelectActive() ? 1 : 0);
  Serial.print(F(",\"chord\":\""));
  Serial.print(chordText());
  Serial.print(F("\",\"track\":\""));
  Serial.print(mcuTrack());
  Serial.print(F("\",\"aum\":"));
  Serial.print(static_cast<uint8_t>(audioGetPortMode()));
  Serial.print(F(",\"sp\":"));
  Serial.print(synthPatchIx);
  Serial.print(F(",\"sa\":"));
  Serial.print(synthPatch.algo);
  Serial.print(F(",\"sf\":"));
  Serial.print(synthPatch.feedback);
  Serial.print(F(",\"slr\":"));
  Serial.print(synthPatch.lfoRate);
  Serial.print(F(",\"slp\":"));
  Serial.print(synthPatch.lfoPitch);
  Serial.print(F(",\"sla\":"));
  Serial.print(synthPatch.lfoAmp);
  Serial.print(F(",\"sn\":\""));
  Serial.print(synthPatch.name);
  Serial.print(F("\",\"so\":["));
  for (uint8_t i = 0; i < SYNTH_OPS; i++) {
    if (i) {
      Serial.print(',');
    }
    const FmOp &o = synthPatch.op[i];
    Serial.print('[');
    Serial.print(o.ratio);
    Serial.print(',');
    Serial.print(o.level);
    Serial.print(',');
    Serial.print(o.atk);
    Serial.print(',');
    Serial.print(o.dec);
    Serial.print(',');
    Serial.print(o.sus);
    Serial.print(',');
    Serial.print(o.rel);
    Serial.print(',');
    Serial.print(o.detune);
    Serial.print(',');
    Serial.print(o.velSens);
    Serial.print(']');
  }
  Serial.print(F("],\"h\":["));
  for (uint8_t r = 0; r < 7; r++) {
    if (r) {
      Serial.print(',');
    }
    Serial.print(h[r]);
  }
  Serial.println(F("]}"));
}

static uint8_t lastOnKo = 255;

static void sendNote(uint8_t baseNote, bool on, uint8_t ko) {
  const uint8_t note = applyOctave(baseNote);
  const uint8_t ch = pianoCh();
  lastNoteShown = note;
  haveLastNote = true;
  Serial.print(on ? F("KEY on ") : F("KEY off "));
  Serial.print(note);
  Serial.print(F(" ch"));
  Serial.println(ch);
  oledMark();
  if (on) {
    lastOnKo = ko;
    emitNoteOn(note, midiMap.vel, ch);
  } else {
    emitNoteOff(note, ch);
  }
}

static void allNotesOff() {
  for (uint8_t ch = 1; ch <= BANK_COUNT; ch++) {
    emitCc(123, 0, ch);
    emitCc(120, 0, ch);
  }
  emitCc(midiMap.tempoCcUp, 0, midiMap.tempoCh);
  emitCc(midiMap.tempoCcDn, 0, midiMap.tempoCh);
  synthPanic();
  chordClear();
  memset(localNoteHeld, 0, sizeof(localNoteHeld));
  oledRollAllOff();
  oledMark();
}

static void nudgeOctave(int dir) {
  int next = static_cast<int>(octave) + dir;
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

static bool isVolumeEnc(const EncSlot &e) {
  return e.cc == CC_VOLUME;
}

static void sendEncCc(uint8_t i) {
  const EncSlot &e = encSlot(i);
  if (e.mode != ENC_MODE_CC) {
    return;
  }
  emitCc(e.cc, encVal[perfBank][i], e.ch);
}

static void nudgeLocalVolume(int8_t step, uint8_t amount) {
  const int inc = (step > 0 ? static_cast<int>(amount) : -static_cast<int>(amount));
  volume = clampU7(static_cast<int>(volume) + inc);
  synthSetMaster(volume);
}

static void onMidiControlChange(byte channel, byte number, byte value) {
  (void)channel;
  const uint8_t cc = static_cast<uint8_t>(number);
  const uint8_t val = clampU7(static_cast<int>(value));
  if (cc == 123 || cc == 120) {
    oledRollAllOff();
    synthPanic();
  }
  if (cc == CC_SUSTAIN) {
    synthSetSustain(val >= 64);
  }
  if (cc == CC_MODULATION) {
    synthSetMod(val);
  }
  if (cc == CC_VOLUME) {
    volume = val;
    synthSetMaster(volume);
    oledMark();
  }
  for (uint8_t i = 0; i < 2; i++) {
    EncSlot &e = encSlot(i);
    if (e.cc != cc) {
      continue;
    }
    // Eco do próprio tick relativo (63/65) não é o fader da DAW.
    if (e.mode == ENC_MODE_REL && (val == 63 || val == 65)) {
      continue;
    }
    encVal[perfBank][i] = val;
    if (isVolumeEnc(e)) {
      volume = val;
      synthSetMaster(volume);
    }
    oledMark();
  }
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
  if (p.mode == PAD_MCU) {
    mcuSendNote(p.note, pressed ? 127 : 0);
    if (pressed && p.note >= MCU_NOTE_SEL1 && p.note < MCU_NOTE_SEL1 + 8) {
      oledMark();
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
  if (!pad && static_cast<uint8_t>(next) != perfBank) {
    allNotesOff();
  }
  *b = static_cast<uint8_t>(next);
  if (!pad) {
    syncPianoChFromBank();
    sendEncCc(0);
    sendEncCc(1);
  }
  armBankSelect(pad);
  oledMark();
  refreshWsFromState();
  Serial.print(F("BANK pb="));
  Serial.print(perfBank);
  Serial.print(F(" bb="));
  Serial.print(padBank);
  Serial.print(F(" pch="));
  Serial.println(pianoCh());
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
    const uint8_t mode = which ? midiMap.tempoModeDn : midiMap.tempoModeUp;
    if (mode == TEMPO_MODE_OCT) {
      if (pressed) {
        nudgeOctave(which ? -1 : 1);
      }
      return;
    }
    if (mode == TEMPO_MODE_MCU) {
      const uint8_t note = which ? midiMap.tempoCcDn : midiMap.tempoCcUp;
      mcuSendNote(note, pressed ? 127 : 0);
      return;
    }
    emitTempoCc(which, pressed);
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
    if (midiMap.stopNote != MCU_FN_TOGGLE) {
      mcuPulseNote(midiMap.stopNote);
      Serial.println(F("TRANSPORT mcu"));
      oledMark();
      refreshWsFromState();
      return;
    }
    if (transportPlaying || midiClockRunning || mcuIsHostPlaying()) {
      mcuSendTransport(false);
      MIDI.sendStop();
      transportPlaying = false;
      midiClockRunning = false;
      resetTempoPos();
      allNotesOff();
      Serial.println(F("TRANSPORT stop"));
    } else {
      mcuSendTransport(true);
      MIDI.sendStart();
      transportPlaying = true;
      midiClockRunning = true;
      resetTempoPos();
      playheadClocks = 0;
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
    sendNote(code, pressed, row);
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
    mcp.pinMode(8 + i, INPUT);
  }
  mcp.pinMode(15, INPUT_PULLUP);
  return true;
}

// Matriz sem díodos: 2 teclas em linhas e colunas distintas fecham os 4 cantos.
// Manter o par real; fantasmas a ligar/desligar soam a sujeira no acorde.
static void suppressRectGhosts(uint8_t *raw) {
  for (uint8_t r1 = 0; r1 < KO_COUNT; r1++) {
    for (uint8_t r2 = static_cast<uint8_t>(r1 + 1); r2 < KO_COUNT; r2++) {
      const uint8_t both = raw[r1] & raw[r2];
      if (both == 0) {
        continue;
      }
      for (uint8_t c1 = 0; c1 < KI_COUNT; c1++) {
        if ((both & (1 << c1)) == 0) {
          continue;
        }
        for (uint8_t c2 = static_cast<uint8_t>(c1 + 1); c2 < KI_COUNT; c2++) {
          if ((both & (1 << c2)) == 0) {
            continue;
          }
          const uint8_t rr[4] = {r1, r1, r2, r2};
          const uint8_t cc[4] = {c1, c2, c1, c2};
          bool on[4];
          bool was[4];
          uint8_t heldCount = 0;
          for (uint8_t i = 0; i < 4; i++) {
            on[i] = (raw[rr[i]] & (1 << cc[i])) != 0;
            was[i] = held[rr[i]][cc[i]];
            if (was[i]) {
              heldCount++;
            }
          }
          if (!on[0] || !on[1] || !on[2] || !on[3]) {
            continue;
          }
          bool keep[4] = {false, false, false, false};
          if (heldCount >= 2) {
            for (uint8_t i = 0; i < 4; i++) {
              keep[i] = was[i];
            }
            const bool sameRow = (was[0] && was[1]) || (was[2] && was[3]);
            const bool sameCol = (was[0] && was[2]) || (was[1] && was[3]);
            if ((sameRow || sameCol) && heldCount == 2) {
              int extra = -1;
              if (lastOnKo != 255) {
                for (uint8_t i = 0; i < 4; i++) {
                  if (!was[i] && rr[i] == lastOnKo) {
                    extra = static_cast<int>(i);
                  }
                }
              }
              if (extra < 0) {
                for (uint8_t i = 0; i < 4; i++) {
                  if (!was[i]) {
                    extra = static_cast<int>(i);
                    break;
                  }
                }
              }
              if (extra >= 0) {
                keep[extra] = true;
              }
            }
          } else if (heldCount == 1) {
            int h = 0;
            for (uint8_t i = 0; i < 4; i++) {
              if (was[i]) {
                h = static_cast<int>(i);
                break;
              }
            }
            keep[h] = true;
            keep[h ^ 3] = true;
          } else {
            keep[0] = true;
            keep[3] = true;
          }
          for (uint8_t i = 0; i < 4; i++) {
            if (!keep[i]) {
              raw[rr[i]] = static_cast<uint8_t>(raw[rr[i]] & ~(1 << cc[i]));
            }
          }
        }
      }
    }
  }
}

static void driveKo(uint8_t row) {
  if (drivenKo >= 0 && drivenKo != static_cast<int8_t>(row)) {
    mcp.pinMode(8 + drivenKo, INPUT);
  }
  mcp.pinMode(8 + row, OUTPUT);
  mcp.digitalWrite(8 + row, LOW);
  drivenKo = static_cast<int8_t>(row);
}

static void releaseKo() {
  if (drivenKo >= 0) {
    mcp.pinMode(8 + drivenKo, INPUT);
    drivenKo = -1;
  }
}

static void scanMatrix() {
  uint8_t raw[KO_COUNT];
  for (uint8_t row = 0; row < KO_COUNT; row++) {
    driveKo(row);
    delayMicroseconds(SCAN_SETTLE_US);
    raw[row] = static_cast<uint8_t>(~mcp.readGPIO(MCP_KI_PORT));
  }
  releaseKo();
  suppressRectGhosts(raw);

  for (uint8_t row = 0; row < KO_COUNT; row++) {
    for (uint8_t col = 0; col < KI_COUNT; col++) {
      const bool rawDown = (raw[row] & (1 << col)) != 0;
      bool pressed = held[row][col];
      if (rawDown) {
        releaseStreak[row][col] = 0;
        pressed = true;
      } else if (held[row][col]) {
        if (releaseStreak[row][col] < 255) {
          releaseStreak[row][col]++;
        }
        if (releaseStreak[row][col] >= RELEASE_CONFIRM) {
          pressed = false;
        }
      }
      if (pressed == held[row][col]) {
        continue;
      }
      held[row][col] = pressed;
      handleCell(row, col, pressed);
    }
  }
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

static int16_t lastBend = 0;
static uint8_t lastJoyCcX = 255;
static uint8_t lastJoyCcY = 255;
static int8_t joyOctGate = 0;
static int joyFiltX = 2048;
static int joyFiltY = 2048;
static bool joyRestX = true;
static bool joyRestY = true;
static bool joyFiltInit = false;

static int joyDeadzoneAdc() {
  int dz = joyAdcMax / 10;
  int cfg = JOY_DEADZONE;
  if (joyAdcMax <= 1023) {
    cfg = JOY_DEADZONE / 4;
  }
  if (cfg > dz) {
    dz = cfg;
  }
  return dz;
}

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
  if (!joyFiltInit) {
    joyFiltX = rx;
    joyFiltY = ry;
    joyFiltInit = true;
  } else {
    joyFiltX = (joyFiltX * 3 + rx) / 4;
    joyFiltY = (joyFiltY * 3 + ry) / 4;
  }
  *outX = joyFiltX;
  *outY = joyFiltY;
}

static bool joyAxisAtRest(int raw, int center, bool *atRest) {
  const int delta = raw - center;
  const int dead = joyDeadzoneAdc();
  const int leave = dead + dead / 3;
  const int enter = (dead * 3) / 4;
  if (*atRest) {
    if (delta <= -leave || delta >= leave) {
      *atRest = false;
    }
  } else if (delta > -enter && delta < enter) {
    *atRest = true;
  }
  return *atRest;
}

static int16_t axisToBend(int raw, int center) {
  const int delta = raw - center;
  const int dead = joyDeadzoneAdc();
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

  joyFiltInit = false;
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
  joyFiltX = joyCenterX;
  joyFiltY = joyCenterY;
  joyRestX = true;
  joyRestY = true;
  lastBend = 0;
  lastJoyCcX = 255;
  lastJoyCcY = 255;
}

static void applyJoyAxis(uint8_t mode, uint8_t ch, uint8_t cc, int raw, int center,
                         uint8_t *lastCc, bool *atRest) {
  const bool rest = joyAxisAtRest(raw, center, atRest);
  if (mode == JOY_PITCH) {
    const int16_t bend = rest ? 0 : axisToBend(raw, center);
    const int diff = abs(static_cast<int>(bend) - static_cast<int>(lastBend));
    if (bend != lastBend && (bend == 0 || lastBend == 0 || diff >= 32)) {
      lastBend = bend;
      emitPitchBend(bend, ch);
    }
    return;
  }
  if (mode == JOY_CC) {
    const uint8_t v = rest ? axisToCc(center) : axisToCc(raw);
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
      nudgeOctave(zone);
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
  static int lastDispX = 0x7fff;
  static int lastDispY = 0x7fff;
  if (rawX != lastDispX || rawY != lastDispY) {
    const int ddx = abs(rawX - lastDispX);
    const int ddy = abs(rawY - lastDispY);
    if (lastDispX == 0x7fff || ddx > 24 || ddy > 24) {
      lastDispX = rawX;
      lastDispY = rawY;
      oledMark();
    }
  }
  const bool sw = digitalRead(JOY_SW_PIN) == LOW;

  const JoySlot &j = joySlot();
  applyJoyAxis(j.xMode, j.xCh, j.xCc, rawX, joyCenterX, &lastJoyCcX, &joyRestX);
  applyJoyAxis(j.yMode, j.yCh, j.yCc, rawY, joyCenterY, &lastJoyCcY, &joyRestY);
  if (sw != lastJoySw) {
    lastJoySw = sw;
    if (j.swMode == SW_CC) {
      emitCc(j.swCc, sw ? 127 : 0, j.swCh);
      if (j.swCc == CC_SUSTAIN) {
        synthSetSustain(sw);
      }
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

static uint32_t lastEncStepMs[2];

static uint8_t encDawAmount(uint8_t i, uint8_t stepField) {
  uint8_t base = stepField == 0 ? 1 : stepField;
  uint16_t amt = static_cast<uint16_t>(base) * ENC_DAW_GAIN;
  const uint32_t now = millis();
  const uint32_t dt = now - lastEncStepMs[i & 1];
  lastEncStepMs[i & 1] = now;
  if (dt < 25) {
    amt = static_cast<uint16_t>(amt * 3);
  } else if (dt < 60) {
    amt = static_cast<uint16_t>(amt * 2);
  }
  if (amt < 1) {
    amt = 1;
  }
  if (amt > 15) {
    amt = 15;
  }
  return static_cast<uint8_t>(amt);
}

static void applyEncStep(uint8_t i, int8_t step) {
  if (step == 0) {
    return;
  }
  encCount[i & 1] += step;
  if (selHeld && lastJoySw) {
    wsNudgeMaster(step);
    return;
  }
  if (selHeld) {
    // SEL + ENC esquerda = banco P; SEL + ENC direita = banco B.
    changeBank(i & 1, step > 0 ? 1 : -1);
    return;
  }
  EncSlot &e = encSlot(i);
  if (e.mode == ENC_MODE_OFF) {
    return;
  }
  if (e.mode == ENC_MODE_MCU) {
    const bool discrete = (e.cc == MCU_ENC_CHANNEL || e.cc == MCU_ENC_BANK);
    const uint8_t amt = discrete ? 1 : encDawAmount(i, e.step);
    mcuApplyEnc(e.cc, step, amt, &encVal[perfBank][i]);
    oledMark();
    return;
  }
  if (e.mode == ENC_MODE_REL) {
    const uint8_t amt = encDawAmount(i, e.step);
    emitCc(e.cc, step > 0 ? static_cast<uint8_t>(64 + amt) : static_cast<uint8_t>(64 - amt), e.ch);
    if (isVolumeEnc(e)) {
      nudgeLocalVolume(step, amt);
    }
    if (e.cc == CC_TRACK) {
      mcuNudgeTrack(step > 0 ? 1 : -1);
      oledMark();
    }
    oledMark();
    return;
  }
  if (e.mode == ENC_MODE_OCT) {
    nudgeOctave(step);
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

static void drainUsbMidi() {
  uint8_t packet[4];
  while (usbMidi.readPacket(packet)) {
    const uint8_t cable = static_cast<uint8_t>(packet[0] >> 4);
    const uint8_t cin = static_cast<uint8_t>(packet[0] & 0x0f);
    if (cin == 0xF) {
      if (packet[1] == 0xF8) {
        onMidiClock();
      } else if (packet[1] == 0xFA) {
        onMidiStart();
      } else if (packet[1] == 0xFB) {
        onMidiContinue();
      } else if (packet[1] == 0xFC) {
        onMidiStop();
      }
    }
    if (cable == 1) {
      mcuHandlePacket(packet);
      continue;
    }
    switch (cin) {
      case 0x8:
      case 0x9: {
        const uint8_t note = packet[2] & 0x7f;
        const uint8_t vel = (cin == 0x8) ? 0 : packet[3];
        onDawNote(note, vel > 0, vel > 0 ? vel : 0);
        break;
      }
      case 0x3: {
        const unsigned beats =
            static_cast<unsigned>(packet[2]) |
            (static_cast<unsigned>(packet[3]) << 7);
        onMidiSongPos(beats);
        break;
      }
      case 0xB: {
        const byte ch = static_cast<byte>((packet[1] & 0x0f) + 1);
        onMidiControlChange(ch, packet[2], packet[3]);
        break;
      }
      default:
        break;
    }
  }
  if (strcmp(lastMcuTrack, mcuTrack()) != 0) {
    strncpy(lastMcuTrack, mcuTrack(), sizeof(lastMcuTrack) - 1);
    lastMcuTrack[sizeof(lastMcuTrack) - 1] = '\0';
    oledMark();
  }
  if (mcuTakeTransportDirty()) {
    if (mcuIsHostPlaying()) {
      transportPlaying = true;
      midiClockRunning = true;
    } else if (!mcuIsHostRecording()) {
      transportPlaying = false;
    }
    oledMark();
    refreshWsFromState();
  }
  if (mcuTakeDisplayDirty()) {
    oledMark();
  }
}

static void handleSerial() {
  while (Serial.available()) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\r' || c == '\n' || c == ' ') {
      continue;
    }
    if (c == 'M' || c == 'm') {
      char line[320];
      const uint8_t prevPb = perfBank;
      const uint8_t prevBb = padBank;
      readSerialLine(line, sizeof(line));
      handleMapCommand(line);
      if (perfBank != prevPb) {
        allNotesOff();
        syncPianoChFromBank();
        armBankSelect(false);
      } else if (padBank != prevBb) {
        armBankSelect(true);
      }
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
    if (c == 'S' || c == 's') {
      char line[96];
      readSerialLine(line, sizeof(line));
      handleSynthCommand(line);
      oledMark();
      continue;
    }
    if (c == 'h' || c == 'H' || c == '?') {
      Serial.println(F("Casio SA-1 MIDI  M dump | MS save | MZ reset | MG ch vel"));
      Serial.println(F("MB p|b n  ME ...  MJ ...  MP ...  MK matriz  u/U live  L leds"));
      Serial.println(F("S synth  S m 0|1|2  S p 0-7  S a algo  S f fb  S l r p a  S o ..."));
    }
  }
}

void setup() {
  if (!TinyUSBDevice.isInitialized()) {
    TinyUSBDevice.begin(0);
  }

  TinyUSBDevice.setManufacturerDescriptor("DIY");
  TinyUSBDevice.setProductDescriptor("Casio SA-1 MIDI");
  TinyUSBDevice.setSerialDescriptor("SA1-MCU-2");
  usbMidi.setStringDescriptor("Casio SA-1 MIDI");
  usbMidi.setCableName(1, "Casio SA-1");
  usbMidi.setCableName(2, "Mackie Control");
  Serial.begin(115200);
  midiMapLoad();
  chordClear();
  mcuBegin(&usbMidi);

  MIDI.begin();
  MIDI.turnThruOff();
  // Input demuxed in drainUsbMidi() so MCU cable 1 is not dropped.

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
  synthSetMaster(volume);
  digitalWrite(LED_PIN, HIGH);
  refreshWsFromState();
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

  static bool wasBankSelect = false;
  const bool nowBankSelect = bankSelectActive();
  if (wasBankSelect && !nowBankSelect) {
    refreshWsFromState();
    oledMark();
  }
  wasBankSelect = nowBankSelect;

  handleSerial();
  drainUsbMidi();
  audioSetClockBpm(currentBpm());
  wsRainbowTick();
  wsIdleTick();
  if (uiStream && millis() - lastJsonMs >= 50) {
    lastJsonMs = millis();
    emitJsonState();
  }

  digitalWrite(LED_PIN, usb ? HIGH : ((millis() / 200) % 2));
  tickInternalTempo();
  scanMatrix();
  scanJoystick();
  scanEncoders();
  oledTick(currentOledStatus(true));
}

void setup1() {
  while (!i2sOk) {
    delay(1);
  }
}

void loop1() {
  audioTick();
  audioTick();
}

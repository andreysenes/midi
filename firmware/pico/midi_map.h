#pragma once

#include <Arduino.h>
#include <string.h>
#include <EEPROM.h>
#include "config.h"

static const uint8_t BANK_COUNT = 8;
static const uint8_t PAD_COUNT = 10;
static const uint16_t MAP_MAGIC = 0xC510;
// v6: joy eixos. v7: orientação antiga + Y pitch (não oitava).
// v8: EC11-2 volume em CC relativo (65/63) — não sobrescreve o fader da track.
static const uint8_t MAP_VER = 8;

enum EncMode : uint8_t {
  ENC_MODE_CC = 0,
  ENC_MODE_OCT = 1,
  ENC_MODE_OFF = 2,
  ENC_MODE_REL = 3,
};
enum JoyAxisMode : uint8_t { JOY_PITCH = 0, JOY_CC = 1, JOY_OFF = 2, JOY_OCTAVE = 3 };
enum JoySwMode : uint8_t { SW_CC = 0, SW_NOTE = 1, SW_OFF = 2 };
enum PadMode : uint8_t {
  PAD_DUAL = 0,
  PAD_CC = 1,
  PAD_NOTE = 2,
  PAD_PROG = 3,
  PAD_OFF = 4,
};

struct EncSlot {
  uint8_t mode;
  uint8_t ch;
  uint8_t cc;
  uint8_t step;
};

struct JoySlot {
  uint8_t xMode;
  uint8_t xCh;
  uint8_t xCc;
  uint8_t yMode;
  uint8_t yCh;
  uint8_t yCc;
  uint8_t swMode;
  uint8_t swCh;
  uint8_t swCc;
};

struct PadSlot {
  uint8_t mode;
  uint8_t ch;
  uint8_t cc;
  uint8_t note;
};

struct MidiMap {
  uint16_t magic;
  uint8_t ver;
  uint8_t ch;
  uint8_t vel;
  uint8_t encSwap;
  EncSlot enc[BANK_COUNT][2];
  JoySlot joy[BANK_COUNT];
  PadSlot pad[BANK_COUNT][PAD_COUNT];
  uint8_t keys[7][8];
  uint8_t tempoCh;
  uint8_t tempoCcUp;
  uint8_t tempoCcDn;
};

static MidiMap midiMap;
static uint8_t perfBank = 0;
static uint8_t padBank = 0;
static uint8_t encVal[BANK_COUNT][2];
static bool selHeld = false;
static uint8_t noteMatrix[7][8];

static void noteMatrixReset() {
  memcpy(noteMatrix, MATRIX, sizeof(noteMatrix));
}

static void noteMatrixToMap() {
  memcpy(midiMap.keys, noteMatrix, sizeof(noteMatrix));
}

static void noteMatrixFromMap() {
  memcpy(noteMatrix, midiMap.keys, sizeof(noteMatrix));
}

static void printMatLine() {
  Serial.print(F("MAT"));
  for (uint8_t r = 0; r < 7; r++) {
    for (uint8_t c = 0; c < 8; c++) {
      Serial.print(' ');
      Serial.print(noteMatrix[r][c]);
    }
  }
  Serial.println();
}

static uint8_t clampCh(int v) {
  if (v < 1) {
    return 1;
  }
  if (v > 16) {
    return 16;
  }
  return static_cast<uint8_t>(v);
}

static uint8_t clamp127(int v) {
  if (v < 0) {
    return 0;
  }
  if (v > 127) {
    return 127;
  }
  return static_cast<uint8_t>(v);
}

static void midiMapDefaults() {
  memset(&midiMap, 0, sizeof(midiMap));
  midiMap.magic = MAP_MAGIC;
  midiMap.ver = MAP_VER;
  midiMap.ch = MIDI_CHANNEL;  // piano = canal 1
  midiMap.vel = NOTE_VELOCITY;
  midiMap.encSwap = 1;
  for (uint8_t b = 0; b < BANK_COUNT; b++) {
    if (b == 0) {
      midiMap.enc[b][0] = EncSlot{ENC_MODE_REL, 4, CC_TRACK, 1};
      midiMap.enc[b][1] = EncSlot{ENC_MODE_REL, 4, CC_VOLUME, 1};
      midiMap.joy[b] = JoySlot{
          JOY_PITCH, 3, 0,
          JOY_CC, 3, CC_MODULATION,
          SW_CC, 3, CC_SUSTAIN};
    } else {
      midiMap.enc[b][0] = EncSlot{ENC_MODE_CC, 4, static_cast<uint8_t>(12 + b), 2};
      midiMap.enc[b][1] = EncSlot{ENC_MODE_CC, 4, static_cast<uint8_t>(20 + b), 2};
      midiMap.joy[b] = JoySlot{
          JOY_CC, 3, static_cast<uint8_t>(16 + b),
          JOY_CC, 3, static_cast<uint8_t>(24 + b),
          SW_CC, 3, CC_SUSTAIN};
    }
    encVal[b][0] = 64;
    encVal[b][1] = (b == 0) ? 100 : 64;
    for (uint8_t d = 0; d < PAD_COUNT; d++) {
      midiMap.pad[b][d] = PadSlot{
          PAD_DUAL, 2,
          static_cast<uint8_t>(30 + b * 10 + d),
          static_cast<uint8_t>(36 + d)};
    }
  }
  midiMap.tempoCh = 2;
  midiMap.tempoCcUp = 40;
  midiMap.tempoCcDn = 41;
  noteMatrixReset();
  noteMatrixToMap();
}

static void midiMapLoad() {
  noteMatrixReset();
  EEPROM.begin(1024);
  EEPROM.get(0, midiMap);
  if (midiMap.magic != MAP_MAGIC || midiMap.ver < 3 || midiMap.ver > MAP_VER) {
    midiMapDefaults();
    return;
  }
  if (midiMap.ver < 4) {
    midiMap.tempoCh = 2;
    midiMap.tempoCcUp = 40;
    midiMap.tempoCcDn = 41;
  }
  if (midiMap.ver < 5) {
    if (midiMap.enc[0][0].mode == ENC_MODE_OCT) {
      midiMap.enc[0][0] = EncSlot{ENC_MODE_REL, 4, CC_TRACK, 1};
    }
  }
  if (midiMap.ver < 6) {
    JoySlot &j0 = midiMap.joy[0];
    if (j0.xMode == JOY_PITCH && j0.yMode == JOY_CC) {
      j0.xMode = JOY_CC;
      j0.xCh = 3;
      j0.xCc = CC_MODULATION;
      j0.yMode = JOY_OCTAVE;
      j0.yCh = 3;
      j0.yCc = 0;
    }
  }
  if (midiMap.ver < 7) {
    JoySlot &j0 = midiMap.joy[0];
    if (j0.yMode == JOY_OCTAVE || (j0.xMode == JOY_CC && j0.yMode == JOY_PITCH)) {
      j0.xMode = JOY_PITCH;
      j0.xCh = 3;
      j0.xCc = 0;
      j0.yMode = JOY_CC;
      j0.yCh = 3;
      j0.yCc = CC_MODULATION;
    }
  }
  if (midiMap.ver < 8) {
    EncSlot &vol = midiMap.enc[0][1];
    if (vol.cc == CC_VOLUME && vol.mode == ENC_MODE_CC) {
      vol.mode = ENC_MODE_REL;
      vol.step = 1;
    }
  }
  midiMap.ver = MAP_VER;
  noteMatrixFromMap();
  midiMap.ch = clampCh(midiMap.ch);
  midiMap.tempoCh = clampCh(midiMap.tempoCh);
  midiMap.tempoCcUp &= 127;
  midiMap.tempoCcDn &= 127;
  if (midiMap.vel == 0) {
    midiMap.vel = NOTE_VELOCITY;
  }
}

static void midiMapSave() {
  midiMap.ver = MAP_VER;
  noteMatrixToMap();
  EEPROM.put(0, midiMap);
  EEPROM.commit();
}

static EncSlot &encSlot(uint8_t i) {
  return midiMap.enc[perfBank][i & 1];
}

static JoySlot &joySlot() {
  return midiMap.joy[perfBank];
}

static PadSlot &padSlot(uint8_t d) {
  return midiMap.pad[padBank][d < PAD_COUNT ? d : 0];
}

static void printEncLine(uint8_t b, uint8_t i) {
  const EncSlot &e = midiMap.enc[b][i];
  Serial.print(F("ENC b="));
  Serial.print(b);
  Serial.print(F(" i="));
  Serial.print(i);
  Serial.print(F(" mode="));
  Serial.print(e.mode);
  Serial.print(F(" ch="));
  Serial.print(e.ch);
  Serial.print(F(" cc="));
  Serial.print(e.cc);
  Serial.print(F(" step="));
  Serial.print(e.step);
  Serial.print(F(" val="));
  Serial.println(encVal[b][i]);
}

static void printJoyLine(uint8_t b) {
  const JoySlot &j = midiMap.joy[b];
  Serial.print(F("JOY b="));
  Serial.print(b);
  Serial.print(F(" xm="));
  Serial.print(j.xMode);
  Serial.print(F(" xch="));
  Serial.print(j.xCh);
  Serial.print(F(" xcc="));
  Serial.print(j.xCc);
  Serial.print(F(" ym="));
  Serial.print(j.yMode);
  Serial.print(F(" ych="));
  Serial.print(j.yCh);
  Serial.print(F(" ycc="));
  Serial.print(j.yCc);
  Serial.print(F(" sm="));
  Serial.print(j.swMode);
  Serial.print(F(" sch="));
  Serial.print(j.swCh);
  Serial.print(F(" scc="));
  Serial.println(j.swCc);
}

static void printTempoLine() {
  Serial.print(F("TMP ch="));
  Serial.print(midiMap.tempoCh);
  Serial.print(F(" up="));
  Serial.print(midiMap.tempoCcUp);
  Serial.print(F(" dn="));
  Serial.println(midiMap.tempoCcDn);
}

static void printPadLine(uint8_t b, uint8_t d) {
  const PadSlot &p = midiMap.pad[b][d];
  Serial.print(F("PAD b="));
  Serial.print(b);
  Serial.print(F(" d="));
  Serial.print(d);
  Serial.print(F(" mode="));
  Serial.print(p.mode);
  Serial.print(F(" ch="));
  Serial.print(p.ch);
  Serial.print(F(" cc="));
  Serial.print(p.cc);
  Serial.print(F(" note="));
  Serial.println(p.note);
}

static void midiMapDump() {
  Serial.print(F("MAP ch="));
  Serial.print(midiMap.ch);
  Serial.print(F(" vel="));
  Serial.print(midiMap.vel);
  Serial.print(F(" pb="));
  Serial.print(perfBank);
  Serial.print(F(" bb="));
  Serial.print(padBank);
  Serial.print(F(" swap="));
  Serial.println(midiMap.encSwap);
  printTempoLine();
  printMatLine();
  for (uint8_t b = 0; b < BANK_COUNT; b++) {
    printEncLine(b, 0);
    printEncLine(b, 1);
    printJoyLine(b);
    for (uint8_t d = 0; d < PAD_COUNT; d++) {
      printPadLine(b, d);
    }
  }
  Serial.println(F("MAP end"));
}

static uint8_t nextToken(char **p) {
  while (**p == ' ') {
    (*p)++;
  }
  const int v = atoi(*p);
  while (**p && **p != ' ') {
    (*p)++;
  }
  return static_cast<uint8_t>(v);
}

static bool parseNoteMatrix(char *line) {
  uint8_t tmp[7][8];
  for (uint8_t r = 0; r < 7; r++) {
    for (uint8_t c = 0; c < 8; c++) {
      while (*line == ' ') {
        line++;
      }
      if (*line == '\0') {
        return false;
      }
      tmp[r][c] = nextToken(&line);
    }
  }
  memcpy(noteMatrix, tmp, sizeof(noteMatrix));
  noteMatrixToMap();
  return true;
}

static void handleMapCommand(char *line) {
  while (*line == ' ') {
    line++;
  }
  while (*line == 'M' || *line == 'm') {
    line++;
    while (*line == ' ') {
      line++;
    }
  }
  if (*line == '\0') {
    midiMapDump();
    return;
  }
  const char cmd = *line++;
  while (*line == ' ') {
    line++;
  }
  if (cmd == 'S' || cmd == 's') {
    midiMapSave();
    Serial.println(F("MAP saved"));
    return;
  }
  if (cmd == 'Z' || cmd == 'z') {
    midiMapDefaults();
    Serial.println(F("MAP reset"));
    midiMapDump();
    return;
  }
  if (cmd == 'G' || cmd == 'g') {
    midiMap.ch = clampCh(nextToken(&line));
    midiMap.vel = clamp127(nextToken(&line));
    Serial.print(F("MAP ch="));
    Serial.print(midiMap.ch);
    Serial.print(F(" vel="));
    Serial.println(midiMap.vel);
    return;
  }
  if (cmd == 'B' || cmd == 'b') {
    const char which = *line++;
    while (*line == ' ') {
      line++;
    }
    uint8_t n = static_cast<uint8_t>(atoi(line) % BANK_COUNT);
    if (which == 'p' || which == 'P') {
      perfBank = n;
    } else {
      padBank = n;
    }
    Serial.print(F("BANK pb="));
    Serial.print(perfBank);
    Serial.print(F(" bb="));
    Serial.println(padBank);
    return;
  }
  if (cmd == 'E' || cmd == 'e') {
    const uint8_t b = nextToken(&line) % BANK_COUNT;
    const uint8_t i = nextToken(&line) & 1;
    midiMap.enc[b][i].mode = nextToken(&line);
    midiMap.enc[b][i].ch = clampCh(nextToken(&line));
    midiMap.enc[b][i].cc = nextToken(&line) & 127;
    midiMap.enc[b][i].step = nextToken(&line);
    if (midiMap.enc[b][i].step == 0) {
      midiMap.enc[b][i].step = 1;
    }
    printEncLine(b, i);
    return;
  }
  if (cmd == 'J' || cmd == 'j') {
    const uint8_t b = nextToken(&line) % BANK_COUNT;
    JoySlot &j = midiMap.joy[b];
    j.xMode = nextToken(&line);
    j.xCh = clampCh(nextToken(&line));
    j.xCc = nextToken(&line) & 127;
    j.yMode = nextToken(&line);
    j.yCh = clampCh(nextToken(&line));
    j.yCc = nextToken(&line) & 127;
    j.swMode = nextToken(&line);
    j.swCh = clampCh(nextToken(&line));
    j.swCc = nextToken(&line) & 127;
    printJoyLine(b);
    return;
  }
  if (cmd == 'K' || cmd == 'k') {
    if (*line == '\0') {
      noteMatrixReset();
      noteMatrixToMap();
      Serial.println(F("MAT reset"));
      printMatLine();
      return;
    }
    if (!parseNoteMatrix(line)) {
      Serial.println(F("MAT err"));
      return;
    }
    midiMapSave();
    Serial.println(F("MAT ok"));
    printMatLine();
    return;
  }
  if (cmd == 'T' || cmd == 't') {
    midiMap.tempoCh = clampCh(nextToken(&line));
    midiMap.tempoCcUp = nextToken(&line) & 127;
    midiMap.tempoCcDn = nextToken(&line) & 127;
    printTempoLine();
    return;
  }
  if (cmd == 'P' || cmd == 'p') {
    const uint8_t b = nextToken(&line) % BANK_COUNT;
    const uint8_t d = nextToken(&line) % PAD_COUNT;
    uint8_t mode = nextToken(&line);
    if (mode > PAD_OFF) {
      mode = PAD_DUAL;
    }
    midiMap.pad[b][d].mode = mode;
    midiMap.pad[b][d].ch = clampCh(nextToken(&line));
    midiMap.pad[b][d].cc = nextToken(&line) & 127;
    midiMap.pad[b][d].note = nextToken(&line) & 127;
    printPadLine(b, d);
    return;
  }
  Serial.println(F("M  uso: M | S | Z | G ch vel | B p|b n | E ... | J ... | P ... | T ch up dn | K 56 celulas"));
}

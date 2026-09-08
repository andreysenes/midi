#pragma once

#include <string.h>
#include <stdlib.h>
#include <Arduino.h>
#include <Adafruit_TinyUSB.h>

// Mackie Control Universal on USB-MIDI cable 1.
// LCD SysEx: F0 00 00 66 14 12 <offset> <ascii...> F7  — 112 chars (2×56)
// Assignment: F0 00 00 66 14 10 <10 ascii> F7

static const uint8_t MCU_NOTE_REC1 = 0x00;
static const uint8_t MCU_NOTE_SOLO1 = 0x08;
static const uint8_t MCU_NOTE_MUTE1 = 0x10;
static const uint8_t MCU_NOTE_SEL1 = 0x18;
static const uint8_t MCU_NOTE_VPOTSW1 = 0x20;
static const uint8_t MCU_NOTE_BANK_L = 0x2E;
static const uint8_t MCU_NOTE_BANK_R = 0x2F;
static const uint8_t MCU_NOTE_CH_L = 0x30;
static const uint8_t MCU_NOTE_CH_R = 0x31;
static const uint8_t MCU_NOTE_READ = 0x4A;
static const uint8_t MCU_NOTE_WRITE = 0x4B;
static const uint8_t MCU_NOTE_TRIM = 0x4C;
static const uint8_t MCU_NOTE_TOUCH = 0x4D;
static const uint8_t MCU_NOTE_LATCH = 0x4E;
static const uint8_t MCU_NOTE_GROUP = 0x4F;
static const uint8_t MCU_NOTE_MARKER = 0x54;
static const uint8_t MCU_NOTE_NUDGE = 0x55;
static const uint8_t MCU_NOTE_CYCLE = 0x56;
static const uint8_t MCU_NOTE_DROP = 0x57;
static const uint8_t MCU_NOTE_REPLACE = 0x58;
static const uint8_t MCU_NOTE_CLICK = 0x59;
static const uint8_t MCU_NOTE_SOLO = 0x5A;
static const uint8_t MCU_NOTE_REW = 0x5B;
static const uint8_t MCU_NOTE_FF = 0x5C;
static const uint8_t MCU_NOTE_STOP = 0x5D;
static const uint8_t MCU_NOTE_PLAY = 0x5E;
static const uint8_t MCU_NOTE_RECORD = 0x5F;

// EncSlot.cc when ENC_MODE_MCU
enum McuEncFn : uint8_t {
  MCU_ENC_VPOT1 = 0,
  MCU_ENC_JOG = 8,
  MCU_ENC_FADER1 = 9,
  MCU_ENC_MASTER = 17,
  MCU_ENC_CHANNEL = 18,
  MCU_ENC_BANK = 19,
};

static Adafruit_USBD_MIDI *mcuUsb = nullptr;
static char mcuLcd[112];
static char mcuStrip[8][8];
static uint8_t mcuSelected = 0;
static char mcuTrackName[12] = "--";
static bool mcuHaveLcd = false;

static char mcuAssign[11] = "";
static bool mcuHaveAssign = false;
static uint16_t mcuBpmx10FromAssign = 0;
static uint32_t mcuPlaySecondsFromAssign = 0;
static bool mcuHavePlaySeconds = false;

static uint8_t mcuTimeCc[10];
static bool mcuHaveTime = false;
static char mcuTimeStr[16] = "--";

static bool mcuHostPlaying = false;
static bool mcuHostRecording = false;
static bool mcuHostCycle = false;
static uint8_t mcuBarPhase = 0;
static bool mcuHaveBarPhase = false;
static bool mcuTransportDirty = false;
static bool mcuDisplayDirty = false;

static uint8_t mcuSysexBuf[128];
static uint8_t mcuSysexLen = 0;
static bool mcuInSysex = false;
static uint8_t mcuFaderVal[9];

static void mcuUpdateTrackName() {
  const char *src = mcuStrip[mcuSelected < 8 ? mcuSelected : 0];
  char tmp[8];
  strncpy(tmp, src, 7);
  tmp[7] = '\0';
  int end = 6;
  while (end >= 0 && (tmp[end] == ' ' || tmp[end] == '\0')) {
    tmp[end] = '\0';
    end--;
  }
  if (end < 0 || tmp[0] == '\0') {
    strncpy(mcuTrackName, "--", sizeof(mcuTrackName));
  } else {
    strncpy(mcuTrackName, tmp, sizeof(mcuTrackName) - 1);
    mcuTrackName[sizeof(mcuTrackName) - 1] = '\0';
  }
  mcuDisplayDirty = true;
}

static char mcuDecode7seg(uint8_t v) {
  const uint8_t core = static_cast<uint8_t>(v & 0x3f);
  if (core == 0 || core == 0x20) {
    return ' ';
  }
  if (core >= '0' && core <= '9') {
    return static_cast<char>(core);
  }
  if (core >= 1 && core <= 26) {
    return static_cast<char>('A' + core - 1);
  }
  if ((core & 0x10) && (core & 0x0f) <= 9) {
    return static_cast<char>('0' + (core & 0x0f));
  }
  return ' ';
}

static void mcuParseBarPhase();

static void mcuRebuildTimeStr() {
  char tmp[16];
  uint8_t n = 0;
  for (uint8_t i = 0; i < 10 && n < 14; i++) {
    const char c = mcuDecode7seg(mcuTimeCc[i]);
    if (n == 0 && c == ' ') {
      continue;
    }
    tmp[n++] = c;
    if ((mcuTimeCc[i] & 0x40) && n < 14) {
      tmp[n++] = '.';
    }
  }
  while (n > 0 && (tmp[n - 1] == ' ' || tmp[n - 1] == '.')) {
    n--;
  }
  tmp[n] = '\0';
  if (n == 0) {
    strncpy(mcuTimeStr, "--", sizeof(mcuTimeStr));
    return;
  }
  strncpy(mcuTimeStr, tmp, sizeof(mcuTimeStr) - 1);
  mcuTimeStr[sizeof(mcuTimeStr) - 1] = '\0';
  mcuParseBarPhase();
}

// Ableton beats 7-seg: bars.beats[.subs][.ticks] (1-based beat/sub).
// 4/4 bar at 24 PPQN → phase 0..95 for the OLED playhead.
static void mcuParseBarPhase() {
  int parts[6];
  uint8_t n = 0;
  int acc = -1;
  for (const char *p = mcuTimeStr; *p && n < 6; p++) {
    if (*p >= '0' && *p <= '9') {
      if (acc < 0) {
        acc = 0;
      }
      acc = acc * 10 + (*p - '0');
    } else if (*p == '.' || *p == ':') {
      if (acc >= 0) {
        parts[n++] = acc;
        acc = -1;
      }
    }
  }
  if (acc >= 0 && n < 6) {
    parts[n++] = acc;
  }
  if (n < 2) {
    return;
  }
  const int beat = parts[1];
  const int sub = (n >= 3) ? parts[2] : 1;
  const int ticks = (n >= 4) ? parts[3] : 0;
  if (beat < 1 || beat > 32) {
    return;
  }
  if (n >= 3 && sub > 16) {
    return;
  }
  uint32_t clocks = static_cast<uint32_t>(beat - 1) * 24UL;
  if (sub >= 1) {
    int s = sub;
    if (s > 16) {
      s = 16;
    }
    clocks += static_cast<uint32_t>(s - 1) * 6UL;
  }
  if (ticks > 0 && ticks <= 119) {
    clocks += (static_cast<uint32_t>(ticks) * 6UL) / 120UL;
  }
  mcuBarPhase = static_cast<uint8_t>(clocks % 96UL);
  mcuHaveBarPhase = true;
}

static void mcuHandleTimeCc(uint8_t cc, uint8_t val) {
  if (cc < 0x40 || cc > 0x49) {
    return;
  }
  const uint8_t idx = static_cast<uint8_t>(0x49 - cc);
  val &= 0x7f;
  if (mcuHaveTime && mcuTimeCc[idx] == val) {
    return;
  }
  mcuTimeCc[idx] = val;
  mcuHaveTime = true;
  mcuRebuildTimeStr();
  mcuDisplayDirty = true;
}

static void mcuParseAssignTempo() {
  mcuBpmx10FromAssign = 0;
  mcuHavePlaySeconds = false;
  mcuPlaySecondsFromAssign = 0;
  if (!mcuHaveAssign || mcuAssign[0] == '\0') {
    return;
  }

  char buf[11];
  strncpy(buf, mcuAssign, 10);
  buf[10] = '\0';
  for (uint8_t i = 0; i < 10; i++) {
    if (buf[i] >= 'A' && buf[i] <= 'Z') {
      buf[i] = static_cast<char>(buf[i] - 'A' + 'a');
    }
  }

  int dots = 0;
  int colons = 0;
  for (uint8_t i = 0; buf[i]; i++) {
    if (buf[i] == '.') {
      dots++;
    }
    if (buf[i] == ':') {
      colons++;
    }
  }

  char *colon = strchr(buf, ':');
  if (colon && colon > buf) {
    if (colons >= 2) {
      const int hh = atoi(buf);
      char *c2 = strchr(colon + 1, ':');
      const int mm = atoi(colon + 1);
      const int ss = c2 ? atoi(c2 + 1) : 0;
      if (hh >= 0 && hh < 100 && mm >= 0 && mm < 60 && ss >= 0 && ss < 60) {
        mcuPlaySecondsFromAssign =
            static_cast<uint32_t>(hh) * 3600UL +
            static_cast<uint32_t>(mm) * 60UL +
            static_cast<uint32_t>(ss);
        mcuHavePlaySeconds = true;
      }
    } else {
      const int mm = atoi(buf);
      const int ss = atoi(colon + 1);
      if (mm >= 0 && ss >= 0 && ss < 60 && mm < 600) {
        mcuPlaySecondsFromAssign =
            static_cast<uint32_t>(mm) * 60UL + static_cast<uint32_t>(ss);
        mcuHavePlaySeconds = true;
      }
    }
  }

  char *bpmTag = strstr(buf, "bpm");
  const bool tagged = bpmTag != nullptr;
  if (bpmTag) {
    *bpmTag = '\0';
  }
  if (dots >= 2) {
    return;
  }
  bool looksTempo = tagged;
  if (!looksTempo && dots == 1) {
    const char *dotp = strchr(buf, '.');
    if (dotp && dotp[1] >= '0' && dotp[1] <= '9') {
      looksTempo = true;
    }
  }
  if (!looksTempo) {
    return;
  }
  const char *p = buf;
  while (*p == ' ') {
    p++;
  }
  if (*p >= '1' && *p <= '9') {
    const int v = atoi(p);
    if (v >= 20 && v <= 400) {
      uint16_t x10 = static_cast<uint16_t>(v * 10);
      const char *dotp = strchr(p, '.');
      if (dotp && dotp[1] >= '0' && dotp[1] <= '9') {
        x10 = static_cast<uint16_t>(x10 + (dotp[1] - '0'));
      }
      mcuBpmx10FromAssign = x10;
    }
  }
}

static void mcuApplyAssign(const uint8_t *data, uint8_t len) {
  memset(mcuAssign, ' ', 10);
  mcuAssign[10] = '\0';
  const uint8_t n = len > 10 ? 10 : len;
  for (uint8_t i = 0; i < n; i++) {
    char c = static_cast<char>(data[i] & 0x7f);
    if (c < 32 || c > 126) {
      c = ' ';
    }
    mcuAssign[i] = c;
  }
  int end = 9;
  while (end >= 0 && mcuAssign[end] == ' ') {
    mcuAssign[end] = '\0';
    end--;
  }
  mcuHaveAssign = true;
  mcuParseAssignTempo();
  mcuDisplayDirty = true;
}

static void mcuRefreshStripsFromLcd() {
  for (uint8_t i = 0; i < 8; i++) {
    memcpy(mcuStrip[i], &mcuLcd[static_cast<uint16_t>(i) * 7], 7);
    mcuStrip[i][7] = '\0';
  }
}

static void mcuApplyLcd(uint8_t offset, const uint8_t *data, uint8_t len) {
  for (uint8_t i = 0; i < len; i++) {
    const uint16_t pos = static_cast<uint16_t>(offset) + i;
    if (pos >= 112) {
      break;
    }
    char c = static_cast<char>(data[i] & 0x7f);
    if (c < 32 || c > 126) {
      c = ' ';
    }
    mcuLcd[pos] = c;
  }
  mcuRefreshStripsFromLcd();
  mcuHaveLcd = true;
  mcuUpdateTrackName();
}

static void mcuSendNote(uint8_t note, uint8_t vel) {
  if (!mcuUsb) {
    return;
  }
  uint8_t pkt[4];
  pkt[0] = 0x19;
  pkt[1] = 0x90;
  pkt[2] = note & 0x7f;
  pkt[3] = vel & 0x7f;
  mcuUsb->writePacket(pkt);
}

static void mcuPulseNote(uint8_t note) {
  mcuSendNote(note, 127);
  delay(5);
  mcuSendNote(note, 0);
}

static void mcuSendTransport(bool play) {
  if (play) {
    mcuPulseNote(MCU_NOTE_PLAY);
  } else {
    mcuPulseNote(MCU_NOTE_STOP);
  }
}

static void mcuSendCc(uint8_t cc, uint8_t val) {
  if (!mcuUsb) {
    return;
  }
  uint8_t pkt[4];
  pkt[0] = 0x1B;
  pkt[1] = 0xB0;
  pkt[2] = cc & 0x7f;
  pkt[3] = val & 0x7f;
  mcuUsb->writePacket(pkt);
}

static void mcuSendPitchBend(uint8_t ch, uint16_t value14) {
  if (!mcuUsb || ch < 1 || ch > 16) {
    return;
  }
  if (value14 > 16383) {
    value14 = 16383;
  }
  uint8_t pkt[4];
  pkt[0] = 0x1E;
  pkt[1] = static_cast<uint8_t>(0xE0 | (ch - 1));
  pkt[2] = static_cast<uint8_t>(value14 & 0x7f);
  pkt[3] = static_cast<uint8_t>((value14 >> 7) & 0x7f);
  mcuUsb->writePacket(pkt);
}

static void mcuSendFader(uint8_t ch, uint8_t val7) {
  if (ch < 1) {
    ch = 1;
  }
  if (ch > 9) {
    ch = 9;
  }
  mcuFaderVal[ch - 1] = val7;
  const uint16_t v14 = static_cast<uint16_t>((static_cast<uint32_t>(val7) * 16383UL) / 127UL);
  mcuSendPitchBend(ch, v14);
}

static void mcuSendVpot(uint8_t index, bool clockwise, uint8_t amount) {
  if (index > 7) {
    return;
  }
  if (amount == 0) {
    amount = 1;
  }
  if (amount > 15) {
    amount = 15;
  }
  const uint8_t val = clockwise ? amount : static_cast<uint8_t>(0x40 | amount);
  mcuSendCc(static_cast<uint8_t>(0x10 + index), val);
}

static void mcuSendJog(bool clockwise, uint8_t amount) {
  if (amount == 0) {
    amount = 1;
  }
  if (amount > 15) {
    amount = 15;
  }
  const uint8_t val = clockwise ? amount : static_cast<uint8_t>(0x40 | amount);
  mcuSendCc(0x3C, val);
}

static void mcuNudgeTrack(int8_t dir) {
  if (dir > 0) {
    mcuPulseNote(MCU_NOTE_CH_R);
    if (mcuSelected < 7) {
      mcuSelected++;
    }
  } else if (dir < 0) {
    mcuPulseNote(MCU_NOTE_CH_L);
    if (mcuSelected > 0) {
      mcuSelected--;
    }
  }
  mcuUpdateTrackName();
}

static void mcuNudgeBank(int8_t dir) {
  if (dir > 0) {
    mcuPulseNote(MCU_NOTE_BANK_R);
  } else if (dir < 0) {
    mcuPulseNote(MCU_NOTE_BANK_L);
  }
}

static uint8_t mcuClampU7(int v) {
  if (v < 0) {
    return 0;
  }
  if (v > 127) {
    return 127;
  }
  return static_cast<uint8_t>(v);
}

static void mcuApplyEnc(uint8_t fn, int8_t dir, uint8_t step, uint8_t *val) {
  const uint8_t amt = step == 0 ? 1 : step;
  const int signedAmt = dir > 0 ? static_cast<int>(amt) : -static_cast<int>(amt);
  if (fn <= 7) {
    mcuSendVpot(fn, dir > 0, amt);
    *val = mcuClampU7(static_cast<int>(*val) + signedAmt);
    return;
  }
  if (fn == MCU_ENC_JOG) {
    mcuSendJog(dir > 0, amt);
    *val = mcuClampU7(static_cast<int>(*val) + signedAmt);
    return;
  }
  if (fn >= MCU_ENC_FADER1 && fn <= MCU_ENC_MASTER) {
    const uint8_t ch = (fn == MCU_ENC_MASTER) ? 9 : static_cast<uint8_t>(fn - MCU_ENC_FADER1 + 1);
    *val = mcuClampU7(static_cast<int>(*val) + signedAmt);
    mcuSendFader(ch, *val);
    return;
  }
  if (fn == MCU_ENC_CHANNEL) {
    mcuNudgeTrack(dir > 0 ? 1 : -1);
    return;
  }
  if (fn == MCU_ENC_BANK) {
    mcuNudgeBank(dir > 0 ? 1 : -1);
  }
}

static void mcuHandleSysexComplete() {
  if (mcuSysexLen < 5) {
    return;
  }
  if (mcuSysexBuf[0] != 0x00 || mcuSysexBuf[1] != 0x00 || mcuSysexBuf[2] != 0x66) {
    return;
  }
  const uint8_t device = mcuSysexBuf[3];
  if (device != 0x14 && device != 0x15) {
    return;
  }
  const uint8_t cmd = mcuSysexBuf[4];
  if (cmd == 0x01 && mcuUsb) {
    uint8_t pkt[4];
    pkt[0] = 0x14;
    pkt[1] = 0xF0;
    pkt[2] = 0x00;
    pkt[3] = 0x00;
    mcuUsb->writePacket(pkt);
    pkt[0] = 0x14;
    pkt[1] = 0x66;
    pkt[2] = 0x14;
    pkt[3] = 0x02;
    mcuUsb->writePacket(pkt);
    pkt[0] = 0x16;
    pkt[1] = 0x14;
    pkt[2] = 0xF7;
    pkt[3] = 0x00;
    mcuUsb->writePacket(pkt);
    return;
  }
  if (cmd == 0x10 && mcuSysexLen >= 5) {
    mcuApplyAssign(&mcuSysexBuf[5], static_cast<uint8_t>(mcuSysexLen - 5));
    return;
  }
  if (cmd == 0x12 && mcuSysexLen >= 6) {
    mcuApplyLcd(mcuSysexBuf[5], &mcuSysexBuf[6], static_cast<uint8_t>(mcuSysexLen - 6));
  }
}

static void mcuFeedByte(uint8_t b) {
  if (b == 0xF0) {
    mcuInSysex = true;
    mcuSysexLen = 0;
    return;
  }
  if (!mcuInSysex) {
    return;
  }
  if (b == 0xF7) {
    mcuInSysex = false;
    mcuHandleSysexComplete();
    return;
  }
  if (mcuSysexLen < sizeof(mcuSysexBuf)) {
    mcuSysexBuf[mcuSysexLen++] = b;
  }
}

static void mcuHandlePacket(const uint8_t packet[4]) {
  const uint8_t cin = packet[0] & 0x0f;
  switch (cin) {
    case 0x4:
      mcuFeedByte(packet[1]);
      mcuFeedByte(packet[2]);
      mcuFeedByte(packet[3]);
      break;
    case 0x5:
      mcuFeedByte(packet[1]);
      break;
    case 0x6:
      mcuFeedByte(packet[1]);
      mcuFeedByte(packet[2]);
      break;
    case 0x7:
      mcuFeedByte(packet[1]);
      mcuFeedByte(packet[2]);
      mcuFeedByte(packet[3]);
      break;
    case 0xB:
      mcuHandleTimeCc(packet[2], packet[3]);
      break;
    case 0x8:
    case 0x9: {
      const uint8_t note = packet[2];
      const uint8_t vel = (cin == 0x8) ? 0 : packet[3];
      if (note >= MCU_NOTE_SEL1 && note < MCU_NOTE_SEL1 + 8 && vel) {
        mcuSelected = static_cast<uint8_t>(note - MCU_NOTE_SEL1);
        mcuUpdateTrackName();
      }
      if (note == MCU_NOTE_PLAY) {
        mcuHostPlaying = (vel > 0);
        if (mcuHostPlaying) {
          mcuHostRecording = false;
        }
        mcuTransportDirty = true;
      } else if (note == MCU_NOTE_STOP && vel > 0) {
        mcuHostPlaying = false;
        mcuHostRecording = false;
        mcuTransportDirty = true;
      } else if (note == MCU_NOTE_RECORD) {
        mcuHostRecording = (vel > 0);
        mcuTransportDirty = true;
      } else if (note == MCU_NOTE_CYCLE) {
        mcuHostCycle = (vel > 0);
        mcuTransportDirty = true;
      }
      break;
    }
    default:
      break;
  }
}

static void mcuBegin(Adafruit_USBD_MIDI *usb) {
  mcuUsb = usb;
  memset(mcuLcd, ' ', sizeof(mcuLcd));
  for (uint8_t i = 0; i < 8; i++) {
    memset(mcuStrip[i], ' ', 7);
    mcuStrip[i][7] = '\0';
  }
  for (uint8_t i = 0; i < 9; i++) {
    mcuFaderVal[i] = 0;
  }
  mcuSelected = 0;
  mcuHaveLcd = false;
  mcuHaveAssign = false;
  mcuAssign[0] = '\0';
  mcuBpmx10FromAssign = 0;
  mcuHavePlaySeconds = false;
  mcuHaveTime = false;
  memset(mcuTimeCc, 0, sizeof(mcuTimeCc));
  strncpy(mcuTimeStr, "--", sizeof(mcuTimeStr));
  mcuHostPlaying = false;
  mcuHostRecording = false;
  mcuHostCycle = false;
  mcuHaveBarPhase = false;
  mcuBarPhase = 0;
  mcuTransportDirty = false;
  mcuDisplayDirty = false;
  strncpy(mcuTrackName, "--", sizeof(mcuTrackName));
}

static const char *mcuTrack() {
  return mcuTrackName;
}

static bool mcuHasTrack() {
  return mcuHaveLcd && strcmp(mcuTrackName, "--") != 0;
}

static uint16_t mcuBpmx10() {
  return mcuBpmx10FromAssign;
}

static uint16_t mcuBpm() {
  return static_cast<uint16_t>((mcuBpmx10FromAssign + 5) / 10);
}

static bool mcuHasTime() {
  return mcuHaveTime && mcuTimeStr[0] && strcmp(mcuTimeStr, "--") != 0;
}

static const char *mcuTimeText() {
  return mcuTimeStr;
}

static bool mcuHasPlaySeconds() {
  return mcuHavePlaySeconds;
}

static uint32_t mcuPlaySeconds() {
  return mcuPlaySecondsFromAssign;
}

static bool mcuIsHostPlaying() {
  return mcuHostPlaying;
}

static bool mcuIsHostRecording() {
  return mcuHostRecording;
}

static bool mcuIsHostLooping() {
  return mcuHostCycle;
}

static bool mcuHasBarPhase() {
  return mcuHaveBarPhase;
}

static uint8_t mcuBarPhaseClocks() {
  return mcuBarPhase;
}

static bool mcuTakeTransportDirty() {
  const bool d = mcuTransportDirty;
  mcuTransportDirty = false;
  return d;
}

static bool mcuTakeDisplayDirty() {
  const bool d = mcuDisplayDirty;
  mcuDisplayDirty = false;
  return d;
}

static const char *mcuAssignText() {
  return mcuHaveAssign ? mcuAssign : "";
}

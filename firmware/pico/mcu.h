#pragma once

#include <string.h>
#include <stdlib.h>
#include <Arduino.h>
#include <Adafruit_TinyUSB.h>

// Mackie Control Universal on USB-MIDI cable 1.
// LCD SysEx: F0 00 00 66 14 12 <offset> <ascii...> F7  — 8 strips × 7 chars
// Assignment/timecode: F0 00 00 66 14 10 <10 ascii> F7
// Transport notes: STOP 0x5D, PLAY 0x5E (Note On vel 127 / 0)

static const uint8_t MCU_NOTE_STOP = 0x5D;
static const uint8_t MCU_NOTE_PLAY = 0x5E;

static Adafruit_USBD_MIDI *mcuUsb = nullptr;
static char mcuStrip[8][8];  // 7 chars + NUL
static uint8_t mcuSelected = 0;
static char mcuTrackName[12] = "--";
static bool mcuHaveLcd = false;

// Assignment / time display (10 chars) — often bars.beats or tempo.
static char mcuAssign[11] = "";
static bool mcuHaveAssign = false;
static uint16_t mcuBpmFromAssign = 0;
static uint32_t mcuPlaySecondsFromAssign = 0;
static bool mcuHavePlaySeconds = false;

// Host lights PLAY/STOP LEDs with Note On feedback.
static bool mcuHostPlaying = false;
static bool mcuTransportDirty = false;
static bool mcuDisplayDirty = false;

static uint8_t mcuSysexBuf[128];
static uint8_t mcuSysexLen = 0;
static bool mcuInSysex = false;

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

static void mcuParseAssignTempo() {
  mcuBpmFromAssign = 0;
  mcuHavePlaySeconds = false;
  mcuPlaySecondsFromAssign = 0;
  if (!mcuHaveAssign || mcuAssign[0] == '\0') {
    return;
  }

  // Prefer a clear BPM token: "120.00", " 128 ", "120bpm"
  char buf[11];
  strncpy(buf, mcuAssign, 10);
  buf[10] = '\0';
  for (uint8_t i = 0; i < 10; i++) {
    if (buf[i] >= 'A' && buf[i] <= 'Z') {
      buf[i] = static_cast<char>(buf[i] - 'A' + 'a');
    }
  }

  // Strip trailing units
  char *bpmTag = strstr(buf, "bpm");
  if (bpmTag) {
    *bpmTag = '\0';
  }

  // If string looks like bars.beats (e.g. 001.01.00 / 12.3.1) skip BPM guess
  // unless there's a standalone 2–3 digit number with optional .xx tempo.
  int dots = 0;
  for (uint8_t i = 0; buf[i]; i++) {
    if (buf[i] == '.') {
      dots++;
    }
  }

  // Try mm:ss or m:ss playhead
  {
    char *colon = strchr(buf, ':');
    if (colon && colon > buf) {
      const int mm = atoi(buf);
      const int ss = atoi(colon + 1);
      if (mm >= 0 && ss >= 0 && ss < 60 && mm < 600) {
        mcuPlaySecondsFromAssign = static_cast<uint32_t>(mm) * 60UL + static_cast<uint32_t>(ss);
        mcuHavePlaySeconds = true;
      }
    }
  }

  // Tempo: number in 20–400 range (integer part before first non-digit aside from leading spaces)
  if (dots <= 1) {
    const char *p = buf;
    while (*p == ' ') {
      p++;
    }
    if (*p >= '1' && *p <= '9') {
      const int v = atoi(p);
      if (v >= 20 && v <= 400) {
        mcuBpmFromAssign = static_cast<uint16_t>(v);
      }
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
  // Trim
  int end = 9;
  while (end >= 0 && mcuAssign[end] == ' ') {
    mcuAssign[end] = '\0';
    end--;
  }
  mcuHaveAssign = true;
  mcuParseAssignTempo();
  mcuDisplayDirty = true;
}

static void mcuApplyLcd(uint8_t offset, const uint8_t *data, uint8_t len) {
  for (uint8_t i = 0; i < len; i++) {
    const uint16_t pos = static_cast<uint16_t>(offset) + i;
    if (pos >= 56) {
      break;
    }
    const uint8_t strip = static_cast<uint8_t>(pos / 7);
    const uint8_t col = static_cast<uint8_t>(pos % 7);
    char c = static_cast<char>(data[i] & 0x7f);
    if (c < 32 || c > 126) {
      c = ' ';
    }
    mcuStrip[strip][col] = c;
  }
  mcuHaveLcd = true;
  mcuUpdateTrackName();
}

static void mcuSendNote(uint8_t note, uint8_t vel) {
  if (!mcuUsb) {
    return;
  }
  uint8_t pkt[4];
  pkt[0] = 0x19;  // cable 1 | CIN 0x9 Note On
  pkt[1] = 0x90;
  pkt[2] = note & 0x7f;
  pkt[3] = vel & 0x7f;
  mcuUsb->writePacket(pkt);
}

static void mcuSendTransport(bool play) {
  // MCU: press = Note On 127, release = Note On 0
  if (play) {
    mcuSendNote(MCU_NOTE_PLAY, 127);
    delay(5);
    mcuSendNote(MCU_NOTE_PLAY, 0);
  } else {
    mcuSendNote(MCU_NOTE_STOP, 127);
    delay(5);
    mcuSendNote(MCU_NOTE_STOP, 0);
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
  // Assignment / timecode display (10 chars) — tempo / position
  if (cmd == 0x10 && mcuSysexLen >= 5) {
    const uint8_t *ascii = &mcuSysexBuf[5];
    const uint8_t alen = static_cast<uint8_t>(mcuSysexLen - 5);
    mcuApplyAssign(ascii, alen);
    return;
  }
  if (cmd == 0x12 && mcuSysexLen >= 6) {
    const uint8_t offset = mcuSysexBuf[5];
    const uint8_t *ascii = &mcuSysexBuf[6];
    const uint8_t alen = static_cast<uint8_t>(mcuSysexLen - 6);
    mcuApplyLcd(offset, ascii, alen);
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
    case 0x8:  // Note Off
    case 0x9: {  // Note On
      const uint8_t note = packet[2];
      const uint8_t vel = (cin == 0x8) ? 0 : packet[3];
      if (note < 8 && vel) {
        mcuSelected = note;
        mcuUpdateTrackName();
      }
      // Host transport LED feedback
      if (note == MCU_NOTE_PLAY) {
        mcuHostPlaying = (vel > 0);
        mcuTransportDirty = true;
      } else if (note == MCU_NOTE_STOP && vel > 0) {
        mcuHostPlaying = false;
        mcuTransportDirty = true;
      }
      break;
    }
    default:
      break;
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

static void mcuNudgeTrack(int8_t dir) {
  if (dir > 0) {
    mcuSendCc(0x30, 127);
    delay(5);
    mcuSendCc(0x30, 0);
    if (mcuSelected < 7) {
      mcuSelected++;
    }
  } else if (dir < 0) {
    mcuSendCc(0x31, 127);
    delay(5);
    mcuSendCc(0x31, 0);
    if (mcuSelected > 0) {
      mcuSelected--;
    }
  }
  mcuUpdateTrackName();
}

static void mcuBegin(Adafruit_USBD_MIDI *usb) {
  mcuUsb = usb;
  for (uint8_t i = 0; i < 8; i++) {
    memset(mcuStrip[i], ' ', 7);
    mcuStrip[i][7] = '\0';
  }
  mcuSelected = 0;
  mcuHaveLcd = false;
  mcuHaveAssign = false;
  mcuAssign[0] = '\0';
  mcuBpmFromAssign = 0;
  mcuHavePlaySeconds = false;
  mcuHostPlaying = false;
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

static uint16_t mcuBpm() {
  return mcuBpmFromAssign;
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

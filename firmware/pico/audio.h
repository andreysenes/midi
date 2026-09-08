#pragma once

#include <I2S.h>
#include <stdio.h>
#include "pico/mutex.h"
#include "config.h"
#include "synth.h"

// Porta de áudio: OUT = synth FM, CLOCK = click no mesmo jack, IN = silêncio
// (ADC futuro em I2S_DIN_PIN). PCM5102A só faz DAC.

static AudioPortMode audioPortMode = AUDIO_PORT_DEFAULT;
static I2S i2sOut(OUTPUT, I2S_BCK_PIN, I2S_DOUT_PIN);
static volatile bool i2sOk = false;
static uint16_t audioClockBpm = 120;
static uint32_t audioClockTick = 0;
static uint32_t audioClickRemain = 0;

static AudioPortMode audioGetPortMode() {
  return audioPortMode;
}

static void audioSetPortMode(AudioPortMode mode) {
  audioPortMode = mode;
  audioClockTick = 0;
  audioClickRemain = 0;
}

static void audioSetClockBpm(uint16_t bpm) {
  if (bpm < 20) {
    bpm = 20;
  }
  if (bpm > 400) {
    bpm = 400;
  }
  audioClockBpm = bpm;
}

static int16_t audioClockSample() {
  const uint32_t period = (SYNTH_SR * 60u) / audioClockBpm;
  if (audioClickRemain > 0) {
    audioClickRemain--;
    audioClockTick++;
    if (audioClockTick >= period) {
      audioClockTick = 0;
    }
    const int16_t amp = static_cast<int16_t>((static_cast<uint16_t>(synthMaster) * 24000) / 127);
    return amp;
  }
  audioClockTick++;
  if (audioClockTick >= period) {
    audioClockTick = 0;
    audioClickRemain = SYNTH_SR / 400;  // ~2.5 ms
    const int16_t amp = static_cast<int16_t>((static_cast<uint16_t>(synthMaster) * 24000) / 127);
    return amp;
  }
  return 0;
}

static int16_t audioNextSample() {
  if (!i2sOk) {
    return 0;
  }
  if (audioPortMode == AUDIO_PORT_CLOCK) {
    return audioClockSample();
  }
  if (audioPortMode == AUDIO_PORT_IN) {
    return 0;
  }
  return synthSample();
}

static void audioFill(uint16_t frames) {
  if (!i2sOk || !synthMxReady) {
    return;
  }
  int room = i2sOut.availableForWrite() / 4;
  if (room < 1) {
    return;
  }
  if (frames > 64) {
    frames = 64;
  }
  if (frames > static_cast<uint16_t>(room)) {
    frames = static_cast<uint16_t>(room);
  }
  while (frames > 0) {
    uint16_t n = frames > 8 ? 8 : frames;
    uint32_t packed[8];
    mutex_enter_blocking(&synthMx);
    for (uint16_t i = 0; i < n; i++) {
      const int16_t s = audioNextSample();
      packed[i] = (static_cast<uint32_t>(static_cast<uint16_t>(s)) << 16) |
                  static_cast<uint16_t>(s);
    }
    mutex_exit(&synthMx);
    i2sOut.write(reinterpret_cast<const uint8_t *>(packed),
                 static_cast<size_t>(n) * 4);
    frames = static_cast<uint16_t>(frames - n);
  }
}

static void audioTick() {
  audioFill(64);
}

static void handleSynthCommand(char *line) {
  while (*line == ' ') {
    line++;
  }
  if (*line == '\0' || *line == '?') {
    Serial.print(F("AUD  "));
    Serial.print(audioPortMode == AUDIO_PORT_OUT ? F("synth") :
                 (audioPortMode == AUDIO_PORT_CLOCK ? F("clock") : F("in")));
    Serial.print(F("  patch "));
    Serial.print(synthPatchIx);
    Serial.print(' ');
    Serial.print(synthPatch.name);
    Serial.print(F("  algo "));
    Serial.print(synthPatch.algo);
    Serial.print(' ');
    Serial.print(synthAlgoName(synthPatch.algo));
    Serial.print(F("  fb "));
    Serial.println(synthPatch.feedback);
    return;
  }
  const char cmd = *line++;
  while (*line == ' ') {
    line++;
  }
  if (cmd == 'm' || cmd == 'M') {
    const int m = atoi(line);
    if (m >= AUDIO_PORT_OUT && m <= AUDIO_PORT_IN) {
      audioSetPortMode(static_cast<AudioPortMode>(m));
    }
    Serial.print(F("AUD  modo="));
    Serial.println(static_cast<uint8_t>(audioPortMode));
    return;
  }
  if (cmd == 'p' || cmd == 'P') {
    synthSelectPatch(static_cast<uint8_t>(atoi(line)));
    Serial.print(F("AUD  patch "));
    Serial.print(synthPatchIx);
    Serial.print(' ');
    Serial.println(synthPatch.name);
    return;
  }
  if (cmd == 'a' || cmd == 'A') {
    synthPatch.algo = static_cast<uint8_t>(atoi(line) & 7);
    synthCommitPatch();
    Serial.print(F("AUD  algo="));
    Serial.println(synthPatch.algo);
    return;
  }
  if (cmd == 'f' || cmd == 'F') {
    int v = atoi(line);
    if (v < 0) {
      v = 0;
    }
    if (v > 7) {
      v = 7;
    }
    synthPatch.feedback = static_cast<uint8_t>(v);
    synthCommitPatch();
    Serial.print(F("AUD  fb="));
    Serial.println(synthPatch.feedback);
    return;
  }
  if (cmd == 'l' || cmd == 'L') {
    int r = 0, p = 0, a = 0;
    if (sscanf(line, "%d %d %d", &r, &p, &a) >= 1) {
      synthPatch.lfoRate = static_cast<uint8_t>(r < 0 ? 0 : (r > 127 ? 127 : r));
      synthPatch.lfoPitch = static_cast<uint8_t>(p < 0 ? 0 : (p > 127 ? 127 : p));
      synthPatch.lfoAmp = static_cast<uint8_t>(a < 0 ? 0 : (a > 127 ? 127 : a));
      synthCommitPatch();
    }
    Serial.print(F("AUD  lfo "));
    Serial.print(synthPatch.lfoRate);
    Serial.print(' ');
    Serial.print(synthPatch.lfoPitch);
    Serial.print(' ');
    Serial.println(synthPatch.lfoAmp);
    return;
  }
  if (cmd == 'o' || cmd == 'O') {
    int i = 0, r = 2, l = 100, a = 0, d = 40, s = 80, rel = 40, t = 0, v = 20;
    if (sscanf(line, "%d %d %d %d %d %d %d %d %d", &i, &r, &l, &a, &d, &s, &rel, &t, &v) >= 3) {
      if (i < 0) {
        i = 0;
      }
      if (i > 3) {
        i = 3;
      }
      auto clamp7 = [](int x) {
        if (x < 0) {
          return 0;
        }
        if (x > 127) {
          return 127;
        }
        return x;
      };
      FmOp &op = synthPatch.op[static_cast<uint8_t>(i)];
      op.ratio = static_cast<uint8_t>(r < 1 ? 1 : (r > 64 ? 64 : r));
      op.level = static_cast<uint8_t>(clamp7(l));
      op.atk = static_cast<uint8_t>(clamp7(a));
      op.dec = static_cast<uint8_t>(clamp7(d));
      op.sus = static_cast<uint8_t>(clamp7(s));
      op.rel = static_cast<uint8_t>(clamp7(rel));
      op.detune = static_cast<int8_t>(t < -64 ? -64 : (t > 63 ? 63 : t));
      op.velSens = static_cast<uint8_t>(clamp7(v));
      synthCommitPatch();
    }
    Serial.print(F("AUD  op"));
    Serial.println(i);
    return;
  }
  Serial.println(F("S  uso: S m 0|1|2 | S p 0-7 | S a 0-7 | S f 0-7 | S l r p a | S o i r l A D s R t v"));
}

static void setupAudio() {
  audioPortMode = AUDIO_PORT_DEFAULT;
  pinMode(I2S_DIN_PIN, INPUT);
  synthInit();
  i2sOut.setBCLK(I2S_BCK_PIN);
  i2sOut.setDOUT(I2S_DOUT_PIN);
  i2sOut.setBitsPerSample(16);
  i2sOut.setBuffers(12, 256);
  i2sOk = i2sOut.begin(static_cast<long>(SYNTH_SR));
  if (i2sOk) {
    for (uint8_t i = 0; i < 16; i++) {
      audioFill(64);
    }
  }
}

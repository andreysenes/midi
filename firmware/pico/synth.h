#pragma once

#include <string.h>
#include <math.h>
#include <Arduino.h>
#include "pico/mutex.h"
#include "config.h"

// 4-op FM, 8 algoritmos (família TX81Z/DX100), 6 vozes, 8 patches em RAM.
// Sine LUT + fase 32-bit. Envelopes ADSR por operador. Feedback no OP1.

static const uint8_t SYNTH_OPS = 4;
static const uint8_t SYNTH_VOICES = 6;
static const uint8_t SYNTH_PATCHES = 8;
static const uint32_t SYNTH_SR = AUDIO_SR;
static const uint16_t SINE_BITS = 10;
static const uint16_t SINE_LEN = 1024;
static const uint32_t ENV_MAX = 65535u << 8;

struct FmOp {
  uint8_t ratio;    // ×0.5  (2 = 1.0, 7 = 3.5, 28 = 14.0)
  uint8_t level;    // 0-127
  uint8_t atk;
  uint8_t dec;
  uint8_t sus;
  uint8_t rel;
  int8_t detune;    // −64..63
  uint8_t velSens;  // 0-127
};

struct FmPatch {
  char name[8];
  uint8_t algo;     // 0-7
  uint8_t feedback; // 0-7
  uint8_t lfoRate;
  uint8_t lfoPitch;
  uint8_t lfoAmp;
  FmOp op[SYNTH_OPS];
};

enum EnvStage : uint8_t { ENV_OFF = 0, ENV_ATT = 1, ENV_DEC = 2, ENV_SUS = 3, ENV_REL = 4 };

struct SynthVoice {
  uint8_t note;
  uint8_t vel;
  bool held;
  bool pendingRel;
  uint32_t age;
  uint32_t phase[SYNTH_OPS];
  uint32_t incr[SYNTH_OPS];
  uint32_t env[SYNTH_OPS];
  uint8_t envSt[SYNTH_OPS];
  uint8_t gain[SYNTH_OPS];
  int32_t fbMem;
  int32_t lp[SYNTH_OPS];
};

static int16_t sineLut[SINE_LEN];
static uint32_t noteIncr[128];
static FmPatch synthPatch;
static FmPatch synthBank[SYNTH_PATCHES];
static uint8_t synthPatchIx = 0;
static SynthVoice synthVoices[SYNTH_VOICES];
static uint32_t synthAge = 1;
static uint32_t lfoPhase = 0;
static uint32_t lfoIncr = 0;
static bool synthSustain = false;
static int16_t synthBend = 0;     // MIDI −8192..8191
static uint8_t synthMod = 0;      // CC1
static uint8_t synthMaster = 100;
static mutex_t synthMx;
static bool synthMxReady = false;
static int32_t outDc = 0;
static int32_t nSmooth = 256;
static bool liteLatch = false;

static void synthLock() {
  if (synthMxReady) {
    mutex_enter_blocking(&synthMx);
  }
}

static void synthUnlock() {
  if (synthMxReady) {
    mutex_exit(&synthMx);
  }
}
static uint32_t opAtkInc[SYNTH_OPS];
static uint32_t opDecInc[SYNTH_OPS];
static uint32_t opRelInc[SYNTH_OPS];
static uint32_t opSusEnv[SYNTH_OPS];

static int16_t sineAt(uint32_t phase) {
  return sineLut[phase >> (32 - SINE_BITS)];
}

// ±32767 → phase. 8192 ≈ ±0.4 rad no fundo de escala — sidebands cabem em 44.1 kHz.
static uint32_t phaseMod(int32_t s) {
  if (s > 32767) {
    s = 32767;
  }
  if (s < -32767) {
    s = -32767;
  }
  return static_cast<uint32_t>(s << 13);
}

static int32_t softSat(int32_t s) {
  if (s > 20000) {
    s = 20000 + ((s - 20000) >> 2);
  } else if (s < -20000) {
    s = -20000 + ((s + 20000) >> 2);
  }
  if (s > 32767) {
    s = 32767;
  }
  if (s < -32767) {
    s = -32767;
  }
  return s;
}

static uint32_t envIncFor(uint8_t rate, bool release) {
  // rate 0 = rápido (~4 ms). 127 ≈ 3–5 s. Só na troca de patch — não no sample.
  float sec = 0.004f * powf(1.055f, static_cast<float>(rate));
  if (release) {
    sec *= 1.15f;
    if (sec < 0.022f) {
      sec = 0.022f;
    }
  }
  const float inc = static_cast<float>(ENV_MAX) / (static_cast<float>(SYNTH_SR) * sec);
  uint32_t v = static_cast<uint32_t>(inc + 0.5f);
  if (v < 1) {
    v = 1;
  }
  return v;
}

static void synthRefreshRates() {
  for (uint8_t i = 0; i < SYNTH_OPS; i++) {
    opAtkInc[i] = envIncFor(synthPatch.op[i].atk, false);
    opDecInc[i] = envIncFor(synthPatch.op[i].dec, false);
    opRelInc[i] = envIncFor(synthPatch.op[i].rel, true);
    opSusEnv[i] = (static_cast<uint32_t>(synthPatch.op[i].sus) * ENV_MAX) / 127u;
  }
}

static void refreshVoiceGain(SynthVoice &v) {
  for (uint8_t i = 0; i < SYNTH_OPS; i++) {
    int32_t lvl = synthPatch.op[i].level;
    const uint8_t vs = synthPatch.op[i].velSens;
    if (vs) {
      const int32_t velAmt = 127 - ((vs * (127 - static_cast<int>(v.vel))) / 127);
      lvl = (lvl * velAmt) / 127;
    }
    if (lvl < 0) {
      lvl = 0;
    }
    if (lvl > 127) {
      lvl = 127;
    }
    v.gain[i] = static_cast<uint8_t>(lvl);
  }
}

static uint32_t opIncrFor(uint8_t note, const FmOp &op) {
  uint8_t n = note;
  if (n > 127) {
    n = 127;
  }
  uint64_t base = noteIncr[n];
  uint8_t ratio = op.ratio;
  if (ratio < 1) {
    ratio = 1;
  }
  uint64_t incr = (base * ratio) / 2;
  if (op.detune) {
    incr = static_cast<uint64_t>(static_cast<int64_t>(incr) +
                                 (static_cast<int64_t>(incr) * op.detune) / 2048);
  }
  if (synthBend) {
    incr = static_cast<uint64_t>(static_cast<int64_t>(incr) +
                                  (static_cast<int64_t>(incr) * synthBend) / 65536);
  }
  if (incr > 0xffffffffULL) {
    incr = 0xffffffffULL;
  }
  return static_cast<uint32_t>(incr);
}

static void voiceRefreshIncr(SynthVoice &v) {
  for (uint8_t i = 0; i < SYNTH_OPS; i++) {
    v.incr[i] = opIncrFor(v.note, synthPatch.op[i]);
  }
}

static void envTick(SynthVoice &v, uint8_t i) {
  uint32_t &e = v.env[i];
  uint8_t &st = v.envSt[i];
  if (st == ENV_OFF) {
    e = 0;
    return;
  }
  if (st == ENV_ATT) {
    e += opAtkInc[i];
    if (e >= ENV_MAX) {
      e = ENV_MAX;
      st = ENV_DEC;
    }
    return;
  }
  if (st == ENV_DEC) {
    const uint32_t sus = opSusEnv[i];
    const uint32_t inc = opDecInc[i];
    if (e <= sus + inc) {
      e = sus;
      st = ENV_SUS;
      return;
    }
    e -= inc;
    return;
  }
  if (st == ENV_SUS) {
    e = opSusEnv[i];
    return;
  }
  const uint32_t inc = opRelInc[i];
  uint32_t drop = (e >> 8) + (inc >> 2) + 48u;
  if (drop < 48u) {
    drop = 48u;
  }
  if (e <= drop) {
    e = 0;
    st = ENV_OFF;
    return;
  }
  e -= drop;
}

static int32_t renderOp(SynthVoice &v, uint8_t i, uint32_t mod) {
  envTick(v, i);
  if (v.envSt[i] == ENV_OFF) {
    return 0;
  }
  v.phase[i] += v.incr[i];
  const int32_t s = sineAt(v.phase[i] + mod);
  const int32_t env = static_cast<int32_t>(v.env[i] >> 8);
  const int32_t x = (s * env) >> 16;
  return (x * static_cast<int32_t>(v.gain[i]) * 516) >> 16;
}

static bool voiceBusy(const SynthVoice &v) {
  if (v.held || v.pendingRel) {
    return true;
  }
  for (uint8_t i = 0; i < SYNTH_OPS; i++) {
    if (v.envSt[i] != ENV_OFF) {
      return true;
    }
  }
  return false;
}

static int32_t voiceSample(SynthVoice &v) {
  const uint8_t fb = synthPatch.feedback > 7 ? 7 : synthPatch.feedback;
  const int32_t o0 = renderOp(v, 0, fb ? (phaseMod(v.fbMem) >> (8 - fb)) : 0);
  v.lp[0] += (o0 - v.lp[0]) >> 2;
  v.fbMem = (v.fbMem + v.lp[0]) >> 1;
  const int32_t m0 = softSat(v.lp[0]);
  int32_t o1, o2, o3, mix = 0;
  switch (synthPatch.algo) {
    case 0:
      o1 = renderOp(v, 1, phaseMod(m0));
      v.lp[1] += (o1 - v.lp[1]) >> 2;
      o2 = renderOp(v, 2, phaseMod(softSat(v.lp[1])));
      v.lp[2] += (o2 - v.lp[2]) >> 2;
      o3 = renderOp(v, 3, phaseMod(softSat(v.lp[2])));
      mix = o3;
      break;
    case 1:
      o1 = renderOp(v, 1, phaseMod(m0));
      v.lp[1] += (o1 - v.lp[1]) >> 2;
      o2 = renderOp(v, 2, 0);
      v.lp[2] += (o2 - v.lp[2]) >> 2;
      o3 = renderOp(v, 3, phaseMod(softSat((v.lp[1] + v.lp[2]) >> 1)));
      mix = o3;
      break;
    case 2:
      o1 = renderOp(v, 1, phaseMod(m0));
      o2 = renderOp(v, 2, 0);
      v.lp[2] += (o2 - v.lp[2]) >> 2;
      o3 = renderOp(v, 3, phaseMod(softSat(v.lp[2])));
      mix = (o1 + o3) >> 1;
      break;
    case 3:
      o1 = renderOp(v, 1, phaseMod(m0));
      v.lp[1] += (o1 - v.lp[1]) >> 2;
      o2 = renderOp(v, 2, phaseMod(softSat(v.lp[1])));
      o3 = renderOp(v, 3, 0);
      mix = (o2 + o3) >> 1;
      break;
    case 4:
      o1 = renderOp(v, 1, phaseMod(m0));
      o2 = renderOp(v, 2, 0);
      o3 = renderOp(v, 3, 0);
      mix = (o1 + o2 + o3) / 3;
      break;
    case 5:
      o1 = renderOp(v, 1, phaseMod(m0));
      o2 = renderOp(v, 2, phaseMod(m0));
      o3 = renderOp(v, 3, phaseMod(m0));
      mix = (o1 + o2 + o3) / 3;
      break;
    case 6:
      o1 = renderOp(v, 1, phaseMod(m0));
      o2 = renderOp(v, 2, 0);
      v.lp[2] += (o2 - v.lp[2]) >> 2;
      o3 = renderOp(v, 3, phaseMod(softSat(v.lp[2])) >> 1);
      mix = (o1 + o3) >> 1;
      break;
    default:
      o1 = renderOp(v, 1, 0);
      o2 = renderOp(v, 2, 0);
      o3 = renderOp(v, 3, 0);
      mix = (o0 + o1 + o2 + o3) >> 2;
      break;
  }
  const uint8_t lfoA = synthPatch.lfoAmp;
  if (lfoA || synthMod) {
    const int32_t lfo = sineAt(lfoPhase);
    const int32_t depth = static_cast<int32_t>(lfoA) + (static_cast<int32_t>(synthMod) / 2);
    mix = mix + ((mix * (lfo >> 8) * depth) >> 22);
  }
  return mix;
}

// 4+ vozes: só o par 2-op (OP1→OP2). Os outros envelopes avançam para não
// prender a voz, mas o sine fica de fora — 5–6 teclas cabem em tempo real.
static int32_t voiceSampleLite(SynthVoice &v) {
  const uint8_t fb = synthPatch.feedback > 7 ? 7 : synthPatch.feedback;
  const int32_t o0 = renderOp(v, 0, fb ? (phaseMod(v.fbMem) >> (8 - fb)) : 0);
  v.fbMem = (v.fbMem + o0) >> 1;
  const int32_t o1 = renderOp(v, 1, phaseMod(o0));
  envTick(v, 2);
  envTick(v, 3);
  v.phase[2] += v.incr[2];
  v.phase[3] += v.incr[3];
  return o1;
}

static int8_t stealVoice(uint8_t note) {
  int8_t freeSlot = -1;
  int8_t steal = 0;
  uint32_t oldest = 0xffffffffUL;
  uint32_t quiet = 0xffffffffUL;
  for (uint8_t i = 0; i < SYNTH_VOICES; i++) {
    if (synthVoices[i].note == note && voiceBusy(synthVoices[i])) {
      return static_cast<int8_t>(i);
    }
    if (!voiceBusy(synthVoices[i])) {
      if (freeSlot < 0) {
        freeSlot = static_cast<int8_t>(i);
      }
      continue;
    }
    if (!synthVoices[i].held && synthVoices[i].age < oldest) {
      oldest = synthVoices[i].age;
      steal = static_cast<int8_t>(i);
    }
    uint32_t e = 0;
    for (uint8_t o = 0; o < SYNTH_OPS; o++) {
      e += synthVoices[i].env[o] >> 8;
    }
    if (e < quiet) {
      quiet = e;
      if (freeSlot < 0 && oldest == 0xffffffffUL) {
        steal = static_cast<int8_t>(i);
      }
    }
  }
  if (freeSlot >= 0) {
    return freeSlot;
  }
  return steal;
}

static void startOpEnvs(SynthVoice &v) {
  for (uint8_t i = 0; i < SYNTH_OPS; i++) {
    v.env[i] = 0;
    v.envSt[i] = ENV_ATT;
    v.phase[i] = 0;
  }
  v.fbMem = 0;
  memset(v.lp, 0, sizeof(v.lp));
}

static void releaseVoice(SynthVoice &v) {
  v.held = false;
  v.pendingRel = false;
  for (uint8_t i = 0; i < SYNTH_OPS; i++) {
    if (v.envSt[i] != ENV_OFF) {
      v.envSt[i] = ENV_REL;
    }
  }
}

static void synthNoteOn(uint8_t note, uint8_t vel) {
  synthLock();
  if (vel == 0) {
    // note-off via vel 0
    for (uint8_t i = 0; i < SYNTH_VOICES; i++) {
      if (synthVoices[i].note == note && synthVoices[i].held) {
        synthVoices[i].held = false;
        if (synthSustain) {
          synthVoices[i].pendingRel = true;
        } else {
          releaseVoice(synthVoices[i]);
        }
      }
    }
    synthUnlock();
    return;
  }
  const int8_t slot = stealVoice(note);
  SynthVoice &v = synthVoices[slot];
  v.note = note;
  v.vel = vel < 1 ? 1 : vel;
  v.held = true;
  v.pendingRel = false;
  v.age = synthAge++;
  voiceRefreshIncr(v);
  refreshVoiceGain(v);
  startOpEnvs(v);
  synthUnlock();
}

static void synthNoteOff(uint8_t note) {
  synthLock();
  for (uint8_t i = 0; i < SYNTH_VOICES; i++) {
    if (synthVoices[i].note == note && synthVoices[i].held) {
      synthVoices[i].held = false;
      if (synthSustain) {
        synthVoices[i].pendingRel = true;
      } else {
        releaseVoice(synthVoices[i]);
      }
    }
  }
  synthUnlock();
}

static void synthSetSustain(bool on) {
  synthLock();
  synthSustain = on;
  if (!on) {
    for (uint8_t i = 0; i < SYNTH_VOICES; i++) {
      if (synthVoices[i].pendingRel) {
        releaseVoice(synthVoices[i]);
      }
    }
  }
  synthUnlock();
}

static void synthPanic() {
  synthLock();
  memset(synthVoices, 0, sizeof(synthVoices));
  outDc = 0;
  nSmooth = 256;
  liteLatch = false;
  synthUnlock();
}

static void synthSetBend(int16_t bend) {
  synthLock();
  synthBend = bend;
  for (uint8_t i = 0; i < SYNTH_VOICES; i++) {
    if (voiceBusy(synthVoices[i])) {
      voiceRefreshIncr(synthVoices[i]);
    }
  }
  synthUnlock();
}

static void synthSetMod(uint8_t v) {
  synthMod = v > 127 ? 127 : v;
}

static void synthSetMaster(uint8_t v) {
  synthMaster = v > 127 ? 127 : v;
}

static void synthRefreshLfo() {
  const float hz = 0.08f + (static_cast<float>(synthPatch.lfoRate) * 10.0f) / 127.0f;
  lfoIncr = static_cast<uint32_t>(hz * 4294967296.0f / static_cast<float>(SYNTH_SR));
  synthRefreshRates();
}

static void synthSelectPatch(uint8_t ix) {
  synthLock();
  if (ix >= SYNTH_PATCHES) {
    ix = SYNTH_PATCHES - 1;
  }
  synthPatchIx = ix;
  synthPatch = synthBank[ix];
  synthRefreshLfo();
  for (uint8_t i = 0; i < SYNTH_VOICES; i++) {
    if (voiceBusy(synthVoices[i])) {
      voiceRefreshIncr(synthVoices[i]);
      refreshVoiceGain(synthVoices[i]);
    }
  }
  synthUnlock();
}

static void synthCommitPatch() {
  synthLock();
  synthBank[synthPatchIx] = synthPatch;
  synthRefreshLfo();
  for (uint8_t i = 0; i < SYNTH_VOICES; i++) {
    if (voiceBusy(synthVoices[i])) {
      voiceRefreshIncr(synthVoices[i]);
      refreshVoiceGain(synthVoices[i]);
    }
  }
  synthUnlock();
}

static void setOp(FmOp &o, uint8_t ratio, uint8_t level, uint8_t a, uint8_t d, uint8_t s,
                  uint8_t r, int8_t det, uint8_t vel) {
  o.ratio = ratio;
  o.level = level;
  o.atk = a;
  o.dec = d;
  o.sus = s;
  o.rel = r;
  o.detune = det;
  o.velSens = vel;
}

static void initPatch(FmPatch &p, const char *name, uint8_t algo, uint8_t fb,
                      uint8_t lr, uint8_t lp, uint8_t la) {
  memset(&p, 0, sizeof(p));
  strncpy(p.name, name, sizeof(p.name) - 1);
  p.algo = algo;
  p.feedback = fb;
  p.lfoRate = lr;
  p.lfoPitch = lp;
  p.lfoAmp = la;
}

static void synthInitPatches() {
  // EPNO — Rhodes-ish: dois pares 2-op, tine (ratio 14)
  initPatch(synthBank[0], "EPNO", 2, 1, 12, 0, 0);
  setOp(synthBank[0].op[0], 2, 48, 8, 52, 0, 44, 3, 60);
  setOp(synthBank[0].op[1], 2, 100, 6, 36, 80, 50, 0, 25);
  setOp(synthBank[0].op[2], 16, 28, 8, 48, 0, 40, -2, 70);
  setOp(synthBank[0].op[3], 2, 96, 6, 40, 70, 52, 0, 20);

  initPatch(synthBank[1], "BRASS", 0, 3, 28, 10, 4);
  setOp(synthBank[1].op[0], 2, 64, 42, 50, 40, 40, 6, 50);
  setOp(synthBank[1].op[1], 2, 70, 38, 48, 50, 44, 0, 40);
  setOp(synthBank[1].op[2], 2, 55, 36, 44, 55, 48, -4, 30);
  setOp(synthBank[1].op[3], 2, 118, 40, 40, 80, 52, 0, 20);

  initPatch(synthBank[2], "BELL", 0, 3, 10, 0, 0);
  setOp(synthBank[2].op[0], 7, 64, 2, 70, 0, 70, 8, 60);
  setOp(synthBank[2].op[1], 11, 72, 0, 64, 0, 68, -6, 50);
  setOp(synthBank[2].op[2], 2, 50, 0, 58, 0, 72, 0, 30);
  setOp(synthBank[2].op[3], 2, 120, 0, 62, 0, 78, 0, 20);

  initPatch(synthBank[3], "BASS", 2, 2, 8, 0, 0);
  setOp(synthBank[3].op[0], 1, 85, 0, 36, 30, 32, 0, 55);
  setOp(synthBank[3].op[1], 2, 118, 0, 40, 90, 36, 0, 20);
  setOp(synthBank[3].op[2], 4, 40, 0, 28, 0, 28, 3, 40);
  setOp(synthBank[3].op[3], 2, 110, 0, 34, 85, 34, 0, 15);

  initPatch(synthBank[4], "ORGAN", 7, 0, 22, 0, 12);
  setOp(synthBank[4].op[0], 2, 90, 0, 8, 127, 20, 0, 10);
  setOp(synthBank[4].op[1], 4, 72, 0, 8, 127, 20, 2, 10);
  setOp(synthBank[4].op[2], 6, 55, 0, 8, 127, 20, -2, 10);
  setOp(synthBank[4].op[3], 8, 40, 0, 8, 127, 20, 0, 10);

  initPatch(synthBank[5], "FLUTE", 4, 1, 24, 8, 18);
  setOp(synthBank[5].op[0], 2, 28, 48, 30, 40, 50, 0, 20);
  setOp(synthBank[5].op[1], 2, 118, 52, 24, 90, 56, 0, 15);
  setOp(synthBank[5].op[2], 4, 18, 56, 20, 70, 54, 3, 10);
  setOp(synthBank[5].op[3], 6, 12, 50, 22, 60, 52, 0, 10);

  initPatch(synthBank[6], "CLAV", 2, 4, 14, 4, 0);
  setOp(synthBank[6].op[0], 2, 72, 2, 22, 0, 24, 10, 80);
  setOp(synthBank[6].op[1], 2, 115, 0, 18, 20, 22, 0, 35);
  setOp(synthBank[6].op[2], 6, 70, 0, 16, 0, 20, -8, 70);
  setOp(synthBank[6].op[3], 2, 108, 0, 20, 10, 22, 0, 30);

  initPatch(synthBank[7], "LEAD", 0, 5, 32, 16, 8);
  setOp(synthBank[7].op[0], 2, 72, 8, 28, 50, 28, 12, 55);
  setOp(synthBank[7].op[1], 2, 80, 10, 26, 55, 30, 0, 40);
  setOp(synthBank[7].op[2], 3, 48, 12, 24, 40, 32, -6, 35);
  setOp(synthBank[7].op[3], 2, 122, 6, 22, 70, 34, 0, 20);

  synthSelectPatch(0);
}

static void synthInit() {
  mutex_init(&synthMx);
  synthMxReady = true;
  for (uint16_t i = 0; i < SINE_LEN; i++) {
    sineLut[i] = static_cast<int16_t>(sinf(6.28318530718f * i / SINE_LEN) * 32767.0f);
  }
  for (uint8_t n = 0; n < 128; n++) {
    const float hz = 440.0f * powf(2.0f, (static_cast<int>(n) - 69) / 12.0f);
    noteIncr[n] = static_cast<uint32_t>(hz * 4294967296.0f / static_cast<float>(SYNTH_SR));
  }
  memset(synthVoices, 0, sizeof(synthVoices));
  synthInitPatches();
}

static int16_t synthSample() {
  lfoPhase += lfoIncr;
  bool active[SYNTH_VOICES];
  uint8_t n = 0;
  for (uint8_t i = 0; i < SYNTH_VOICES; i++) {
    const SynthVoice &v = synthVoices[i];
    const bool on = v.held || v.pendingRel || v.envSt[0] != ENV_OFF ||
                    v.envSt[1] != ENV_OFF || v.envSt[2] != ENV_OFF ||
                    v.envSt[3] != ENV_OFF;
    active[i] = on;
    if (on) {
      n++;
    }
  }
  if (n >= 4) {
    liteLatch = true;
  } else if (n == 0) {
    liteLatch = false;
  }
  const bool lite = liteLatch;
  const int32_t lfo = (!lite && (synthPatch.lfoPitch || synthMod)) ? sineAt(lfoPhase) : 0;
  const int32_t lfoDepth = lite ? 0 : (synthPatch.lfoPitch + (synthMod / 4));
  int32_t mix = 0;
  for (uint8_t i = 0; i < SYNTH_VOICES; i++) {
    if (!active[i]) {
      continue;
    }
    SynthVoice &v = synthVoices[i];
    if (lfoDepth) {
      const int32_t extra =
          (((static_cast<int32_t>(v.incr[3] >> 14) * (lfo >> 8)) >> 8) * lfoDepth) >> 7;
      for (uint8_t o = 0; o < SYNTH_OPS; o++) {
        v.phase[o] += static_cast<uint32_t>(extra);
      }
    }
    mix += lite ? voiceSampleLite(v) : voiceSample(v);
  }
  const int32_t nTarget = n == 0 ? 256 : (static_cast<int32_t>(n) << 8);
  nSmooth += (nTarget - nSmooth) >> 7;
  if (nSmooth < 32) {
    nSmooth = 32;
  }
  if (n > 0) {
    mix = (mix << 8) / nSmooth;
  }
  mix = (mix * 3) / 4;
  mix = (mix * static_cast<int32_t>(synthMaster)) / 120;
  if (mix > 24000) {
    mix = 24000 + ((mix - 24000) >> 3);
  } else if (mix < -24000) {
    mix = -24000 + ((mix + 24000) >> 3);
  }
  const int32_t y = mix - outDc;
  outDc += (mix - outDc) >> 8;
  if (y > 32767) {
    return 32767;
  }
  if (y < -32767) {
    return -32767;
  }
  return static_cast<int16_t>(y);
}

static const char *synthAlgoName(uint8_t a) {
  switch (a) {
    case 0: return "1-2-3-4";
    case 1: return "12+3>4";
    case 2: return "12|34";
    case 3: return "123+4";
    case 4: return "12+3+4";
    case 5: return "1>234";
    case 6: return "12+34";
    default: return "1+2+3+4";
  }
}

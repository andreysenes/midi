#pragma once

#include "config.h"

// Porta de áudio configurável. OUT e CLOCK usam o PCM5102A (I2S out).
// IN reserva GP13 para um ADC no mesmo BCK/LRCK — o PCM5102A não converte analógico→digital.
// I2S / synth ainda não arrancam; só o modo fica escolhível.

static AudioPortMode audioPortMode = AUDIO_PORT_DEFAULT;

static AudioPortMode audioGetPortMode() {
  return audioPortMode;
}

static void audioSetPortMode(AudioPortMode mode) {
  audioPortMode = mode;
}

static void setupAudio() {
  audioPortMode = AUDIO_PORT_DEFAULT;
  pinMode(I2S_DIN_PIN, INPUT);
}

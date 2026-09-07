#pragma once

// Mapa SA-1 — log completo 2026-08-31 (aprender + correcao KO0 piano vs KO4 botoes).
// KI5–7: teórico (saltados no aprender — validar com DOWN na sonda).
enum : uint8_t {
  BTN_TEMPO_UP = 210,
  BTN_TEMPO_DN = 211,
  BTN_SEL = 212,
  BTN_STOP = 215,
  BTN_DEMO = 216,
  CELL_EMPTY = 255,
};

static const uint8_t KO_COUNT = 7;
static const uint8_t KI_COUNT = 8;

// [KO][KI] — F3=53 .. C6=84. KO1–KO3: KI4=nota grave, KI0=aguda (invertido vs Casio doc).
static const uint8_t MATRIX[7][8] = {
    {53, 54, 55, 56, 57, 58, 59, 60}, // KO0 piano F3..C4
    {61, 62, 63, 64, 65, 66, 67, 68}, // KO1 F5..A5, A#5..C6
    {69, 70, 71, 72, 73, 74, 75, 76}, // KO2 C#5..A4, D5..E5
    {77, 78, 79, 80, 81, 82, 83, 84}, // KO3 F4..C#4, F#4..G#4
    {200, 201, 202, 203, 204, 210, 255, 212}, // KO4 0-4, tempo+, sel
    {205, 206, 207, 208, 209, 215, 211, 255}, // KO5 5-9, stop, tempo-
    {255, 255, 255, 255, 255, 216, 255, 255} // KO6 demo
};

static const uint8_t MIDI_CHANNEL = 1;
static const uint8_t NOTE_VELOCITY = 100;
static const int8_t OCTAVE_MIN = -2;
static const int8_t OCTAVE_MAX = 3;
static const uint8_t CC_VOLUME = 7;
static const uint8_t CC_TRACK = 14;  // EC11-1: seleção de track (CC relativo 65/63)
static const uint16_t BUTTON_DEBOUNCE_MS = 25;

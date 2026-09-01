#pragma once

// Mapa SA-1 — log completo 2026-08-31 (aprender + correcao KO0 piano vs KO4 botoes).
// KI5–7: teórico (saltados no aprender — validar com DOWN na sonda).
enum : uint8_t {
  BTN_STOP = 215,
  BTN_DEMO = 216,
  CELL_EMPTY = 255,
};

static const uint8_t KO_COUNT = 7;
static const uint8_t KI_COUNT = 8;

// [KO][KI] — F3=53 .. C6=84. KO1–KO3: KI4=nota grave, KI0=aguda (invertido vs Casio doc).
static const uint8_t MATRIX[7][8] = {
    {53, 54, 55, 56, 57, 58, 59, 60},                             // KO0 piano F3..C4
    {81, 80, 79, 78, 77, 82, 83, 84},                             // KO1 F5..A5, A#5..C6
    {73, 72, 71, 70, 69, 74, 75, 76},                             // KO2 C#5..A4, D5..E5
    {65, 64, 63, 62, 61, 66, 67, 68},                             // KO3 F4..C#4, F#4..G#4
    {200, 201, 202, 203, 204, CELL_EMPTY, CELL_EMPTY, CELL_EMPTY}, // KO4 botoes 0-4 (teórico)
    {205, 206, 207, 208, 209, BTN_STOP, CELL_EMPTY, CELL_EMPTY},   // KO5 botoes 5-9, stop
    {CELL_EMPTY, CELL_EMPTY, CELL_EMPTY, CELL_EMPTY, CELL_EMPTY, BTN_DEMO, CELL_EMPTY, CELL_EMPTY},
};

static const uint8_t MIDI_CHANNEL = 1;
static const uint8_t NOTE_VELOCITY = 100;
static const int8_t OCTAVE_MIN = -2;
static const int8_t OCTAVE_MAX = 3;
static const uint8_t CC_VOLUME = 7;
static const uint16_t BUTTON_DEBOUNCE_MS = 25;

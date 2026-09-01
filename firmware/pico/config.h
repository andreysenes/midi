#pragma once

#include "matrix.h"

// Pico + CJMCU-2317 (MCP23017 I2C) + OLED 0.91" + 2× EC11 + KY-023 + WS2812 ×8.
// Isolar o M6387. GND comum. Lógica 3,3 V. WS2812 VCC no VBUS 5 V.
// RESET do MCP no 3V3; A0–A2 no GND (0x20).

// GPA0–7 = KI0–7. GPB0–6 = KO0–6. Se DOWN bater com KIrev/KOrev da sonda, ative aqui.
static const bool MCP_KO_REV = false;
static const bool MCP_KI_REV = false;

static const uint8_t MCP_ADDR = 0x20;
static const uint8_t I2C_SDA_PIN = 0;
static const uint8_t I2C_SCL_PIN = 1;

// OLED 0.91" SSD1306 128×32 I2C. Silk SCK = SCL. Mesmo barramento do MCP (GP0/GP1).
// Endereço típico 0x3C; se a sonda achar 0x3D, mude aqui.
static const uint8_t OLED_ADDR = 0x3C;
static const uint8_t OLED_WIDTH = 128;
static const uint8_t OLED_HEIGHT = 32;
static const uint16_t OLED_PERIOD_MS = 50;

// GPA0–7 = KI0–7 (entrada + pullup). GPB0–6 = KO0–6 (saída, ativa em LOW).
static const uint8_t MCP_KI_PORT = 0;
static const uint8_t MCP_KO_PORT = 1;

struct EncoderPins {
  uint8_t a;
  uint8_t b;
};

static const EncoderPins ENC_OCTAVE = {18, 19};
static const EncoderPins ENC_VOLUME = {21, 22};

// KY-023: alimentar no 3V3 do Pico, nunca no 5 V (ADC do Pico queima).
static const uint8_t JOY_X_PIN = 26;
static const uint8_t JOY_Y_PIN = 27;
static const uint8_t JOY_SW_PIN = 20;
static const bool JOY_INVERT_X = false;
static const bool JOY_INVERT_Y = false;
static const uint8_t CC_MODULATION = 1;
static const uint8_t CC_SUSTAIN = 64;

// Barra WS2812 8×1 (5050). VCC = 4–7 V → VBUS. DIN = GP16. Ainda sem uso no firmware.
static const uint8_t WS_PIN = 16;
static const uint8_t WS_COUNT = 8;

static const uint8_t LED_PIN = 25;
static const uint16_t SCAN_SETTLE_US = 20;

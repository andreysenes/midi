#pragma once

#include "matrix.h"

// Pico + CJMCU-2317 (MCP23017 I2C) + OLED 0.91" + 2× EC11 + KY-023 + WS2812 ×8
// + PCM5102 I2S. Isolar o M6387. GND comum. Lógica 3,3 V. WS2812 VCC no VBUS 5 V.
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

// Painel: esquerda = EC11-2 volume (GP21/22), direita = EC11-1 track (GP18/19).
static const EncoderPins ENC_OCTAVE = {18, 19};
static const EncoderPins ENC_VOLUME = {21, 22};
static const bool ENC_OCTAVE_REV = true;  // EC11-1: horário = +
static const bool ENC_VOLUME_REV = false;

// KY-023: silk do Pico = A0 (GP26), A1 (GP27), A2 (GP28). VRx=A0, VRy=A1.
// Alimentar no 3V3 do Pico, nunca no 5 V (ADC do Pico queima).
static const uint8_t JOY_X_PIN = 26; // A0
static const uint8_t JOY_Y_PIN = 27; // A1
static const uint8_t JOY_SW_PIN = 20;
static const bool JOY_INVERT_X = true;
static const bool JOY_INVERT_Y = false;
static const bool JOY_SWAP_XY = true;
// Alcance calibrado na sonda (Lab → cola aqui). Usar no MIDI quando ligares o span.
static const int JOY_MIN_X = 0;
static const int JOY_MAX_X = 4095;
static const int JOY_MIN_Y = 0;
static const int JOY_MAX_Y = 4095;
static const int JOY_ADC_MAX = 4095;   // 1023 se a sonda disser 10-bit
static const int JOY_DEADZONE = 16;
static const int JOY_CIRCLE_R = 0;     // raio do anel (Lab assistente)
static const uint8_t CC_MODULATION = 1;
static const uint8_t CC_SUSTAIN = 64;

// Barra WS2812 8×1 (5050). Silk SND IN VCC GND. VCC → VBUS. IN → GP16.
// WS_REV: IN no lado do LED 8 (lógico 0 = pixel físico 7). Bank 1 / tempo 1 = LED da esquerda.
static const uint8_t WS_PIN = 16;
static const uint8_t WS_COUNT = 8;
static const bool WS_REV = true;

// Porta de áudio: o software escolhe o modo (OLED/encoder, mais tarde).
// OUT e CLOCK saem no jack do PCM5102A. IN não passa neste chip — é só DAC.
enum AudioPortMode : uint8_t {
  AUDIO_PORT_OUT = 0,    // synth / line out
  AUDIO_PORT_CLOCK = 1,  // pulso/click de clock no mesmo jack
  AUDIO_PORT_IN = 2,     // line in (ADC futuro em I2S_DIN_PIN)
};

static const AudioPortMode AUDIO_PORT_DEFAULT = AUDIO_PORT_OUT;

// PCM5102A I2S DAC (módulo roxo, jack 3.5 mm LINE OUT).
// Arduino-Pico (Earle): I2S.setBCLK(I2S_BCK_PIN) usa BCK e BCK+1 = LRCK.
// Pico DOUT → DIN do módulo. SCK do módulo no GND (PLL). Não confundir com o SCK do OLED.
static const uint8_t I2S_BCK_PIN = 10;
static const uint8_t I2S_LRCK_PIN = 11;
static const uint8_t I2S_DOUT_PIN = 12;
static const uint8_t I2S_DIN_PIN = 13; // Pico DIN ← ADC futuro (PCM1808 etc.). Livre por agora.

static const uint8_t LED_PIN = 25;
static const uint16_t SCAN_SETTLE_US = 20;

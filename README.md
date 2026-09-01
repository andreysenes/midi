# Casio SA-1 → MIDI

**Agora: banco de testes no browser** (`lab/run.sh`) — sem Arduino IDE. Matriz: [Etapa 1](ETAPA-1.md).

A placa `M3210-MAIM(F)` continua no teclado. O `MCP23017` lê a matriz; o **Raspberry Pi Pico 2020** manda USB-MIDI, lê os dois EC11 e o joystick KY-023, e mostra estado no OLED 0.91".

Peças desta montagem: Pico 2020, ZS-040 (HC-05), 2× EC11 sem clique, joystick KY-023, **CJMCU-2317** (MCP23017 I2C), OLED 0.91" SSD1306 (128×32, 4 pinos).

## Quem faz o quê

| Peça | Papel |
| --- | --- |
| **CJMCU-2317 (MCP23017)** | 15 fios da matriz (7 KO + 8 KI). Até o Pico: VCC, GND, SDA, SCL + RESET e A0–A2 |
| **Pico 2020** | USB-MIDI + OLED + 2 encoders + joystick |
| **2× EC11 sem clique** | Oitava e volume (CC 7). Sem switch — programa pelos botões 0–9 da placa |
| **Joystick KY-023** | X = pitch bend, Y = CC 1 (mod), clique = sustain (CC 64) |
| **ZS-040 (HC-05)** | MIDI serial por Bluetooth SPP. Não é BLE-MIDI |
| **OLED 0.91" SSD1306** | 128×32 I2C. Oitava, volume, programa, última nota, sustain |

O `M6387` original sai da matriz. O som interno do Casio some.

## Isolar o chip

1. Lado dos componentes: SDIL de 30 pinos, pino 1 na marca/chanfro.
2. Corte as trilhas (ou levante os pinos) **11–18** e **24–30**.
3. Solde no lado da **matriz**, não no pino do chip.
4. GND comum (pino 7 ou negativo das pilhas). Pico / MCP só em **3,3 V**.

## Ligação

Varredura **ativa em LOW** (pull-up do MCP). A borracha só curto-circuita KO↔KI; a polaridade é invertida em relação ao Casio original, os fios são os mesmos.

### CJMCU-2317 (MCP23017) ↔ Casio

Chip `MCP23017-E/SS`, I2C até 1,7 MHz. O firmware usa 400 kHz. 1,8–5,5 V — no Pico use **3V3**.

O verso do CJMCU (header 2×10) escreve `B0/A0`–`B7/A7`: cada linha é **dois furos** (GPB | GPA). `A0` `A1` `A2` da coluna esquerda são **endereço I2C**, não GPIO. Mapa e Mermaid: [ETAPA-1](ETAPA-1.md).

| Casio | M6387 | CJMCU-2317 |
| --- | --- | --- |
| KI0–KI7 | 11–18 | GPA0–GPA7 (lado A / “right”) |
| KO0–KO6 | 30…24 | GPB0–GPB6 (lado B / “left”) |
| GND | 7 | GND |
| — | — | VCC → 3V3 do Pico |

| Pino do módulo | Ligar em | Por quê |
| --- | --- | --- |
| SDA / SI | Pico GP0 | I2C data |
| SCL / SCK | Pico GP1 | I2C clock |
| RESET | **3V3** | Ativo em LOW. Solto = chip some do barramento |
| A0, A1, A2 | **GND** | Endereço `0x20`. Soltos = endereço instável |
| INTA, INTB | nada | Não usamos interrupção |
| NC / S0, NC / CS | nada | Só existem no MCP23S17 (SPI) |
| GPB7 | nada | Livre |

A0–A2 em VCC mudam o endereço (`0x21`…`0x27`). Se mudar, ajuste `MCP_ADDR` em `firmware/pico/config.h`.

O módulo já tem pull-up de 10 kΩ no I2C (array `103`). RESET: algumas placas já puxam com 10 kΩ; ligue mesmo assim no 3V3, sem resistor extra em paralelo.

### Pico 2020

| Sinal | GPIO |
| --- | --- |
| SDA | GP0 |
| SCL | GP1 |
| EC11 oitava A / B | GP18 / GP19 |
| EC11 volume A / B | GP21 / GP22 |
| KY-023 VRx / VRy / SW | GP26 / GP27 / GP20 |
| KY-023 +5V | **3V3** (não use 5 V no Pico) |
| ZS-040 RXD | GP8 (TX) |
| ZS-040 TXD | GP9 (RX) |
| ZS-040 VCC | **VBUS (5 V)** — o silk pede 3,6–6 V |
| 3V3, GND | MCP + OLED + encoders + joystick (+ GND do ZS-040) |
| OLED SDA | GP0 (mesmo SDA do MCP) |
| OLED SCK | GP1 (silk **SCK** = SCL; mesmo clock do MCP) |

EC11: três pinos. O do meio (C) no GND; A e B nos GPIOs. `INPUT_PULLUP`, sem resistor extra.

O silk do KY-023 diz `+5V` (às vezes `VCC`), mas é só o VCC dos potenciômetros de ~10 kΩ. No Pico ligue em **3V3**. Se ligar em 5 V, VRx/VRy passam de 3,3 V e queimam o ADC. No boot o firmware calibra o centro — deixe o stick solto ao ligar. Eixo invertido: `JOY_INVERT_X` / `JOY_INVERT_Y` em `config.h`.

### OLED 0.91" (SSD1306 128×32)

Quatro pinos: **GND, VCC, SCK, SDA**. O silk `SCK` é o SCL do I2C — não é SPI. Entra no **mesmo barramento** do CJMCU-2317 (endereços diferentes: MCP `0x20`, OLED `0x3C`).

| OLED | Pico |
| --- | --- |
| GND | GND |
| VCC | **3V3** (não use 5 V) |
| SDA | GP0 |
| SCK | GP1 |

O módulo já traz pull-up. Sem fio extra de RESET. Se a sonda imprimir `OLED 0x3D`, mude `OLED_ADDR` em `firmware/pico/config.h`. Sem o display o firmware segue; só não atualiza a tela.

A tela mostra oitava, volume (barra), programa (botões 0–9) e a última nota. Clique do joystick = `S` (sustain).

## Firmware

Arduino IDE não é necessário no dia a dia. O **Lab** no browser conecta no Serial, mostra matriz / EC11 / joystick / OLED / Bluetooth ao vivo, manda comandos e grava o firmware.

```bash
./lab/run.sh
```

Abre `http://127.0.0.1:8741`. Escolha a porta `usbmodem` do Pico → **Conectar**.

| Botão | O que faz |
| --- | --- |
| **Gravar sonda** | Compila e envia `firmware/probe` (banco de testes) |
| **Gravar MIDI** | Compila e envia `firmware/pico` (USB-MIDI) |
| **Conectar** | Serial 115200 + stream ao vivo |
| AT / PROBE | Testa o HC-05 (`a` / `w`) |

A sonda precisa estar gravada uma vez. Se ainda não tiver `arduino-cli` (`brew install arduino-cli`), grave a sonda pela IDE só nessa primeira vez; o teste em si é no Lab.

Bibliotecas (IDE ou `arduino-cli lib install`): **Adafruit MCP23017**, **Adafruit SSD1306**, **Adafruit GFX Library**, **MIDI Library**. Placa **Raspberry Pi Pico** (Earle Philhower). MIDI: `USB Stack → Adafruit TinyUSB`. A sonda usa o USB padrão (CDC).

Sem o Lab: Serial 115200 na sonda. F3 → `KO0 KI0`. C6 → `KO3 KI7`. Depois grave `firmware/pico/pico.ino` — aparece **Casio SA-1 MIDI**.

### MIDI

| Controle | Mensagem |
| --- | --- |
| 32 teclas | Note On/Off F3–C6, oitava no EC11 1 |
| Botões 0–9 | Program Change em dois dígitos |
| stop / demo | All Notes Off |
| EC11 1 | oitava (−2…+3) |
| EC11 2 | volume CC 7 |
| Joystick X | Pitch Bend (centro = 0, com deadzone) |
| Joystick Y | CC 1 modulação (0–127) |
| Joystick SW | CC 64 sustain |

## ZS-040 (Bluetooth)

Módulo HC-05: porta serial clássica (SPP), não BLE-MIDI. A DAW não vê “Bluetooth MIDI” sozinha. USB-MIDI do Pico continua o caminho direto; o Bluetooth é um COM/serial extra.

| ZS-040 | Pico |
| --- | --- |
| VCC | **VBUS** (5 V no USB). Não use 3V3 — a placa pede 3,6–6 V |
| GND | GND |
| RXD | GP8 (TX). Lógica 3,3 V, sem divisor |
| TXD | GP9 (RX) |
| EN, STATE | soltos |

O firmware já espelha o MIDI no Serial1 a **9600** (padrão de fábrica do HC-05). PIN de pareamento costuma ser `1234`.

No computador, depois de parear:

1. [Hairless MIDI](https://projectgus.github.io/hairless-midiserial/) na mesma baud (9600).
2. Hairless → IAC (Mac) ou loopMIDI (Windows) → DAW.

Para 115200: entre em modo AT (EN no GND no reset, LED piscando lento), `AT+UART=115200,0,0`, e mude `BT_BAUD` em `config.h`. Sem módulo, deixe `MIRROR_SERIAL_MIDI = false`.

## Matriz (referência)

| | KI0 | KI1 | KI2 | KI3 | KI4 | KI5 | KI6 | KI7 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| **KO0** | F3 | F#3 | G3 | G#3 | A3 | A#3 | B3 | C4 |
| **KO1** | C#4 | D4 | D#4 | E4 | F4 | F#4 | G4 | G#4 |
| **KO2** | A4 | A#4 | B4 | C5 | C#5 | D5 | D#5 | E5 |
| **KO3** | F5 | F#5 | G5 | G#5 | A5 | A#5 | B5 | C6 |
| **KO4** | 0–4 | | | | | — | — | — |
| **KO5** | 5–9 | | | | | stop | — | — |
| **KO6** | — | | | | | demo | demo | demo |

Tempo ±, volume ± e rhythm da placa ignorados (KI5–7). Volume MIDI = EC11.

**O que não entra no mapa (confirmado em várias fontes):**

- **KO7** (toco 23): no SA-1 não varre tecla. Só detecta polifonia no boot (ligar 23↔18). [weltenschule](http://www.weltenschule.de/TableHooters/Casio_SA-1.html), [Bannister](https://forums.bannister.org/ubbthreads.php?ubb=showflat&Number=45295).
- **KO8–KO11** (pinos 22–19): NC no SA-1. Só GZ-5 / SA-65 / MA-120.
- **KO6 × KI0–4**: cópias dos botões 0–4, não funções novas. No SA-21 essa linha é pads de bateria; no SA-1 (`M6387-01`) não.
- Weltenschule escreve `B5` em KO2 KI2; entre A#4 e C5 é **B4** (já está assim no firmware).

As 32 notas estão só em KO0–KO3 × KI0–KI7 (grupos de 8). Sem KI5–7 não há A#3 B3 C4 / F#4 G4 G#4 / D5 D#5 E5 / A#5 B5 C6 — não existe outro sítio na placa.

[Hardware SA-1](http://www.weltenschule.de/TableHooters/Casio_SA-1.html). Sem diodos: acorde de 3+ notas no mesmo retângulo pode fantasma.

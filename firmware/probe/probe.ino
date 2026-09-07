#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_MCP23X17.h>
#include "periph.h"

// Sonda: matriz KO/KI + EC11 + joystick + OLED.
// Serial 115200. Comandos: h l r c m n p

static const uint8_t LED_PIN = 25;
static const uint8_t KO_COUNT = 7;
static const uint8_t KI_COUNT = 8;

static const uint8_t BUSSES[][2] = {
    {0, 1},
    {4, 5},
    {2, 3},
};

// Cobre Casio (weltenschule) — só para conferir solda, não é o MIDI.
static const char *const CASIO_NAMES[7][8] = {
    {"F3", "F#3", "G3", "G#3", "A3", "A#3", "B3", "C4"},
    {"C#4", "D4", "D#4", "E4", "F4", "F#4", "G4", "G#4"},
    {"A4", "A#4", "B4", "C5", "C#5", "D5", "D#5", "E5"},
    {"F5", "F#5", "G5", "G#5", "A5", "A#5", "B5", "C6"},
    {"0", "1", "2", "3", "4", "tempo+", "vol+", "sel"},
    {"5", "6", "7", "8", "9", "stop", "tempo-", "vol-"},
    {"-", "-", "-", "-", "-", "demo", "demo", "demo"},
};

// Mapa aprendido = firmware/pico/matrix.h (esta unidade, 2026-08-31).
static const char *const NAMES[7][8] = {
    {"F3", "F#3", "G3", "G#3", "A3", "A#3", "B3", "C4"},
    {"A5", "G#5", "G5", "F#5", "F5", "A#5", "B5", "C6"},
    {"C#5", "C5", "B4", "A#4", "A4", "D5", "D#5", "E5"},
    {"F4", "E4", "D#4", "D4", "C#4", "F#4", "G4", "G#4"},
    {"0", "1", "2", "3", "4", "-", "-", "-"},
    {"5", "6", "7", "8", "9", "stop", "-", "-"},
    {"-", "-", "-", "-", "-", "demo", "-", "-"},
};

static const char *const LEARN[] = {
    "F3",  "F#3", "G3",  "G#3", "A3",  "A#3", "B3",  "C4",
    "C#4", "D4",  "D#4", "E4",  "F4",  "F#4", "G4",  "G#4",
    "A4",  "A#4", "B4",  "C5",  "C#5", "D5",  "D#5", "E5",
    "F5",  "F#5", "G5",  "G#5", "A5",  "A#5", "B5",  "C6",
    "tempo-", "tempo+", "sel",
    "0",   "1",   "2",   "3",   "4",   "5",   "6",   "7",
    "8",   "9",   "stop",
};
static const uint8_t LEARN_COUNT = sizeof(LEARN) / sizeof(LEARN[0]);

Adafruit_MCP23X17 mcp;
static bool held[7][8];
static bool seen[7][8];
static bool stuck[7][8];
static uint8_t foundAddr = 0;
static uint8_t foundSda = 0;
static uint8_t foundScl = 1;
static bool mcpOk = false;

static bool learnOn = false;
static bool learnWaitUp = false;
static bool learnPick = false;
static uint8_t learnStep = 0;
static int8_t learnedKo[LEARN_COUNT][4];
static int8_t learnedKi[LEARN_COUNT][4];
static uint8_t learnedN[LEARN_COUNT];

static uint8_t scanOnce() {
  uint8_t first = 0;
  for (uint8_t addr = 0x20; addr <= 0x27; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() != 0) {
      continue;
    }
    Serial.print(F("  0x"));
    Serial.println(addr, HEX);
    if (first == 0) {
      first = addr;
    }
  }
  return first;
}

static bool findMcp() {
  for (uint8_t i = 0; i < 3; i++) {
    const uint8_t sda = BUSSES[i][0];
    const uint8_t scl = BUSSES[i][1];
    Wire.end();
    pinMode(sda, INPUT_PULLUP);
    pinMode(scl, INPUT_PULLUP);
    delay(5);
    Wire.setSDA(sda);
    Wire.setSCL(scl);
    Wire.begin();
    Wire.setClock(100000);
    delay(20);

    Serial.print(F("Scan SDA=GP"));
    Serial.print(sda);
    Serial.print(F(" SCL=GP"));
    Serial.println(scl);
    const uint8_t addr = scanOnce();
    if (addr == 0) {
      Serial.println(F("  nada"));
      continue;
    }
    if (mcp.begin_I2C(addr, &Wire)) {
      foundAddr = addr;
      foundSda = sda;
      foundScl = scl;
      return true;
    }
  }
  return false;
}

static const char *nameAt(uint8_t row, uint8_t col) {
  return NAMES[row][col];
}

static void printKoKi(uint8_t row, uint8_t col) {
  Serial.print(F("KO"));
  Serial.print(row);
  Serial.print(F(" KI"));
  Serial.print(col);
}

static void printHypotheses(uint8_t row, uint8_t col) {
  const uint8_t kiRev = static_cast<uint8_t>(7 - col);
  const uint8_t koRev = static_cast<uint8_t>(6 - row);
  Serial.print(F("  mapa:"));
  Serial.print(nameAt(row, col));
  Serial.print(F("  Casio:"));
  Serial.print(CASIO_NAMES[row][col]);
  Serial.print(F("  KIrev:"));
  Serial.print(CASIO_NAMES[row][kiRev]);
  Serial.print(F("  KOrev:"));
  Serial.println(CASIO_NAMES[koRev][col]);
}

static void printHeld() {
  Serial.print(F("  held"));
  bool any = false;
  for (uint8_t row = 0; row < KO_COUNT; row++) {
    for (uint8_t col = 0; col < KI_COUNT; col++) {
      if (!held[row][col]) {
        continue;
      }
      any = true;
      Serial.print(' ');
      printKoKi(row, col);
      if (stuck[row][col]) {
        Serial.print(F("*"));
      }
    }
  }
  if (!any) {
    Serial.print(F(" (nenhuma)"));
  }
  Serial.println();
}

static void printMap() {
  Serial.println(F("mapa  KI0 KI1 KI2 KI3 KI4 KI5 KI6 KI7   #visto  Scolado"));
  for (uint8_t row = 0; row < KO_COUNT; row++) {
    Serial.print(F("KO"));
    Serial.print(row);
    Serial.print(' ');
    for (uint8_t col = 0; col < KI_COUNT; col++) {
      if (stuck[row][col]) {
        Serial.print(F("  S"));
      } else if (seen[row][col]) {
        Serial.print(F("  #"));
      } else {
        Serial.print(F("  ."));
      }
    }
    Serial.println();
  }
  Serial.println(F("mapa MCP: KO0=F3..C4  KO1=A5..F5+A#5..C6  KO2=C#5..A4+D5..E5  KO3=F4..C#4+F#4..G#4"));
  Serial.println(F("KO1-3 serpentina (KI0-4 invertidos vs Casio). vol da placa = EC11."));
}

static void printMapSummary() {
  uint8_t pianoSeen = 0;
  uint8_t btnSeen = 0;
  for (uint8_t row = 0; row < KO_COUNT; row++) {
    for (uint8_t col = 0; col < KI_COUNT; col++) {
      if (stuck[row][col] || !seen[row][col]) {
        continue;
      }
      if (row <= 3) {
        pianoSeen++;
      } else if (col <= 4) {
        btnSeen++;
      }
    }
  }
  Serial.print(F("resumo  piano "));
  Serial.print(pianoSeen);
  Serial.print(F("/32  botoes "));
  Serial.print(btnSeen);
  Serial.print(F("/12  colados"));
  bool anyStuck = false;
  for (uint8_t row = 0; row < KO_COUNT; row++) {
    for (uint8_t col = 0; col < KI_COUNT; col++) {
      if (!stuck[row][col]) {
        continue;
      }
      if (!anyStuck) {
        Serial.print(':');
        anyStuck = true;
      } else {
        Serial.print(',');
      }
      printKoKi(row, col);
    }
  }
  if (!anyStuck) {
    Serial.print(F(": nenhum"));
  }
  Serial.println(F("  (M=mapa completo)"));
}

static void printGpaRaw() {
  Serial.println(F("GPA cru (bit0=KI0 .. bit7=KI7). 0=LOW tecla"));
  for (uint8_t row = 0; row < KO_COUNT; row++) {
    mcp.writeGPIO(static_cast<uint8_t>(~(1 << row)) & 0x7F, 1);
    delayMicroseconds(40);
    const uint8_t cols = mcp.readGPIO(0);
    Serial.print(F("KO"));
    Serial.print(row);
    Serial.print(F("  0x"));
    if (cols < 0x10) {
      Serial.print('0');
    }
    Serial.print(cols, HEX);
    Serial.print(F("  "));
    for (int8_t bit = 7; bit >= 0; bit--) {
      Serial.print((cols & (1 << bit)) ? '1' : '0');
    }
    Serial.println();
  }
  mcp.writeGPIO(0x7F, 1);
  Serial.println(F("teste: jumper GPA5->GND deve pôr bit5=0 em TODAS as linhas KO"));
}

static void printJumperHelp() {
  Serial.println(F("--- teste B5/B6/B7 (MCP vs Casio) ---"));
  Serial.println(F("1. Desliga cambos dos tocos 16 17 18 da Casio."));
  Serial.println(F("2. Jumper fio GPA de B5/A5 -> GND. Envia 'g'."));
  Serial.println(F("   Certo: bit5=0 em KO0..KO6. Errado: bit5 sempre 1 -> furo GPA errado."));
  Serial.println(F("3. Repete GPA de B6/A6 (bit6) e B7/A7 (bit7)."));
  Serial.println(F("4. Reconecta toco 16 no GPA de B5. Multimetro: 16 nao apita com 25 (GPB)."));
  Serial.println(F("5. A#3: 30<->16. C4: 30<->18. C6: 27<->18."));
  Serial.println(F("--- fim ---"));
}

static void printHelp() {
  Serial.println(F("h ajuda | l aprender | f finalizar | z reiniciar | b voltar | n saltar | r sair"));
  Serial.println(F("w <tecla>  ir para essa tecla no aprender (ex. w F#3 | w stop | w tempo+ | w sel)"));
  Serial.println(F("g GPA | t jumper | m resumo | M mapa | c limpar visto"));
  Serial.println(F("p perifericos | u UI json | U para"));
  Serial.println(F("L <0-7|a|m|r> <rrggbb|0-255> [alpha]  — WS2812  (L r 1|0 rainbow)"));
  Serial.println(F("A m 0|1|2|3  mute|tom|teclas|clock  | A v 0-127 | A t Hz | A c verifica I2S"));
  Serial.println(F("j c recentrar | j x | j y | j s | j e | j g | j m xmin xmax ymin ymax | j 0"));
  Serial.println(F("e  trocar posicao EC11 2|1 no painel (1=oitava GP18/19  2=volume GP21/22)"));
  Serial.println(F("DOWN = KO/KI + nome aprendido. Gire EC11, mexa stick, L r 1, A m 1."));
}

static void publishMatrixMasks() {
  uint8_t h[7] = {};
  uint8_t s[7] = {};
  for (uint8_t row = 0; row < KO_COUNT; row++) {
    for (uint8_t col = 0; col < KI_COUNT; col++) {
      if (held[row][col]) {
        h[row] |= static_cast<uint8_t>(1u << col);
      }
      if (stuck[row][col]) {
        s[row] |= static_cast<uint8_t>(1u << col);
      }
    }
  }
  periphSetMatrixMasks(h, s);
}

static void checkKoPairs() {
  for (uint8_t col = 0; col < KI_COUNT; col++) {
    const bool pair = held[0][col] && held[4][col];
    if (pair) {
      periphWarnKoPair(col);
    } else {
      periphClearKoPairWarn(col);
    }
  }
}

static void readSerialLine(char *buf, size_t cap) {
  size_t n = 0;
      const uint32_t start = millis();
  while (n + 1 < cap) {
    if (!Serial.available()) {
      if (millis() - start > 300) {
        break;
      }
      delay(1);
      continue;
    }
    const char c = static_cast<char>(Serial.read());
    if (c == '\n' || c == '\r') {
      break;
    }
    buf[n++] = c;
  }
  buf[n] = '\0';
}

static bool anyHeldLive() {
  for (uint8_t row = 0; row < KO_COUNT; row++) {
    for (uint8_t col = 0; col < KI_COUNT; col++) {
      if (held[row][col] && !stuck[row][col]) {
        return true;
      }
    }
  }
  return false;
}

static uint8_t collectLiveHeld(uint8_t *rows, uint8_t *cols, uint8_t maxN) {
  uint8_t n = 0;
  for (uint8_t row = 0; row < KO_COUNT; row++) {
    for (uint8_t col = 0; col < KI_COUNT; col++) {
      if (!held[row][col] || stuck[row][col] || n >= maxN) {
        continue;
      }
      rows[n] = row;
      cols[n] = col;
      n++;
    }
  }
  return n;
}

static bool learnNameEq(const char *a, const char *b) {
  while (*a && *b) {
    char ca = *a++;
    char cb = *b++;
    if (ca >= 'a' && ca <= 'z') {
      ca = static_cast<char>(ca - 32);
    }
    if (cb >= 'a' && cb <= 'z') {
      cb = static_cast<char>(cb - 32);
    }
    if (ca != cb) {
      return false;
    }
  }
  return *a == *b;
}

static void emitLearnJson(const char *st, const char *name, int8_t ko, int8_t ki) {
  const char *nm = name;
  if (nm == nullptr) {
    nm = (learnOn && learnStep < LEARN_COUNT) ? LEARN[learnStep] : "";
  }
  Serial.print(F("J{\"t\":\"l\",\"on\":"));
  Serial.print(learnOn ? 1 : 0);
  Serial.print(F(",\"i\":"));
  Serial.print(learnStep);
  Serial.print(F(",\"tot\":"));
  Serial.print(LEARN_COUNT);
  Serial.print(F(",\"n\":\""));
  Serial.print(nm);
  Serial.print(F("\",\"w\":"));
  Serial.print(learnWaitUp ? 1 : 0);
  Serial.print(F(",\"st\":\""));
  Serial.print(st);
  Serial.print('"');
  if (ko >= 0) {
    Serial.print(F(",\"ko\":"));
    Serial.print(ko);
    Serial.print(F(",\"ki\":"));
    Serial.print(ki);
  }
  Serial.println('}');
}

static void printLearnPrompt() {
  Serial.print(F("APERTA sozinha  ["));
  Serial.print(learnStep + 1);
  Serial.print('/');
  Serial.print(LEARN_COUNT);
  Serial.print(F("]  "));
  Serial.print(LEARN[learnStep]);
  Serial.println(F("   (n=saltar  b=voltar  f=finalizar)"));
  emitLearnJson("wait", LEARN[learnStep], -1, -1);
}

static void printMatrixH();

static void printLearned() {
  Serial.println(F("--- aprendido (cola isto) ---"));
  for (uint8_t i = 0; i < LEARN_COUNT; i++) {
    Serial.print(LEARN[i]);
    Serial.print('\t');
    if (learnedN[i] == 0) {
      Serial.println(F("?"));
      continue;
    }
    for (uint8_t k = 0; k < learnedN[i] && k < 4; k++) {
      if (k) {
        Serial.print(F(" + "));
      }
      printKoKi(static_cast<uint8_t>(learnedKo[i][k]),
                static_cast<uint8_t>(learnedKi[i][k]));
    }
    if (learnedN[i] > 1) {
      Serial.print(F("  << varias celulas"));
    }
    Serial.println();
  }
  Serial.println(F("--- fim ---"));
  printMatrixH();
}

static uint8_t learnIndexToCode(uint8_t i) {
  if (i < 32) {
    return static_cast<uint8_t>(53 + i);
  }
  if (i == 32) {
    return 211; // tempo-
  }
  if (i == 33) {
    return 210; // tempo+
  }
  if (i == 34) {
    return 212; // sel
  }
  if (i >= 35 && i <= 44) {
    return static_cast<uint8_t>(200 + (i - 35)); // 0..9
  }
  if (i == 45) {
    return 215; // stop
  }
  return 255;
}

static void printMatrixCell(uint8_t code) {
  if (code == 255) {
    Serial.print(F("CELL_EMPTY"));
    return;
  }
  Serial.print(code);
}

// Gera matrix.h a partir do aprendido — endereco fisico KO/KI -> nota MIDI.
static void printMatrixH() {
  uint8_t grid[7][8];
  for (uint8_t row = 0; row < KO_COUNT; row++) {
    for (uint8_t col = 0; col < KI_COUNT; col++) {
      grid[row][col] = 255;
    }
  }

  Serial.println(F("--- matrix.h (copia para firmware/pico/matrix.h) ---"));
  Serial.println(F("// Gerado pela sonda. Cada celula = onde o hardware leu a tecla."));
  Serial.println(F("// CELL_EMPTY = nao aprendido — teste com DOWN ou n no aprender."));

  for (uint8_t i = 0; i < LEARN_COUNT; i++) {
    if (learnedN[i] == 0) {
      continue;
    }
    if (learnedN[i] > 1) {
      Serial.print(F("// AVISO "));
      Serial.print(LEARN[i]);
      Serial.println(F(" varias celulas — usada a primeira"));
    }
    const uint8_t ko = static_cast<uint8_t>(learnedKo[i][0]);
    const uint8_t ki = static_cast<uint8_t>(learnedKi[i][0]);
    const uint8_t code = learnIndexToCode(i);
    if (grid[ko][ki] != 255 && grid[ko][ki] != code) {
      Serial.print(F("// CONFLITO KO"));
      Serial.print(ko);
      Serial.print(F(" KI"));
      Serial.print(ki);
      Serial.print(F(" ja tinha "));
      Serial.print(grid[ko][ki]);
      Serial.print(F(", sobrescreve com "));
      Serial.println(code);
    }
    grid[ko][ki] = code;
  }

  Serial.println(F("static const uint8_t MATRIX[7][8] = {"));
  for (uint8_t row = 0; row < KO_COUNT; row++) {
    Serial.print(F("    {"));
    for (uint8_t col = 0; col < KI_COUNT; col++) {
      if (col) {
        Serial.print(F(", "));
      }
      printMatrixCell(grid[row][col]);
    }
    Serial.print(F("}"));
    if (row + 1 < KO_COUNT) {
      Serial.println(F(","));
    } else {
      Serial.println();
    }
  }
  Serial.println(F("};"));
  Serial.println(F("--- fim matrix.h ---"));
  Serial.println(F("Se faltam notas: a tecla pode estar noutro KO/KI — grave com aprender ou"));
  Serial.println(F("teste DOWN e preencha a celula. MCP_KO_REV/KI_REV em config.h se cabo invertido."));
}

static void learnStart() {
  learnOn = true;
  learnPick = false;
  learnWaitUp = anyHeldLive();
  learnStep = 0;
  memset(learnedN, 0, sizeof(learnedN));
  for (uint8_t i = 0; i < LEARN_COUNT; i++) {
    for (uint8_t k = 0; k < 4; k++) {
      learnedKo[i][k] = -1;
      learnedKi[i][k] = -1;
    }
  }
  Serial.println(F("APRENDER: uma tecla de cada vez, largar antes da seguinte."));
  Serial.println(F("32 teclas F3..C6, depois tempo-/+ e sel, 0-9 e stop. Sem PWR/vol/demo."));
  emitLearnJson("start", LEARN[0], -1, -1);
  printLearnPrompt();
}

static void learnRecord(const uint8_t *rows, const uint8_t *cols, uint8_t n) {
  const uint8_t step = learnStep;
  learnedN[step] = n;
  for (uint8_t k = 0; k < n && k < 4; k++) {
    learnedKo[step][k] = static_cast<int8_t>(rows[k]);
    learnedKi[step][k] = static_cast<int8_t>(cols[k]);
  }
  Serial.print(F("GRAVOU  "));
  Serial.print(LEARN[step]);
  Serial.print(F("  =  "));
  for (uint8_t k = 0; k < n && k < 4; k++) {
    if (k) {
      Serial.print(F(" + "));
    }
    printKoKi(rows[k], cols[k]);
  }
  if (n > 1) {
    Serial.print(F("  << nao e 1:1"));
  }
  Serial.println();
  learnWaitUp = true;
  emitLearnJson("ok", LEARN[step], static_cast<int8_t>(rows[0]),
                static_cast<int8_t>(cols[0]));
  if (learnPick) {
    Serial.println(F("--- gravou esta tecla. clica outra no Lab ou n=seguinte ---"));
    return;
  }
  learnStep++;
  if (learnStep >= LEARN_COUNT) {
    learnOn = false;
    printLearned();
    Serial.println(F("aprender completo. l outra vez ou manda o bloco GRAVOU."));
    emitLearnJson("done", "", -1, -1);
    return;
  }
  Serial.println(F("--- solta tudo; aguarda mensagem 'soltou' antes da proxima ---"));
}

static void learnSkip() {
  if (!learnOn) {
    Serial.println(F("nao em aprender — l para comecar, depois n para saltar"));
    return;
  }
  Serial.print(F("SALTOU  "));
  Serial.println(LEARN[learnStep]);
  emitLearnJson("skip", LEARN[learnStep], -1, -1);
  learnedN[learnStep] = 0;
  learnStep++;
  learnWaitUp = anyHeldLive();
  if (learnStep >= LEARN_COUNT) {
    learnOn = false;
    printLearned();
    emitLearnJson("done", "", -1, -1);
    return;
  }
  printLearnPrompt();
}

static void learnBack() {
  if (learnStep == 0 && !learnOn) {
    Serial.println(F("nada a voltar — envia l para comecar"));
    return;
  }
  if (learnStep == 0) {
    Serial.print(F("ja no passo 1/"));
    Serial.println(LEARN_COUNT);
    printLearnPrompt();
    return;
  }
  learnOn = true;
  learnStep--;
  learnedN[learnStep] = 0;
  for (uint8_t k = 0; k < 4; k++) {
    learnedKo[learnStep][k] = -1;
    learnedKi[learnStep][k] = -1;
  }
  learnWaitUp = anyHeldLive();
  Serial.print(F("VOLTOU  refazer  "));
  Serial.println(LEARN[learnStep]);
  emitLearnJson("back", LEARN[learnStep], -1, -1);
  printLearnPrompt();
}

static void learnFinish() {
  if (learnStep == 0 && !learnOn) {
    Serial.println(F("nada gravado — envia l para comecar"));
    return;
  }
  learnOn = false;
  Serial.println(F("--- finalizado (parcial ok) ---"));
  printLearned();
  Serial.println(F("? = saltado ou nao testado. m=ver de novo."));
  emitLearnJson("done", "", -1, -1);
}

static int8_t learnFind(const char *s) {
  while (*s == ' ') {
    s++;
  }
  if (*s == '\0') {
    return -1;
  }
  // Alias UI / serial sem +/− especiais.
  if (learnNameEq(s, "t-") || learnNameEq(s, "tdn") || learnNameEq(s, "tempo_dn") ||
      learnNameEq(s, "tempodn")) {
    s = "tempo-";
  } else if (learnNameEq(s, "t+") || learnNameEq(s, "tup") || learnNameEq(s, "tempo_up") ||
             learnNameEq(s, "tempoup")) {
    s = "tempo+";
  } else if (learnNameEq(s, "select") || learnNameEq(s, "rhythm")) {
    s = "sel";
  }
  for (uint8_t i = 0; i < LEARN_COUNT; i++) {
    if (learnNameEq(s, LEARN[i])) {
      return static_cast<int8_t>(i);
    }
  }
  bool digits = true;
  for (const char *p = s; *p; p++) {
    if (*p < '0' || *p > '9') {
      digits = false;
      break;
    }
  }
  if (digits) {
    const int n = atoi(s);
    if (n >= 1 && n <= LEARN_COUNT) {
      return static_cast<int8_t>(n - 1);
    }
  }
  return -1;
}

static bool needMcp();

static void learnGoto(char *line) {
  if (!needMcp()) {
    return;
  }
  const int8_t i = learnFind(line);
  if (i < 0) {
    Serial.println(F("w  tecla desconhecida (ex. w F#3 | w 0 | w stop | w tempo+ | w sel)"));
    return;
  }
  learnOn = true;
  learnPick = true;
  learnStep = static_cast<uint8_t>(i);
  learnedN[learnStep] = 0;
  for (uint8_t k = 0; k < 4; k++) {
    learnedKo[learnStep][k] = -1;
    learnedKi[learnStep][k] = -1;
  }
  Serial.print(F("IR  "));
  Serial.println(LEARN[learnStep]);
  uint8_t rows[8];
  uint8_t cols[8];
  const uint8_t n = collectLiveHeld(rows, cols, 8);
  if (n > 0) {
    learnRecord(rows, cols, n);
    return;
  }
  learnWaitUp = false;
  printLearnPrompt();
}

static void markStuck() {
  uint8_t votes[7][8] = {};
  for (uint8_t n = 0; n < 20; n++) {
    for (uint8_t row = 0; row < KO_COUNT; row++) {
      mcp.writeGPIO(static_cast<uint8_t>(~(1 << row)) & 0x7F, 1);
      delayMicroseconds(40);
      const uint8_t cols = mcp.readGPIO(0);
      for (uint8_t col = 0; col < KI_COUNT; col++) {
        if ((cols & (1 << col)) == 0) {
          votes[row][col]++;
        }
      }
    }
    mcp.writeGPIO(0x7F, 1);
    delay(8);
  }
  Serial.println(F("colados no arranque (ignorados no aprender):"));
  bool any = false;
  for (uint8_t row = 0; row < KO_COUNT; row++) {
    for (uint8_t col = 0; col < KI_COUNT; col++) {
      if (votes[row][col] < 15) {
        continue;
      }
      stuck[row][col] = true;
      held[row][col] = true;
      seen[row][col] = true;
      any = true;
      Serial.print(F("  S  "));
      printKoKi(row, col);
      Serial.print(F("  "));
      Serial.println(nameAt(row, col));
    }
  }
  if (!any) {
    Serial.println(F("  nenhum"));
  }
}

static bool needMcp() {
  if (mcpOk) {
    return true;
  }
  Serial.println(F("sem MCP — so perifericos"));
  return false;
}

static void handleSerial() {
  while (Serial.available()) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\r' || c == '\n' || c == ' ') {
      continue;
    }
    if (c == 'L') {
      char line[48];
      readSerialLine(line, sizeof(line));
      handleLedCommand(line);
      continue;
    }
    if (c == 'A') {
      char line[32];
      readSerialLine(line, sizeof(line));
      handleAudioCommand(line);
      continue;
    }
    if (c == 'j' || c == 'J') {
      const uint32_t wait = millis();
      while (!Serial.available() && (millis() - wait) < 300) {
        delay(1);
      }
      char line[24];
      readSerialLine(line, sizeof(line));
      handleJoyCommand(line);
      continue;
    }
    if (c == 'w' || c == 'W') {
      char line[24];
      readSerialLine(line, sizeof(line));
      learnGoto(line);
      continue;
    }
    switch (c) {
    case 'h':
    case 'H':
    case '?':
      printHelp();
      break;
    case 'l':
      if (!needMcp()) {
        break;
      }
      if (learnOn) {
        Serial.print(F("ja em aprender passo "));
        Serial.print(learnStep + 1);
        Serial.print(F("/"));
        Serial.print(LEARN_COUNT);
        Serial.println(F(". z=reiniciar | b=voltar | r=sair"));
        printLearnPrompt();
        break;
      }
      learnStart();
      break;
    case 'z':
    case 'Z':
      if (!needMcp()) {
        break;
      }
      if (learnOn) {
        Serial.println(F("reiniciando aprender do passo 1..."));
      }
      learnStart();
      break;
    case 'e':
    case 'E':
      encSwap = !encSwap;
      Serial.print(F("EC11  swap="));
      Serial.print(encSwap ? 1 : 0);
      Serial.println(encSwap
                         ? F("  → esquerda=EC11-2 GP21/22  direita=EC11-1 GP18/19")
                         : F("  → esquerda=EC11-1 GP18/19  direita=EC11-2 GP21/22"));
      Serial.print(F("EC11-1  count="));
      Serial.println(enc1.count);
      Serial.print(F("EC11-2  count="));
      Serial.println(enc2.count);
      emitJsonState();
      break;
    case 'n':
    case 'N':
      learnSkip();
      break;
    case 'b':
    case 'B':
      learnBack();
      break;
    case 'f':
    case 'F':
      learnFinish();
      break;
    case 'r':
    case 'R':
      learnOn = false;
      Serial.println(F("modo cru"));
      emitLearnJson("stop", "", -1, -1);
      break;
    case 'c':
    case 'C':
      if (!needMcp()) {
        break;
      }
      memset(seen, 0, sizeof(seen));
      for (uint8_t row = 0; row < KO_COUNT; row++) {
        for (uint8_t col = 0; col < KI_COUNT; col++) {
          if (stuck[row][col]) {
            seen[row][col] = true;
          }
        }
      }
      Serial.println(F("visto limpo (S mantidos)"));
      printMapSummary();
      break;
    case 'm':
      if (!needMcp()) {
        break;
      }
      printMapSummary();
      if (learnStep > 0) {
        printLearned();
      }
      break;
    case 'M':
      if (!needMcp()) {
        break;
      }
      printMap();
      if (learnStep > 0) {
        printLearned();
      }
      break;
    case 'g':
    case 'G':
      if (!needMcp()) {
        break;
      }
      printGpaRaw();
      break;
    case 't':
    case 'T':
      printJumperHelp();
      break;
    case 'p':
    case 'P':
      printPeriphStatus();
      break;
    case 'u':
      periphSetUi(true);
      break;
    case 'U':
      periphSetUi(false);
      break;
    default:
      break;
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(800);
  pinMode(LED_PIN, OUTPUT);

  Serial.println(F("=== sonda hardware (matriz + EC11 + joy + OLED + PCM5102) ==="));
  mcpOk = findMcp();
  if (!mcpOk) {
    Serial.println(F("MCP NAO ENCONTRADO em GP0/1, GP4/5, GP2/3"));
    Serial.println(F("VCC=3V3  GND  RESET=3V3  A0-A2=GND  — matriz desligada, resto segue"));
    Wire.end();
    Wire.setSDA(0);
    Wire.setSCL(1);
    Wire.begin();
  } else {
    Serial.print(F("MCP OK  0x"));
    Serial.print(foundAddr, HEX);
    Serial.print(F("  SDA=GP"));
    Serial.print(foundSda);
    Serial.print(F("  SCL=GP"));
    Serial.println(foundScl);
    for (uint8_t i = 0; i < KI_COUNT; i++) {
      mcp.pinMode(i, INPUT_PULLUP);
    }
    for (uint8_t i = 0; i < KO_COUNT; i++) {
      mcp.pinMode(8 + i, OUTPUT);
      mcp.digitalWrite(8 + i, HIGH);
    }
  }
  Wire.setClock(400000);

  digitalWrite(LED_PIN, HIGH);
  setupPeripherals(mcpOk);
  if (mcpOk) {
    markStuck();
  }
  publishMatrixMasks();
  printHelp();
}

void loop() {
  handleSerial();
  pollPeripherals();

  if (!mcpOk) {
    digitalWrite(LED_PIN, (millis() / 80) % 2);
    return;
  }

  uint8_t downRows[8];
  uint8_t downCols[8];
  uint8_t nDown = 0;
  bool changed = false;

  for (uint8_t row = 0; row < KO_COUNT; row++) {
    mcp.writeGPIO(static_cast<uint8_t>(~(1 << row)) & 0x7F, 1);
    delayMicroseconds(20);
    const uint8_t cols = mcp.readGPIO(0);
    for (uint8_t col = 0; col < KI_COUNT; col++) {
      const bool pressed = (cols & (1 << col)) == 0;
      if (pressed == held[row][col]) {
        continue;
      }
      held[row][col] = pressed;
      changed = true;
      if (pressed) {
        seen[row][col] = true;
      }

      Serial.print(pressed ? F("DOWN  ") : F("UP    "));
      printKoKi(row, col);
      if (stuck[row][col]) {
        Serial.print(F("  S"));
      }
      printHypotheses(row, col);
      periphNoteMatrix(row, col, pressed, nameAt(row, col));

      if (pressed && !stuck[row][col] && nDown < 8) {
        downRows[nDown] = row;
        downCols[nDown] = col;
        nDown++;
      }
    }
  }
  mcp.writeGPIO(0x7F, 1);

  publishMatrixMasks();
  checkKoPairs();

  if (changed) {
    printHeld();
  }

  // Limpar learnWaitUp no UP (antes do return por nDown==0).
  if (learnOn && learnWaitUp && !anyHeldLive()) {
    learnWaitUp = false;
    Serial.println(F("--- soltou: pode apertar a proxima tecla ---"));
    if (learnPick && learnedN[learnStep] > 0) {
      emitLearnJson("ok", LEARN[learnStep], learnedKo[learnStep][0],
                    learnedKi[learnStep][0]);
    } else {
      printLearnPrompt();
    }
  }

  if (!learnOn || nDown == 0) {
    return;
  }
  if (learnWaitUp) {
    return;
  }
  learnRecord(downRows, downCols, nDown);
}

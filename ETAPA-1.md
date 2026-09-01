# Etapa 1 — Jump da matriz SA-1 → CJMCU-2317

Nesta etapa **não** ligue encoders, joystick nem Bluetooth. Só: isolar o `M6387`, pular os 15 fios da matriz no MCP, e confirmar com a sonda.

Peças agora: placa `M3210-MAIM(F)`, CJMCU-2317, Pico 2020, fios, multímetro.

## Esquemas

### Verso do CJMCU-2317 — 3 fileiras (cima)

```mermaid
flowchart TB
  subgraph MCP["CJMCU-2317 verso"]
    direction TB

    subgraph F1["fileira 1"]
      direction LR
      L1["esq  A2<br/>Pico GND"]
      R1["dir  VCC/GND<br/>M6387 pino 7"]
      L1 --- R1
    end

    subgraph F2["fileira 2"]
      direction LR
      L2["esq  A1<br/>Pico GND"]
      R2["dir  ITB/ITA<br/>solto"]
      L2 --- R2
    end

    subgraph F3["fileira 3"]
      direction LR
      L3["esq  A0  endereço<br/>Pico GND"]
      R3["dir  B0/A0<br/>GPA0 toco 11 · GPB0 toco 30"]
      L3 --- R3
    end
  end
```

| Fileira | Esquerda | Direita |
| --- | --- | --- |
| 1 | A2 → GND | VCC/GND → pino 7 |
| 2 | A1 → GND | ITB/ITA → solto |
| 3 | A0 endereço → GND | B0/A0 → 11 / 30 |

`A0` da esquerda **não** é GPA0. Em `B0/A0`, furo perto do texto = GPA; o outro = GPB.

### Bloco — etapa 1

```mermaid
flowchart LR
  subgraph SA1["SA-1 M3210-MAIM"]
    RUB["Borracha condutiva<br/>32 teclas + botões"]
    PADS["Tocos LSI1<br/>M6387 removido"]
    RUB --- PADS
  end

  subgraph MCP["CJMCU-2317"]
    GPA["GPA0–GPA7<br/>entradas + pull-up"]
    GPB["GPB0–GPB6<br/>saídas ativa LOW"]
    I2C["SDA SCL"]
  end

  subgraph PICO["Pico 2020"]
    USB["USB Serial 115200<br/>sonda"]
    I2CP["GP0 SDA / GP1 SCL"]
    P3V["3V3 + GND"]
  end

  PADS -->|"KI0–KI7"| GPA
  PADS -->|"KO0–KO6"| GPB
  PADS -->|"pino 7"| P3V
  GPA --- I2C
  GPB --- I2C
  I2C --- I2CP
  P3V --- MCP
  I2CP --- USB
```

### Footprint LSI1 — pino 1

Chanfro / seta da silk = pino 1. Esquerda 1→15 (para baixo), direita 30→16 (para baixo).

```mermaid
flowchart TB
  subgraph DIP["LSI1  M6387-01B  30 pinos SDIL"]
    direction LR
    subgraph LEFT["fileira pinos 1–15"]
      direction TB
      P1["1 TEST"]
      P7["7 GND ★"]
      P10["10 VDD"]
      P11["11 KI0 ★"]
      P12["12 KI1 ★"]
      P13["13 KI2 ★"]
      P14["14 KI3 ★"]
      P15["15 KI4 ★"]
      P1 --- P7 --- P10 --- P11 --- P12 --- P13 --- P14 --- P15
    end
    subgraph RIGHT["fileira pinos 30–16"]
      direction TB
      P30["30 KO0 ★"]
      P29["29 KO1 ★"]
      P28["28 KO2 ★"]
      P27["27 KO3 ★"]
      P26["26 KO4 ★"]
      P25["25 KO5 ★"]
      P24["24 KO6 ★"]
      P23["23 NC"]
      P18["18 KI7 ★"]
      P17["17 KI6 ★"]
      P16["16 KI5 ★"]
      P30 --- P29 --- P28 --- P27 --- P26 --- P25 --- P24 --- P23 --- P18 --- P17 --- P16
    end
  end
```

★ = soldar cambo. Pinos 16–18 estão no canto inferior direito (depois do 15, a contagem sobe pelo outro lado: 16, 17, 18).

### Jump — matriz + I2C

```mermaid
flowchart LR
  subgraph TOCOS["Tocos do M6387"]
    KIA["11 KI0"]
    KIB["12 KI1"]
    KIC["13 KI2"]
    KID["14 KI3"]
    KIE["15 KI4"]
    KIF["16 KI5"]
    KIG["17 KI6"]
    KIH["18 KI7"]
    KOA["30 KO0"]
    KOB["29 KO1"]
    KOC["28 KO2"]
    KOD["27 KO3"]
    KOE["26 KO4"]
    KOF["25 KO5"]
    KOG["24 KO6"]
    GND7["7 GND"]
  end

  subgraph CJ["CJMCU-2317"]
    A0["GPA0"]
    A1["GPA1"]
    A2["GPA2"]
    A3["GPA3"]
    A4["GPA4"]
    A5["GPA5"]
    A6["GPA6"]
    A7["GPA7"]
    B0["GPB0"]
    B1["GPB1"]
    B2["GPB2"]
    B3["GPB3"]
    B4["GPB4"]
    B5["GPB5"]
    B6["GPB6"]
    SDA["SDA"]
    SCL["SCL"]
    VCC["VCC"]
    GNDM["GND"]
    RST["RESET"]
    ADDR["A0 A1 A2"]
  end

  subgraph PIC["Pico 2020"]
    GP0["GP0"]
    GP1["GP1"]
    V33["3V3"]
    PGND["GND"]
  end

  KIA --> A0
  KIB --> A1
  KIC --> A2
  KID --> A3
  KIE --> A4
  KIF --> A5
  KIG --> A6
  KIH --> A7
  KOA --> B0
  KOB --> B1
  KOC --> B2
  KOD --> B3
  KOE --> B4
  KOF --> B5
  KOG --> B6
  GND7 --> GNDM
  GNDM --> PGND
  VCC --> V33
  RST --> V33
  ADDR --> PGND
  SDA --> GP0
  SCL --> GP1
```

### Uma tecla (como o MCP lê)

```mermaid
flowchart LR
  GPB["GPB n<br/>sai LOW na varredura"]
  SW["borracha<br/>fecha o cruzamento"]
  GPA["GPA m<br/>pull-up 100k<br/>LOW = tecla down"]
  GPB --- SW --- GPA
```

F3 = KO0+KI0 = toco 30 + toco 11. C6 = KO3+KI7 = toco 27 + toco 18. Multímetro: continuidade entre esses pares, **não** contra o pino 7.

## 1. Achar o pino 1

Lado dos **componentes**. O `M6387` é o SDIL de 30 pinos (mais estreito que DIP). Chanfro ou ponto = pino 1.

```
          chanfro
     1 ●              30  KO0
     2                29  KO1
     3                28  KO2
     ...              27  KO3
    10 VDD            26  KO4
    11 KI0            25  KO5
    12 KI1            24  KO6
    13 KI2            23  (não usar)
    14 KI3            22
    15 KI4            21
    ----------------
    16 KI5   17 KI6   18 KI7   … até 16 embaixo à direita
```

Contagem DIP: esquerda 1→15 (de cima para baixo), direita 16→30 (de baixo para cima).  
Os furos no meio da face de solda da foto **são esses pinos**. Marque o pino 1 com caneta antes de virar a placa — no lado da solda a imagem está espelhada.

Pinos que importam:

| Função | M6387 |
| --- | --- |
| KI0–KI7 | 11, 12, 13, 14, 15, 16, 17, 18 |
| KO0–KO6 | 30, 29, 28, 27, 26, 25, 24 |
| GND | 7 |

`KO7` (pino 23) no SA-1 não varre tecla. Ignore.

## 2. Isolar o chip (melhor: remover)

O MCP não pode dividir o barramento com o `M6387`. **Tirar o chip inteiro é o isolamento mais limpo** — melhor que cortar 15 trilhas. Sem o OKI, as linhas KI/KO ficam só na matriz + pads vazios.

É SDIL (passo mais estreito que DIP comum) numa placa de uma face, fácil de arrancar pad. Não force com sucção de uma vez.

1. Marque o pino 1 (chanfro) com caneta **antes** de sair o chip.
2. Com alicate de corte, seccione os 30 pinos **rente ao encapsulamento**. O corpo sai; os tocos ficam nos furos.
3. Dessolde cada toco, um a um, com malha dessoldadora. Sem alavancar o pad.
4. Pode **manter os tocos** dos pinos e soldar os cambos neles (estaqueie com um pouco de solda). Não precisa arrancar o metal do furo. Use os tocos 11–18, 24–30 e o 7 (GND), ou outro GND da placa.

Pilhas / DC 6 V **fora**. O 4558 e os jacks podem ficar.

## 2b. Teste com o multímetro (fazer isto agora)

Pilhas e DC **desligados**. Modo **continuidade** (apito) ou ohms — **não** volts. Sem o chip e sem fonte, as linhas da matriz não têm tensão.

A tecla **não fecha contra o GND**. Ela só une um `KO` a um `KI`. Com a ponta preta no pino 7 você **não** ouve apito ao apertar tecla. Isso é normal.

### O que o pino 7 serve

Ponta preta no **7**. Ponta vermelha nos outros tocos, **sem** apertar tecla:

| Apita? | Toco | Significado |
| --- | --- | --- |
| Sim | 1, 2, 6 | TEST / AGND, também no terra — ignore para a matriz |
| Não | 11–18, 24–30 | Candidatos KI/KO — bons |

Se 11–18 ou 24–30 apitarem com o 7, o pad está em curto com o terra (trilha rasgada ou solda). Não pule esses.

### Mapear a matriz (mude a ponta preta)

1. Borrachas no lugar, teclas livres.
2. Ponta **preta no toco 30** (KO0), vermelha no **11** (KI0).
3. Aperte **F3** (branca mais grave). Tem que apitar. Solte: para.
4. Preta no **27**, vermelha no **18**. Aperte **C6** (branca mais aguda). Apita.

Ordem sugerida (preta = KO, vermelha = KI):

| Apertar | Preta KO | Vermelha KI |
| --- | --- | --- |
| F3 | 30 | 11 |
| F#3 | 30 | 12 |
| G3 | 30 | 13 |
| … até C4 | 30 | 18 |
| C#4 | 29 | 11 |
| … | … | … |
| C6 | 27 | 18 |
| Botão 0 | 26 | 11 |
| Stop | 25 | 16 |

Sem tecla: infinito / aberto. Com tecla: apito ou poucos ohms (carbono da borracha, 100 Ω–2 kΩ é normal; não precisa ser 0 Ω).

Se F3 não fechar 30↔11: a numeração do pino 1 está invertida. Teste o canto oposto (toco que você achava 16 pode ser o 1). Anote o par que apitar e manda.

## 3. Ligar os cambos no CJMCU

Verso do módulo, texto `CJMCU` legível. `B0/A0` é **dois furos**: o mais perto do texto = **GPA**; o do outro lado = **GPB**.  
Não use o `A0` da **coluna esquerda** (endereço).

### Dos tocos LSI1 → MCP

| Cambo no toco | Silk no verso | Furo do par |
| --- | --- | --- |
| 11 | B0/A0 | GPA (perto do texto) |
| 12 | B1/A1 | GPA |
| 13 | B2/A2 | GPA |
| 14 | B3/A3 | GPA |
| 15 | B4/A4 | GPA |
| 16 | B5/A5 | GPA |
| 17 | B6/A6 | GPA |
| 18 | B7/A7 | GPA |
| 30 | B0/A0 | GPB (lado oposto) |
| 29 | B1/A1 | GPB |
| 28 | B2/A2 | GPB |
| 27 | B3/A3 | GPB |
| 26 | B4/A4 | GPB |
| 25 | B5/A5 | GPB |
| 24 | B6/A6 | GPB |
| 7 | GND | coluna esquerda |

Toco 23 e o GPB7 de `B7/A7` ficam soltos.

### CJMCU → Pico (para a sonda)

| Silk esquerda | Pico |
| --- | --- |
| VCC | 3V3 |
| GND | GND (junto com o toco 7) |
| SDA/SI | GP0 |
| SCL/SCK | GP1 |
| RESET | 3V3 |
| A0, A1, A2 | GND |
| NC/SO, NC/CS, ITB/ITA | soltos |

Só 3,3 V. Sem VBUS / 5 V.

---

Tudo em 3,3 V. Sem 5 V nesta etapa.

## 4. Teste agora (Pico + CJMCU soldados)

Arduino IDE:

1. Placa **Raspberry Pi Pico** (Earle Philhower, não o Mbed).
2. Biblioteca **Adafruit MCP23017** (Library Manager).
3. Porta USB do Pico → grave `firmware/probe/probe.ino`.
4. Serial Monitor **115200**.

| Serial / LED | Significado |
| --- | --- |
| `MCP OK em 0x20` + LED aceso | I2C certo. Pode apertar tecla |
| `achou 0x21`…`0x27` | A0–A2 não estão todos no GND |
| `nenhum dispositivo` + LED piscando rápido | VCC, GND, RESET, SDA/SCL |

Com I2C ok e borrachas no lugar: F3 → `DOWN  KO0 KI0  F3`.

Com o MCP OK, aperte nesta ordem:

| Apertar | Serial esperado | Se vier outro par |
| --- | --- | --- |
| F3 (branca mais grave) | `KO0 KI0  F3` | KI trocado ou KO invertido |
| C6 (branca mais aguda) | `KO3 KI7  C6` | mesma coisa |
| C4 (8ª tecla) | `KO0 KI7  C4` | colunas deslocadas |
| Botão `0` | `KO4 KI0  0` | linhas de botão (KO4–KO6) |

Aperte as 32 teclas e anote qualquer linha que não bata. **Não feche o mapa ainda.** O nome à direita (`F3`, `0`, …) é só uma hipótese.

### Aprender o mapa (sonda expandida)

Grave de novo `firmware/probe/probe.ino`. Serial **115200**. No monitor, envie `l` e Enter.

A sonda pede **44** toques, um de cada vez, largar antes do seguinte:

1. As 32 teclas **F3 → C6** (cromático, incluindo pretas).
2. Botões **0–9**, depois `stop` e `demo`. Sem tempo / volume / rhythm da placa (volume = EC11).

Se um passo não existir ou não responder: envie `n` (saltar).

Cada `DOWN` mostra o cruzamento cru e **quatro** nomes:

| Campo | Significado |
| --- | --- |
| `KO4 KI3` | O que o MCP leu (isto é o dado) |
| `Casio:` | Mapa publicado (weltenschule) |
| `KIrev:` | Se KI0–7 estiverem invertidos no cabo |
| `KOrev:` | Se KO0–6 estiverem invertidos |
| `ambos:` | Os dois invertidos |
| `held` | Todas as células baixas **agora**. Mais de uma (além de `S`) = curto ou fantasma |
| `S` | Colado no arranque (ex. stop/demo). Ignorado no aprender |

No fim aparece um bloco `--- aprendido ---`. Copia isso para o chat **antes** de alterar `firmware/pico/matrix.h`.

Outros comandos: `h` ajuda, `m` mapa já, `c` limpa `#` vistos, `r` sai do aprender.

## 5. Pronto para a etapa 2 quando

- [ ] MCP responde em `0x20`
- [ ] F3 = KO0 KI0 e C6 = KO3 KI7
- [ ] Botões 0–9 e stop/demo nas linhas KO4–KO6 (tempo / vol / rhythm da placa: ignorados)
- [ ] Nenhuma tecla dispara sozinha (GND comum + isolamento ok)

Aí entram EC11, KY-023 e firmware MIDI (`firmware/pico/pico.ino`).

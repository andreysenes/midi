#!/usr/bin/env python3
"""Gera hardware/m3210-maim.svg — 1 unidade = 1 mm. Vista verso (solda)."""

from pathlib import Path

OUT = Path(__file__).with_name("m3210-maim.svg")

KO_COL = ["#e07a3d", "#d4a017", "#3d9a62", "#3d7ec4", "#9b6bb3", "#c45c6a", "#6a8a9b"]
NOTE = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]

CASIO_PIANO = [
    ["F3", "F#3", "G3", "G#3", "A3", "A#3", "B3", "C4"],
    ["C#4", "D4", "D#4", "E4", "F4", "F#4", "G4", "G#4"],
    ["A4", "A#4", "B4", "C5", "C#5", "D5", "D#5", "E5"],
    ["F5", "F#5", "G5", "G#5", "A5", "A#5", "B5", "C6"],
]
CASIO_BTN = [
    ["0", "1", "2", "3", "4", "tempo+", "vol+", "rhythm"],
    ["5", "6", "7", "8", "9", "stop", "tempo−", "vol−"],
    ["0*", "1*", "2*", "3*", "4*", "demo", "demo", "demo"],
]
LEARNED_BTN = [
    ["0", "1", "2", "3", "4", "—", "—", "—"],
    ["5", "6", "7", "8", "9", "stop", "—", "—"],
    ["—", "—", "—", "—", "—", "demo", "—", "—"],
]

# KI5–7 saltados no aprender; KO4 0–4 teórico.
THEO_PIANO_KI = {5, 6, 7}

PIN_KI = [11, 12, 13, 14, 15, 16, 17, 18]
PIN_KO = [30, 29, 28, 27, 26, 25, 24]


def midi_name(m: int) -> str:
    return NOTE[m % 12] + str(m // 12 - 1)


def esc(s: str) -> str:
    return (
        s.replace("&", "&amp;")
        .replace("<", "&lt;")
        .replace(">", "&gt;")
    )


parts: list[str] = []


def add(s: str) -> None:
    parts.append(s if s.endswith("\n") else s + "\n")


def aname(k: str) -> str:
    if k.endswith("_"):
        k = k[:-1]
    return k.replace("_", "-")


def attrs(a: dict) -> str:
    return " ".join(f'{aname(k)}="{v}"' for k, v in a.items())


def rect(x, y, w, h, **a) -> None:
    add(f'<rect x="{x:.2f}" y="{y:.2f}" width="{w:.2f}" height="{h:.2f}" {attrs(a)}/>')


def circ(cx, cy, r, **a) -> None:
    add(f'<circle cx="{cx:.2f}" cy="{cy:.2f}" r="{r:.2f}" {attrs(a)}/>')


def line(x1, y1, x2, y2, **a) -> None:
    add(f'<line x1="{x1:.2f}" y1="{y1:.2f}" x2="{x2:.2f}" y2="{y2:.2f}" {attrs(a)}/>')


def text(x, y, s, **a) -> None:
    add(f'<text x="{x:.2f}" y="{y:.2f}" {attrs(a)}>{esc(s)}</text>')


def group(gid=None, **a):
    ident = f'id="{gid}" ' if gid else ""
    add(f"<g {ident}{attrs(a)}>")


def end() -> None:
    add("</g>")


# --- layout mm ---
BW, BH = 352.0, 74.0
OX, OY = 24.0, 36.0
KEY_Y = OY + 61.0
KEY_PITCH = 10.0
KEY_X0 = OX + 14.0
BUS_Y0 = OY + 44.0
CHIP_X, CHIP_Y = OX + 198.0, OY + 18.0

add(
    f'''<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 400 640" width="400mm" height="640mm">
<title>Casio SA-1 · M3210-MAIM(F) · matriz verso 1:1 mm</title>
<desc>Vista da face de solda. Grave à esquerda, agudo à direita, teclas em baixo — igual às fotos. 1 unidade = 1 mm. Contorno da placa 352 × 74 mm.</desc>
<style>
  text {{ font-family: "IBM Plex Sans", "Segoe UI", Helvetica, Arial, sans-serif; fill: #1a1814; }}
  .mono {{ font-family: "IBM Plex Mono", ui-monospace, Menlo, monospace; }}
  .dim {{ fill: #6a6358; }}
  .w {{ fill: #f4efe4; }}
</style>
<rect width="400" height="640" fill="#e8e2d6"/>
'''
)

# header
text(24, 16, "CASIO SA-1  ·  PCB M3210-MAIM(F)", font_size="7", font_weight="700")
text(24, 24, "Face de solda  ·  1 mm = 1 unidade  ·  imprimir a 100% para sobrepor na placa", class_="dim", font_size="3.2")
text(376, 16, "hardware/m3210-maim.svg", class_="dim", font_size="3", text_anchor="end")

# board
group("placa")
rect(OX, OY, BW, BH, fill="#1c3d2e", stroke="#0e2018", stroke_width="0.6", rx="1.2")

# mounting holes (approximate from photos)
for hx, hy in ((OX + 8, OY + 8), (OX + 8, OY + 66), (OX + 344, OY + 8), (OX + 344, OY + 66),
               (OX + 120, OY + 8), (OX + 230, OY + 8)):
    circ(hx, hy, 1.6, fill="none", stroke="#7aa090", stroke_width="0.3")

# copper pour hint left (power)
rect(OX + 4, OY + 14, 28, 22, fill="#2a5644", opacity="0.5", rx="0.4")
text(OX + 18, OY + 22, "W / R", class_="w", font_size="2.2", text_anchor="middle")
text(OX + 18, OY + 26.5, "speaker", class_="w", font_size="1.8", text_anchor="middle")
text(OX + 18, OY + 32, "DC / power", class_="w", font_size="1.8", text_anchor="middle")

# silk
text(OX + 338, OY + 28, "M3210-", fill="#8fbfa8", font_size="3.4", font_weight="700", text_anchor="end")
text(OX + 338, OY + 33, "MAIM(F)", fill="#8fbfa8", font_size="3.4", font_weight="700", text_anchor="end")

# KI bus — 8 traces above keys, running to chip
group("ki-bus")
for ki in range(8):
    y = BUS_Y0 + ki * 0.85
    x1 = KEY_X0 - 4
    x2 = OX + BW - 18
    line(x1, y, x2, y, stroke="#7ec8c3", stroke_width="0.28", opacity="0.85")
    text(x2 + 1.2, y + 0.6, f"KI{ki}", class_="mono", fill="#b6ebe4", font_size="1.6")
end()

# carbon jumpers over KI bus (right, as in photos)
group("carbono")
jx = OX + BW - 42
for i, ko in enumerate((3, 2, 1)):
    y = BUS_Y0 - 2.2 + i * 3.4
    rect(jx, y, 22, 1.5, fill="#1a1410", rx="0.3")
    text(jx + 11, y - 0.4, f"jumper KO{ko}", fill="#c4b8a8", font_size="1.5", text_anchor="middle")
end()

# KO rails under each group of 8 keys
group("ko-rails")
for g in range(4):
    x1 = KEY_X0 + g * 8 * KEY_PITCH - 4.2
    x2 = KEY_X0 + (g * 8 + 7) * KEY_PITCH + 4.2
    y = KEY_Y + 5.6
    line(x1, y, x2, y, stroke=KO_COL[g], stroke_width="0.7", stroke_linecap="round")
    mx = (x1 + x2) / 2
    line(mx, y, mx, CHIP_Y + 22, stroke=KO_COL[g], stroke_width="0.35", opacity="0.7")
end()

# 32 key pads
group("teclas")
for i in range(32):
    midi = 53 + i
    ko_c, ki_c = divmod(i, 8)
    cx = KEY_X0 + i * KEY_PITCH
    cy = KEY_Y
    fill = "#11100e"
    stroke = KO_COL[ko_c]
    circ(cx, cy, 3.6, fill=fill, stroke=stroke, stroke_width="0.45")
    # comb hint
    line(cx - 2.2, cy, cx + 2.2, cy, stroke="#4a4844", stroke_width="0.2")
    line(cx, cy - 2.2, cx, cy + 2.2, stroke="#4a4844", stroke_width="0.2")
    # KI tap
    line(cx, cy - 3.6, cx, BUS_Y0 + ki_c * 0.85, stroke="#7ec8c3", stroke_width="0.22", opacity="0.7")
    name = midi_name(midi)
    text(cx, cy + 0.7, name, class_="w", font_size="1.55", text_anchor="middle", font_weight="600")
    badge = "○" if ki_c in THEO_PIANO_KI else "●"
    text(cx, cy - 4.6, f"{badge}{ki_c}", class_="mono", fill=KO_COL[ko_c], font_size="1.45", text_anchor="middle")
end()

# function buttons (top row — X aproximado nas fotos)
group("botoes")
btns = [
    # label, ko, ki, x_offset_from_board_left, ignore
    ("vol−", 5, 7, 48, True),
    ("vol+", 4, 6, 58, True),
    ("tmp−", 5, 6, 70, True),
    ("tmp+", 4, 5, 80, True),
    ("rhy", 4, 7, 92, True),
    ("demo", 6, 5, 110, False),
    ("stop", 5, 5, 124, False),
    ("0", 4, 0, 148, False),
    ("1", 4, 1, 158, False),
    ("2", 4, 2, 168, False),
    ("3", 4, 3, 178, False),
    ("4", 4, 4, 188, False),
    ("5", 5, 0, 204, False),
    ("6", 5, 1, 214, False),
    ("7", 5, 2, 224, False),
    ("8", 5, 3, 234, False),
    ("9", 5, 4, 244, False),
]
text(OX + 48, OY + 6.2, "botões (X aproximado nas fotos; KO/KI = mapa Casio / weltenschule)", class_="w", font_size="1.7")
for label, ko, ki, xoff, ignore in btns:
    cx = OX + xoff
    cy = OY + 11.5
    col = KO_COL[ko]
    op = "0.45" if ignore else "1"
    circ(cx, cy, 3.1, fill="#141210", stroke=col, stroke_width="0.4", opacity=op)
    text(cx, cy + 0.6, label, class_="w", font_size="1.5", text_anchor="middle", opacity=op)
end()

# LSI1 footprint — solder side, two horizontal rows
# Convention documented: component notch LEFT after 90° CW; solder = rows swapped.
group("lsi1")
rect(CHIP_X, CHIP_Y, 42, 12, fill="#0f1c16", stroke="#c8c0b0", stroke_width="0.35", rx="0.4")
text(CHIP_X + 21, CHIP_Y + 5.2, "LSI1  M6387", class_="w", font_size="2.4", text_anchor="middle", font_weight="700")
text(CHIP_X + 21, CHIP_Y + 8.6, "11–18 KI  ·  24–30 KO  ·  7 GND", class_="w", font_size="1.6", text_anchor="middle")
end()

end()  # placa

# scale bar
line(OX, OY + BH + 6, OX + 50, OY + BH + 6, stroke="#1a1814", stroke_width="0.4")
line(OX, OY + BH + 5, OX, OY + BH + 7, stroke="#1a1814", stroke_width="0.4")
line(OX + 50, OY + BH + 5, OX + 50, OY + BH + 7, stroke="#1a1814", stroke_width="0.4")
text(OX + 25, OY + BH + 10, "50 mm", font_size="2.4", text_anchor="middle")
text(OX + BW, OY + BH + 10, "352 × 74 mm  (caixa SA-1 375 mm; placa ~4,8:1 como nas fotos)", class_="dim", font_size="2.2", text_anchor="end")
for g in range(4):
    mx = KEY_X0 + (g * 8 + 3.5) * KEY_PITCH
    text(mx, OY + BH + 10, f"KO{g} p.{PIN_KO[g]}", class_="mono", fill=KO_COL[g], font_size="2.2", text_anchor="middle", font_weight="700")

# legend
ly = OY + BH + 18
text(24, ly, "Como ler a palheta", font_size="4", font_weight="700")
text(24, ly + 6, "● KI0–4  medido no aprender desta unidade (2026-08-31)   ○ KI5–7  teórico — saltado no aprender, validar com sonda", font_size="2.6")
text(24, ly + 11, "Cor do anel = KO Casio (cobre / pino do M6387). MCP diferente do Casio = tabela de baixo (serpentina KO1↔KO3 + KI0–4).", font_size="2.6")
text(24, ly + 16, "A tecla NÃO fecha contra o GND (pino 7). Continuidade só KO↔KI com a borracha premida. Jumpers pretos = carbono a saltar o barramento KI.", font_size="2.6")

# tables
def table(title, origin_x, origin_y, rows, header_ki=True, learned=False):
    text(origin_x, origin_y, title, font_size="3.6", font_weight="700")
    cw, rh = 22.5, 9.5
    x0, y0 = origin_x, origin_y + 3
    # header
    rect(x0, y0, 22, rh, fill="#1a1814")
    text(x0 + 11, y0 + 6.2, "KO\\KI", class_="w mono", font_size="2.1", text_anchor="middle")
    for ki in range(8):
        rect(x0 + 22 + ki * cw, y0, cw, rh, fill="#1a1814")
        pin = PIN_KI[ki]
        text(x0 + 22 + ki * cw + cw / 2, y0 + 4.2, f"KI{ki}", class_="w mono", font_size="2", text_anchor="middle")
        text(x0 + 22 + ki * cw + cw / 2, y0 + 7.4, f"p.{pin}", class_="w", font_size="1.6", text_anchor="middle")
    for ko, row in enumerate(rows):
        y = y0 + (ko + 1) * rh
        rect(x0, y, 22, rh, fill=KO_COL[ko])
        text(x0 + 11, y + 4.4, f"KO{ko}", class_="w mono", font_size="2.1", text_anchor="middle", font_weight="700")
        text(x0 + 11, y + 7.4, f"p.{PIN_KO[ko]}", class_="w", font_size="1.6", text_anchor="middle")
        for ki, cell in enumerate(row):
            x = x0 + 22 + ki * cw
            fill = "#f7f3ea"
            if learned and ko <= 3 and ki in THEO_PIANO_KI:
                fill = "#efe6c8"
            if cell in ("—", "-", "0*", "1*", "2*", "3*", "4*"):
                fill = "#ddd8cc"
            if cell in ("tempo+", "vol+", "rhythm", "tempo−", "vol−"):
                fill = "#e4ddd0"
            rect(x, y, cw, rh, fill=fill, stroke="#c9c2b4", stroke_width="0.2")
            text(x + cw / 2, y + 6, str(cell), class_="mono", font_size="2.15", text_anchor="middle")


table("Mapa Casio no cobre  —  pinos do M6387  (SM PK-5 M3210 + weltenschule; KO2 KI2 = B4)", 24, 148, CASIO_PIANO + CASIO_BTN)
text(24, 148 + 3 + 10 * 9.5 + 5, "* cópia na PCB, muitas vezes sem borracha no SA-1.  tempo/vol/rhythm: ignorados (volume = EC11).", class_="dim", font_size="2.2")

table("Mapa aprendido no MCP desta unidade  —  firmware/pico/matrix.h  (KI5–7 piano e KO4 0–4: teórico)", 24, 268, [CASIO_PIANO[0], ["A5", "G#5", "G5", "F#5", "F5", "A#5", "B5", "C6"], ["C#5", "C5", "B4", "A#4", "A4", "D5", "D#5", "E5"], ["F4", "E4", "D#4", "D4", "C#4", "F#4", "G4", "G#4"]] + LEARNED_BTN, learned=True)

text(24, 268 + 3 + 10 * 9.5 + 5, "KO0 = Casio.  KO1↔KO3 e KI0–4 invertidos nas linhas agudas = serpentina no cobre ou cambos 29↔27. Não «corrigir» o firmware para a tabela Casio sem medir.", class_="dim", font_size="2.2")

# pin jump
ty = 390
text(24, ty, "Jump  tocos LSI1 → CJMCU-2317  (se o pino 1 estiver certo)", font_size="3.6", font_weight="700")
cells = [
    ("11–18", "KI0–7", "GPA0–7", "perto do texto B0/A0"),
    ("30…24", "KO0–6", "GPB0–6", "lado oposto do par"),
    ("7", "GND", "GND", "comum com o Pico"),
    ("23", "KO7", "solto", "não varre tecla"),
]
for i, (a, b, c, d) in enumerate(cells):
    x = 24 + i * 90
    rect(x, ty + 4, 86, 16, fill="#f7f3ea", stroke="#c9c2b4", stroke_width="0.25", rx="1")
    text(x + 43, ty + 9, f"{a}  {b}", class_="mono", font_size="2.3", text_anchor="middle", font_weight="700")
    text(x + 43, ty + 13.5, f"→ {c}", font_size="2.2", text_anchor="middle")
    text(x + 43, ty + 17.5, d, class_="dim", font_size="1.8", text_anchor="middle")

# continuity
cy = 422
text(24, cy, "Sete pares que fecham o mapa  (continuidade, pilhas/USB fora)", font_size="3.6", font_weight="700")
checks = [
    ("F3", "30 ↔ 11", "KO0 KI0"),
    ("C4", "30 ↔ 18", "KO0 KI7"),
    ("C#4", "29 ↔ 11", "KO1 KI0  Casio"),
    ("C6", "27 ↔ 18", "KO3 KI7"),
    ("0", "26 ↔ 11", "KO4 KI0"),
    ("stop", "25 ↔ 16", "KO5 KI5"),
    ("demo", "24 ↔ 16", "KO6 KI5–7"),
]
for i, (name, pair, note) in enumerate(checks):
    x = 24 + (i % 4) * 92
    y = cy + 6 + (i // 4) * 18
    rect(x, y, 88, 16, fill="#1a1814", rx="1")
    text(x + 8, y + 6.5, name, class_="w", font_size="2.6", font_weight="700")
    text(x + 8, y + 12, f"{pair}   {note}", class_="w mono", font_size="1.8")

text(24, 470, "Se F3 = 30↔11 mas C#4 = 27↔15 (e F4 = 27↔11): cambos 29 e 27 trocados — é o padrão já no matrix.h. Deixe o mapa aprendido ou troque os fios, não as duas coisas.", font_size="2.5")

# confidence
text(24, 486, "Confiança", font_size="3.6", font_weight="700")
conf = [
    ("alta", "#3d9a62", "Topologia 8 KI × 4 KO de piano; pinos 11–18 / 24–30; KO7 NC; palhetas não vão ao GND. Fotos + SM M3210 + weltenschule."),
    ("média", "#d4a017", "KO0 piano = Casio nesta unidade. Botões 5–9 / stop / demo no MCP. Posição X dos botões no SVG é aproximada."),
    ("aberta", "#c45c6a", "KI5–7 das 32 teclas (A#3 B3 C4 / F#4 G4 G#4 / D5 D#5 E5 / A#5 B5 C6) e botões 0–4: teóricos até a sonda DOWN."),
]
for i, (lvl, col, msg) in enumerate(conf):
    y = 492 + i * 12
    rect(24, y, 14, 8, fill=col, rx="1")
    text(31, y + 5.6, lvl, class_="w", font_size="2.1", text_anchor="middle")
    text(42, y + 5.6, msg, font_size="2.4")

text(24, 534, "Fontes: Casio PK-5 SM (PCB M3210-MA1M, mesma família); weltenschule.de SA-1; fotos verso desta placa; log aprender 2026-08-31 em matrix.h.", class_="dim", font_size="2.3")
text(24, 542, "O Lab (lab/static/index.html) usa o mapa aprendido. Esta folha é o circuito no cobre. Os dois têm de coexistir até os sete pares acima estarem anotados.", class_="dim", font_size="2.3")

# DIP callout
text(24, 560, "M6387 visto do lado dos componentes  (chanfro = pino 1, esquerda 1→15 para baixo)", font_size="3.2", font_weight="700")
# mini dip
dx, dy = 24, 566
rect(dx + 28, dy, 18, 52, fill="#1a1814", rx="0.6")
text(dx + 37, dy + 27, "M6387", class_="w", font_size="2.2", text_anchor="middle", transform=f"rotate(-90 {dx+37} {dy+27})")
left = [(1, "TEST"), (7, "GND ★"), (10, "VDD"), (11, "KI0 ★"), (12, "KI1"), (13, "KI2"), (14, "KI3"), (15, "KI4 ★")]
right = [(30, "KO0 ★"), (29, "KO1"), (28, "KO2"), (27, "KO3 ★"), (26, "KO4"), (25, "KO5"), (24, "KO6 ★"), (23, "KO7 NC"), (18, "KI7 ★"), (16, "KI5")]
for i, (p, n) in enumerate(left):
    y = dy + 3 + i * 6.2
    circ(dx + 28, y, 0.8, fill="#f4efe4")
    text(dx + 26.5, y + 0.7, f"{p}  {n}", class_="mono", font_size="2", text_anchor="end")
for i, (p, n) in enumerate(right):
    y = dy + 3 + i * 5.0
    circ(dx + 46, y, 0.8, fill="#f4efe4")
    text(dx + 48, y + 0.7, f"{p}  {n}", class_="mono", font_size="2")

text(dx + 100, dy + 8, "No verso a numeração está espelhada. Marque o pino 1 com caneta antes de virar.", font_size="2.4")
text(dx + 100, dy + 14, "★ = soldar cambo.  KI5–7 = pinos 16, 17, 18 (canto depois do 15, a contar pelo outro lado).", font_size="2.4")
text(dx + 100, dy + 20, "GPA = entradas + pull-up (LOW = tecla).  GPB = varredura activa LOW.", font_size="2.4")
text(dx + 100, dy + 26, "Documentação: hardware/MATRIZ.md  ·  jump: ETAPA-1.md  ·  firmware: firmware/pico/matrix.h", font_size="2.4")

add("</svg>\n")
OUT.write_text("".join(parts), encoding="utf-8")
print(f"wrote {OUT} ({OUT.stat().st_size} bytes)")

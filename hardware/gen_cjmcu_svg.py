#!/usr/bin/env python3
"""Gera hardware/cjmcu-2317-solda.svg — verso do módulo para a bancada."""

from pathlib import Path

OUT = Path(__file__).with_name("cjmcu-2317-solda.svg")

KO = ["#e07a3d", "#d4a017", "#3d9a62", "#3d7ec4", "#9b6bb3", "#c45c6a", "#6a8a9b"]
KI = "#2e7a88"

parts: list[str] = []


def add(s: str) -> None:
    parts.append(s if s.endswith("\n") else s + "\n")


def aname(k: str) -> str:
    return k[:-1] if k.endswith("_") else k.replace("_", "-")


def a2s(a: dict) -> str:
    return " ".join(f'{aname(k)}="{v}"' for k, v in a.items())


def rect(x, y, w, h, **a) -> None:
    add(f'<rect x="{x:.1f}" y="{y:.1f}" width="{w:.1f}" height="{h:.1f}" {a2s(a)}/>')


def circ(cx, cy, r, **a) -> None:
    add(f'<circle cx="{cx:.1f}" cy="{cy:.1f}" r="{r:.1f}" {a2s(a)}/>')


def text(x, y, s, **a) -> None:
    t = s.replace("&", "&amp;").replace("<", "&lt;")
    add(f'<text x="{x:.1f}" y="{y:.1f}" {a2s(a)}>{t}</text>')


add(
    '''<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 210 188" width="210mm" height="188mm">
<title>CJMCU-2317 verso — solda da matriz SA-1</title>
<style>
  text { font-family: "IBM Plex Sans", "Segoe UI", Helvetica, Arial, sans-serif; fill: #1a1814; }
  .mono { font-family: "IBM Plex Mono", ui-monospace, Menlo, monospace; }
  .w { fill: #f4efe4; }
  .dim { fill: #5c564c; }
</style>
<rect width="210" height="188" fill="#e8e2d6"/>
'''
)

text(8, 11, "CJMCU-2317 verso · texto CJMCU legível", font_size="4.6", font_weight="700")
text(8, 17, "Número no furo = toco do M6387. GPA = A (KI). GPB = B (KO). A0 da esquerda ≠ GPA0.", class_="dim", font_size="2.7")

MX, MY, MW, MH = 52, 22, 106, 148
rect(MX, MY, MW, MH, fill="#2a6b4a", stroke="#0e2018", stroke_width="0.55", rx="1.8")
text(MX + MW / 2, MY + 8, "CJMCU-2317", class_="w", font_size="4.2", font_weight="700", text_anchor="middle")
rect(MX + 29, MY + 12, 48, 12, fill="#1a1814", rx="0.6")
text(MX + 53, MY + 20.2, "MCP23017", class_="w", font_size="2.6", text_anchor="middle")

pitch = 11.0
y0 = MY + 34
lx = MX + 9
bx, ax = MX + 68, MX + 82

left = [
    ("A2", "→ GND", "#8a3030"),
    ("A1", "→ GND", "#8a3030"),
    ("A0", "→ GND", "#8a3030"),
    ("GND", "← 7 + Pico", "#222"),
    ("VCC", "← Pico 3V3", "#a33"),
    ("RESET", "← Pico 3V3", "#a33"),
    ("SDA/SI", "← GP0", "#222"),
    ("SCL/SCK", "← GP1", "#222"),
    ("NC/SO", "solto", "#666"),
    ("NC/CS", "solto", "#666"),
]
for i, (silk, dest, col) in enumerate(left):
    y = y0 + i * pitch
    circ(lx, y, 2.0, fill="#e4dccf", stroke="#1a1814", stroke_width="0.4")
    text(lx + 4.2, y + 1.15, silk, class_="mono w", font_size="2.8")
    text(lx - 3.6, y + 1.15, dest, class_="mono", fill=col, font_size="2.7", text_anchor="end")

right = [
    ("VCC/GND", None, None, "GND ok"),
    ("ITB/ITA", None, None, "solto"),
    ("B0/A0", 30, 11),
    ("B1/A1", 29, 12),
    ("B2/A2", 28, 13),
    ("B3/A3", 27, 14),
    ("B4/A4", 26, 15),
    ("B5/A5", 25, 16),
    ("B6/A6", 24, 17),
    ("B7/A7", None, 18),
]

text(bx, y0 - 7.2, "B  KO", class_="w mono", font_size="2.4", text_anchor="middle")
text(ax, y0 - 7.2, "A  KI", class_="w mono", font_size="2.4", text_anchor="middle")

for i, row in enumerate(right):
    silk = row[0]
    ko_pin, ki_pin = row[1], row[2]
    y = y0 + i * pitch
    circ(bx, y, 2.0, fill="#e4dccf", stroke="#1a1814", stroke_width="0.4")
    circ(ax, y, 2.0, fill="#e4dccf", stroke="#1a1814", stroke_width="0.4")
    text(ax + 4.0, y + 1.15, silk, class_="mono w", font_size="2.6")
    if ko_pin is not None:
        col = KO[i - 2]
        circ(bx, y, 1.45, fill=col, stroke="#1a1814", stroke_width="0.2")
        text(bx, y + 1.05, str(ko_pin), class_="mono", fill="#fff", font_size="2.3", font_weight="700", text_anchor="middle")
        circ(ax, y, 1.45, fill=KI, stroke="#1a1814", stroke_width="0.2")
        text(ax, y + 1.05, str(ki_pin), class_="mono", fill="#fff", font_size="2.3", font_weight="700", text_anchor="middle")
    elif ki_pin is not None:
        circ(ax, y, 1.45, fill=KI, stroke="#1a1814", stroke_width="0.2")
        text(ax, y + 1.05, str(ki_pin), class_="mono", fill="#fff", font_size="2.3", font_weight="700", text_anchor="middle")
        text(bx, y + 1.05, "—", class_="mono", fill="#f4efe4", font_size="2.6", text_anchor="middle")
    else:
        text(ax + 28, y + 1.15, row[3], class_="w", font_size="2.4")

text(8, 178, "Furo B (esquerda do par) = KO, toco 30…24.   Furo A (perto do texto) = KI, toco 11–18.", font_size="2.8")
text(8, 184, "A0 A1 A2 e RESET: cambos curtos no módulo. Só 3,3 V. Toco 23 solto.  ·  SOLDA-MCP.md", class_="dim", font_size="2.5")

add("</svg>\n")
OUT.write_text("".join(parts), encoding="utf-8")
print(f"wrote {OUT} ({OUT.stat().st_size} bytes)")

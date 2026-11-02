# -*- coding: utf-8 -*-
"""Genera Presentacion_Defensa_PicoReflex.pptx — diseño Swiss Modernism 2.0.

- Paleta: primario #2563EB, secundario #3B82F6, acento #F97316, fondo #F8FAFC,
  texto #1E293B, card #FFFFFF.
- Tipografía: Inter (fallback del visor si no está instalada); código en Consolas.
- Diagramas: PNG embebidos (python-pptx no soporta SVG) desde docs/img/.
- Contenido espejo de docs/estudio/presentacion-defensa.md.

Usa SOLO la API nativa de python-pptx (sin dependencias nuevas).
"""

import os
from pptx import Presentation
from pptx.util import Inches, Pt
from pptx.dml.color import RGBColor
from pptx.enum.shapes import MSO_SHAPE
from pptx.enum.text import MSO_ANCHOR

BASE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(BASE, "Presentacion_Defensa_PicoReflex.pptx")
IMG = os.path.join(BASE, "docs", "img")

# --- Paleta (Swiss Modernism 2.0) ---
PRIMARY = RGBColor(0x25, 0x63, 0xEB)   # azul
SECONDARY = RGBColor(0x3B, 0x82, 0xF6) # azul claro
ACCENT = RGBColor(0xF9, 0x73, 0x16)    # naranja
BG = RGBColor(0xF8, 0xFA, 0xFC)        # fondo
TEXT = RGBColor(0x1E, 0x29, 0x3B)       # texto
CARD = RGBColor(0xFF, 0xFF, 0xFF)       # card
MUTED = RGBColor(0x47, 0x55, 0x69)      # gris pizarra
BAND_ALT = RGBColor(0xF1, 0xF5, 0xF9)   # fila alterna tabla
WHITE = RGBColor(0xFF, 0xFF, 0xFF)
LIGHT = RGBColor(0xDB, 0xEA, 0xFE)      # azul muy claro (portada)

FONT = "Inter"
MONO = "Consolas"

MARGIN = Inches(0.7)


def _set_16x9(prs):
    prs.slide_width = Inches(13.333)
    prs.slide_height = Inches(7.5)


def _set_bg(slide, color):
    slide.background.fill.solid()
    slide.background.fill.fore_color.rgb = color


def _rect(slide, left, top, width, height, fill, line=None):
    sh = slide.shapes.add_shape(MSO_SHAPE.RECTANGLE, left, top, width, height)
    sh.fill.solid()
    sh.fill.fore_color.rgb = fill
    if line is None:
        sh.line.fill.background()
    else:
        sh.line.color.rgb = line
        sh.line.width = Pt(0.75)
    sh.shadow.inherit = False
    return sh


def _textbox(slide, left, top, width, height):
    tb = slide.shapes.add_textbox(left, top, width, height)
    tf = tb.text_frame
    tf.word_wrap = True
    return tb, tf


def _run(p, text, size, color, bold=False, mono=False):
    r = p.add_run()
    r.text = text
    r.font.name = MONO if mono else FONT
    r.font.size = Pt(size)
    r.font.bold = bold
    r.font.color.rgb = color
    return r


def _title(slide, text):
    # barra de acento + título sobre fondo claro
    _rect(slide, MARGIN, Inches(0.42), Inches(0.5), Inches(0.14), ACCENT)
    tb, tf = _textbox(slide, MARGIN, Inches(0.64), Inches(12.0), Inches(0.85))
    p = tf.paragraphs[0]
    _run(p, text, 28, TEXT, bold=True)


def _card(slide, left, top, width, height):
    return _rect(slide, left, top, width, height, CARD)


def _bullets(slide, items, left, top, width, height, size0=16, size1=14):
    tb, tf = _textbox(slide, left, top, width, height)
    for i, (text, level, mono) in enumerate(items):
        p = tf.paragraphs[0] if i == 0 else tf.add_paragraph()
        p.space_after = Pt(8 if level == 0 else 3)
        p.space_before = Pt(0)
        p.line_spacing = 1.12
        prefix = "•  " if level == 0 else "      "
        _run(p, prefix + text, size1 if level else size0,
             TEXT if level == 0 else MUTED, mono=mono)
    return tb


def _table(slide, headers, rows, left, top, width, col_w, row_h=0.42):
    """Tabla con header coloreado (primario) y filas alternas."""
    nrows = len(rows) + 1
    ncols = len(headers)
    gtbl = slide.shapes.add_table(
        nrows, ncols, left, top, width, Inches(row_h * nrows)).table
    gtbl.first_row = True
    gtbl.horz_banding = False
    # header
    for j, h in enumerate(headers):
        c = gtbl.cell(0, j)
        c.text = h
        c.fill.solid()
        c.fill.fore_color.rgb = PRIMARY
        c.vertical_anchor = MSO_ANCHOR.MIDDLE
        for p in c.text_frame.paragraphs:
            for r in p.runs:
                r.font.name = FONT
                r.font.size = Pt(13)
                r.font.bold = True
                r.font.color.rgb = WHITE
    # body
    for i, row in enumerate(rows, start=1):
        fill = CARD if i % 2 == 1 else BAND_ALT
        for j, val in enumerate(row):
            c = gtbl.cell(i, j)
            c.text = val
            c.fill.solid()
            c.fill.fore_color.rgb = fill
            c.vertical_anchor = MSO_ANCHOR.MIDDLE
            for p in c.text_frame.paragraphs:
                for r in p.runs:
                    r.font.name = MONO if j == 1 else FONT
                    r.font.size = Pt(12)
                    r.font.color.rgb = TEXT
    for j, w in enumerate(col_w):
        gtbl.columns[j].width = Inches(w)
    return gtbl


def _picture(slide, name, left, top, width):
    path = os.path.join(IMG, name)
    return slide.shapes.add_picture(path, left, top, width=width)


# ---------------------------------------------------------------------------
# Filminas
# ---------------------------------------------------------------------------

def slide_portada(prs):
    s = prs.slides.add_slide(prs.slide_layouts[6])  # blank
    _set_bg(s, PRIMARY)
    # banda de acento
    _rect(s, Inches(1.0), Inches(2.0), Inches(0.9), Inches(0.16), ACCENT)
    tb, tf = _textbox(s, Inches(1.0), Inches(2.3), Inches(11.3), Inches(2.4))
    p = tf.paragraphs[0]
    _run(p, "PicoReflex", 60, WHITE, bold=True)
    p2 = tf.add_paragraph()
    _run(p2, "Juego de reacción en Raspberry Pi Pico W", 30, LIGHT)
    sub, stf = _textbox(s, Inches(1.0), Inches(5.5), Inches(11.3), Inches(1.6))
    for i, line in enumerate([
        "Defensa de Trabajo Final · Microprocesadores",
        "Tecnicatura en Automatización y Robótica · UNL · Año 2026",
        "Autor: Alessandro Klein · Firmware v0.9.7 · Pico SDK 2.3.0 · FreeRTOS SMP",
    ]):
        p = stf.paragraphs[0] if i == 0 else stf.add_paragraph()
        p.space_after = Pt(4)
        _run(p, line, 18, LIGHT)


def slide_bullets(prs, title, items, img=None):
    """Filmina estándar: título + card con bullets (opcional imagen a la derecha)."""
    s = prs.slides.add_slide(prs.slide_layouts[6])
    _set_bg(s, BG)
    _title(s, title)
    if img is None:
        _card(s, MARGIN, Inches(1.7), Inches(11.93), Inches(5.3))
        _bullets(s, items, Inches(1.0), Inches(1.95), Inches(11.3), Inches(4.9))
    else:
        _card(s, MARGIN, Inches(1.7), Inches(6.5), Inches(5.3))
        _bullets(s, items, Inches(0.95), Inches(1.95), Inches(6.0), Inches(4.9),
                 size0=14, size1=12.5)
        _picture(s, img, Inches(7.5), Inches(2.1), Inches(5.1))
    return s


def slide_table(prs, title, headers, rows, col_w, note=None):
    s = prs.slides.add_slide(prs.slide_layouts[6])
    _set_bg(s, BG)
    _title(s, title)
    _table(s, headers, rows, MARGIN, Inches(1.8), Inches(11.93), col_w)
    if note:
        tb, tf = _textbox(s, MARGIN, Inches(6.6), Inches(11.93), Inches(0.6))
        _run(tf.paragraphs[0], note, 13, MUTED)
    return s


def build():
    prs = Presentation()
    _set_16x9(prs)

    # 1. Portada
    slide_portada(prs)

    # 2. Qué es
    slide_bullets(prs, "¿Qué es PicoReflex?", [
        ("Dispositivo de entrenamiento de reflejos: 5 LEDs + 5 pulsadores (mismo índice).", 0, False),
        ("Mide el tiempo de reacción en microsegundos (time_us_32() en la IRQ del PIO).", 0, False),
        ("4 modos: libre, contrarreloj, muerte súbita, Simón Dice.", 0, False),
        ("Seed determinista (xorshift32) → secuencias reproducibles.", 0, False),
        ("Servidor web embebido + OTA A/B + Wi-Fi STA/AP.", 0, False),
        ("Hardware: OLED SSD1306/SH1106, buzzer PWM, batería ADC.", 0, False),
    ])

    # 3. Arquitectura + diagrama de bloques
    slide_bullets(prs, "Arquitectura dual-core (FreeRTOS SMP)", [
        ("RP2040 (2 núcleos) + CYW43439 (Wi-Fi); FreeRTOS SMP.", 0, False),
        ("game_task (Core 0, 2048 B, IDLE+2): juego, IRQ, LEDs, Flash, watchdog.", 0, False),
        ("network_task (Core 1, 4096 B, IDLE+3): Wi-Fi, web, display, batería, watchdog.", 0, False),
        ("Comunicación: critical_section + queue_t.", 0, False),
        ("Boot: watchdog → shared_state_init() → xTaskCreate ×2 → vTaskStartScheduler().", 0, False),
    ], img="Diagrama de bloques.png")

    # 4. Máquina de estados + flujo del juego
    slide_bullets(prs, "Mecanismo de juego — máquina de estados", [
        ("game::run() (src/game_logic.cpp:536-700) nunca retorna.", 0, False),
        ("IDLE → COUNTDOWN → RUNNING → EVALUACIÓN → IDLE (+ SIMON_SAYS).", 0, False),
        ("IDLE → COUNTDOWN  :570-575  (cualquier botón)", 1, True),
        ("COUNTDOWN → RUNNING  :386-397", 1, True),
        ("RUNNING → EVALUACIÓN  :632-644 / :648-651", 1, True),
        ("EVALUACIÓN → IDLE  :693-695", 1, True),
        ("Drenado eventos web  :541-565", 1, True),
    ], img="Flujo del juego (Core 0).png")

    # 5. Matemática del timing (tabla)
    slide_table(prs, "Mecanismo de juego — matemática del timing",
        ["Paso", "Código", "Detalle"],
        [
            ("Intervalo entre estímulos", ":610-611", "min + next_range(max − min + 1) → uniforme en [min, max]"),
            ("t0 al encender el LED", ":620", "instante de referencia"),
            ("reaction_us", ":635", "g_button_ts_us − t0: resta SIN signo → módulo 2³² absorbe el wrap"),
            ("tolerancia", ":623-626", "1000 / 500 / 250 ms; FREE_PLAY = UINT32_MAX"),
            ("fin por tiempo (TIMED)", ":654-655", "(time_us_32() − g_round_start_us) > round_seconds·1e6"),
            ("timestamp en IRQ", ":167-185", "capturado en la IRQ del PIO, jitter mínimo"),
        ],
        [3.1, 1.5, 7.33],
    )

    # 6. xorshift32 (algoritmo)
    slide_bullets(prs, "Seed — xorshift32 (el algoritmo)", [
        ("Marsaglia (2003) — src/game_logic.cpp:48-55", 0, False),
        ("x ^= x << 13u;   x ^= x >> 17u;   x ^= x << 5u;", 1, True),
        ("Linealidad sobre GF(2): XOR = suma módulo 2; cada shift es una matriz 32×32.", 0, False),
        ("Transición completa: T = (I+L⁵)(I+R¹⁷)(I+L¹³).", 0, False),
        ("Periodo máximo 2³²−1 (polinomio primitivo en GF(2)).", 0, False),
        ("No es criptográfico (lineal, predecible).", 0, False),
    ])

    # 7. Propiedades matemáticas
    slide_bullets(prs, "Seed — propiedades matemáticas", [
        ("Estado 0 = punto fijo → set_seed(0) usa 0x9E3779B9 = ⌊2³²/φ⌋ (razón áurea).", 0, False),
        ("include/game_logic.hpp:45", 1, True),
        ("Sesgo del módulo :58-61 → next_range(n) = next() % n; nunca emite 0.", 0, False),
        ("para n=5 (LED_COUNT): 2³²−1 = 5·858 993 459 → sesgo exacto 0.", 1, False),
        ("Determinismo: misma semilla → misma secuencia (reproducible).", 0, False),
        ("Siembra :406 → seed=0 = aleatorio (get_rand_32); seed≠0 = determinista.", 0, False),
    ])

    # 8. Simón Dice
    slide_bullets(prs, "Simón Dice", [
        ("start_round() inicia con longitud 1  :409-411", 0, True),
        ("simon_show_sequence :465-474 → muestra TODA la secuencia.", 0, True),
        ("simon_wait_input :477-498 → valida elemento a elemento: btn==expected.", 0, True),
        ("Acertar todo → crece hasta SIMON_MAX_SEQ 64  :522-528", 0, True),
        ("Fallar → game_over + reinicio a longitud 1  :511-519", 0, True),
        ("Secuencia determinista según la semilla.", 0, False),
    ])

    # 9. Precisión
    slide_bullets(prs, "Precisión y timing (por qué µs)", [
        ("time_us_32(): contador hardware de 1 µs, wrap cada 2³² µs ≈ 71,58 min.", 0, False),
        ("Timestamp en la IRQ del PIO (no en el bucle) → jitter mínimo.", 0, False),
        ("La resta sin signo absorbe el wrap sin código especial.", 0, False),
        ("Debounce por PIO: una SM por botón, lockout ~5 ms, sin CPU.", 0, False),
    ])

    # 10. Red + OTA + flujo de red
    slide_bullets(prs, "Red + OTA", [
        ("Servidor web raw lwIP (request[1024], g_body[8192]).", 0, False),
        ("Wi-Fi STA + respaldo AP (STA_MAX_RETRIES 3, backoff 1→15 s).", 0, False),
        ("Captive portal (DNS) + DHCP (DORA) + mDNS picoreflex.local.", 0, False),
        ("Auth PIN: WEB_PIN 1234, cookie/Bearer, rate-limit 60 req/s + 5 login/s.", 0, False),
        ("OTA A/B: bootloader + app + staging + CRC32 + rollback.", 0, False),
    ], img="Flujo de red (Core 1).png")

    # 11. Hardware + esquemático
    slide_bullets(prs, "Hardware", [
        ("Display OLED SSD1306/SH1106 (I2C1, 0x3C/0x3D, runtime persistido).", 0, False),
        ("Debounce por PIO (button_debounce.pio, 26 instr, 1 SM/botón).", 0, False),
        ("Buzzer PWM (GPIO16, f = clk_sys / (clkdiv·(wrap+1)), duty 50 %).", 0, False),
        ("Batería ADC (GPIO26/ADC0, divisor 10 kΩ/22 kΩ, curva 4200/3700/3000 mV).", 0, False),
        ("Watchdog hardware 8000 ms (ambos núcleos refrescan).", 0, False),
    ], img="Esquemático.png")

    # 12. Cierre
    slide_bullets(prs, "Cierre — qué van a preguntar", [
        ("¿Por qué resta sin signo? → módulo 2³² absorbe el wrap del contador.", 0, False),
        ("¿Por qué 13/17/5? → periodo máximo (polinomio primitivo en GF(2)).", 0, False),
        ("¿Semilla 0? → punto fijo; se reemplaza por 0x9E3779B9.", 0, False),
        ("¿Reproducible? → misma semilla = misma secuencia de LEDs/Simón.", 0, False),
        ("Lo más importante: mecanismo de juego (estados + timing) y seed (xorshift32).", 0, False),
    ])

    prs.save(OUT)
    print("OK ->", OUT)
    print("FILMINAS:", len(prs.slides._sldIdLst))


if __name__ == "__main__":
    build()

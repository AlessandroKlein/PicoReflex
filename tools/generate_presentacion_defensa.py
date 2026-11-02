# -*- coding: utf-8 -*-
"""Genera Presentacion_Defensa_PicoReflex.pptx (defensa del trabajo).

Contenido espejo de docs/estudio/presentacion-defensa.md. Usa python-pptx.
Ejecutar con el Python del entorno que tenga python-pptx instalado.
"""

import os
from pptx import Presentation
from pptx.util import Inches, Pt
from pptx.dml.color import RGBColor

BASE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(BASE, "Presentacion_Defensa_PicoReflex.pptx")

DARK = RGBColor(0x1F, 0x2A, 0x44)
GREY = RGBColor(0x55, 0x5A, 0x66)


def _set_16x9(prs):
    prs.slide_width = Inches(13.333)
    prs.slide_height = Inches(7.5)


def _style_title(slide, text):
    title = slide.shapes.title
    title.text = text
    for run in title.text_frame.paragraphs[0].runs:
        run.font.size = Pt(30)
        run.font.bold = True
        run.font.color.rgb = DARK


def add_title_slide(prs, title, lines):
    slide = prs.slides.add_slide(prs.slide_layouts[6])  # blank
    tb = slide.shapes.add_textbox(Inches(0.8), Inches(2.2), Inches(11.7), Inches(2.6))
    tf = tb.text_frame
    tf.word_wrap = True
    p = tf.paragraphs[0]
    run = p.add_run()
    run.text = title
    run.font.size = Pt(44)
    run.font.bold = True
    run.font.color.rgb = DARK
    sub = slide.shapes.add_textbox(Inches(0.8), Inches(4.4), Inches(11.7), Inches(2.0))
    stf = sub.text_frame
    stf.word_wrap = True
    for i, line in enumerate(lines):
        p = stf.paragraphs[0] if i == 0 else stf.add_paragraph()
        run = p.add_run()
        run.text = line
        run.font.size = Pt(20)
        run.font.color.rgb = GREY
    return slide


def add_bullet_slide(prs, title, items):
    """items: lista de (texto, nivel, mono)."""
    slide = prs.slides.add_slide(prs.slide_layouts[1])
    _style_title(slide, title)
    body = slide.placeholders[1]
    tf = body.text_frame
    tf.word_wrap = True
    tf.clear()
    for i, (text, level, mono) in enumerate(items):
        p = tf.paragraphs[0] if i == 0 else tf.add_paragraph()
        p.level = level
        run = p.add_run()
        run.text = text
        run.font.size = Pt(17 if level == 0 else 15)
        run.font.color.rgb = DARK if level == 0 else GREY
        if mono:
            run.font.name = "Consolas"
            run.font.size = Pt(15)
    return slide


def build():
    prs = Presentation()
    _set_16x9(prs)

    # 1. Portada
    add_title_slide(
        prs,
        "PicoReflex — Juego de reacción\nen Raspberry Pi Pico W",
        [
            "Defensa de Trabajo Final · Microprocesadores",
            "Tecnicatura en Automatización y Robótica · UNL · Año 2026",
            "Autor: Alessandro Klein · Firmware v0.9.7 · Pico SDK 2.3.0 · FreeRTOS SMP",
        ],
    )

    # 2. Qué es
    add_bullet_slide(prs, "¿Qué es PicoReflex?", [
        ("Dispositivo de entrenamiento de reflejos: 5 LEDs + 5 pulsadores (mismo índice).", 0, False),
        ("Mide el tiempo de reacción en microsegundos (time_us_32() en la IRQ del PIO).", 0, False),
        ("4 modos: libre, contrarreloj, muerte súbita, Simón Dice.", 0, False),
        ("Seed determinista (xorshift32) → secuencias reproducibles.", 0, False),
        ("Servidor web embebido + OTA A/B + Wi-Fi STA/AP.", 0, False),
        ("Hardware: OLED SSD1306/SH1106, buzzer PWM, batería ADC.", 0, False),
    ])

    # 3. Arquitectura
    add_bullet_slide(prs, "Arquitectura dual-core (FreeRTOS SMP)", [
        ("RP2040 (2 núcleos) + CYW43439 (Wi-Fi); FreeRTOS SMP sobre ambos núcleos.", 0, False),
        ("game_task (Core 0, 2048 B, IDLE+2): juego, IRQ, LEDs, Flash, watchdog.", 0, False),
        ("network_task (Core 1, 4096 B, IDLE+3): Wi-Fi, web, display, batería, watchdog.", 0, False),
        ("Comunicación: critical_section (estado) + queue_t (eventos Core1→Core0).", 0, False),
        ("Boot: watchdog → shared_state_init() → game::init() → xTaskCreate ×2 → vTaskStartScheduler().", 0, False),
    ])

    # 4. Máquina de estados
    add_bullet_slide(prs, "Mecanismo de juego — máquina de estados", [
        ("game::run() (src/game_logic.cpp:536-700) nunca retorna.", 0, False),
        ("IDLE → COUNTDOWN → RUNNING → EVALUACIÓN → IDLE (+ rama SIMON_SAYS).", 0, False),
        ("IDLE → COUNTDOWN  :570-575  (cualquier botón)", 1, True),
        ("COUNTDOWN → RUNNING  :386-397  (cuenta regresiva)", 1, True),
        ("RUNNING → EVALUACIÓN  :632-644 / :648-651  (pulsación o timeout)", 1, True),
        ("EVALUACIÓN → IDLE  :693-695  (muerte súbita + fallo)", 1, True),
        ("Drenado de eventos web  :541-565  (START/STOP/CONFIG)", 1, True),
    ])

    # 5. Matemática del timing
    add_bullet_slide(prs, "Mecanismo de juego — matemática del timing", [
        ("delay_ms uniforme en [min, max]  :610-611", 0, True),
        ("  min + next_range(max − min + 1)  (el +1 es el off-by-one)", 1, False),
        ("t0 capturado al encender el LED  :620", 0, True),
        ("reaction_us = g_button_ts_us − t0  :635  → resta SIN signo (módulo 2³²)", 0, True),
        ("  absorbe el wrap del contador sin código especial", 1, False),
        ("tolerance_us  :623-626  → 1000 / 500 / 250 ms; FREE_PLAY = UINT32_MAX", 0, True),
        ("TIMED: fin por round_seconds  :654-655", 0, True),
        ("Timestamp capturado en la IRQ del PIO  :167-185  (jitter mínimo)", 0, True),
    ])

    # 6. xorshift32 (algoritmo)
    add_bullet_slide(prs, "Seed — xorshift32 (el algoritmo)", [
        ("Marsaglia (2003) — src/game_logic.cpp:48-55", 0, False),
        ("x ^= x << 13u;   x ^= x >> 17u;   x ^= x << 5u;", 0, True),
        ("Linealidad sobre GF(2): XOR = suma módulo 2; cada shift es una matriz 32×32.", 0, False),
        ("Transición completa: T = (I+L⁵)(I+R¹⁷)(I+L¹³).", 0, False),
        ("Periodo máximo 2³²−1 (polinomio primitivo en GF(2)).", 0, False),
        ("No es criptográfico (lineal, predecible).", 0, False),
    ])

    # 7. Propiedades matemáticas
    add_bullet_slide(prs, "Seed — propiedades matemáticas", [
        ("Estado 0 = punto fijo → set_seed(0) usa 0x9E3779B9 = ⌊2³²/φ⌋ (razón áurea).", 0, False),
        ("  include/game_logic.hpp:45", 1, True),
        ("Sesgo del módulo :58-61 → next_range(n) = next() % n; nunca emite 0.", 0, False),
        ("  para n=5 (LED_COUNT): 2³²−1 = 5·858 993 459 → sesgo exacto 0.", 1, False),
        ("Determinismo: misma semilla → misma secuencia (reproducible).", 0, False),
        ("Siembra :406 → seed=0 = aleatorio (get_rand_32); seed≠0 = determinista.", 0, False),
    ])

    # 8. Simón Dice
    add_bullet_slide(prs, "Simón Dice", [
        ("start_round() inicia con longitud 1  :409-411", 0, True),
        ("simon_show_sequence :465-474 → muestra TODA la secuencia (LED + tono).", 0, True),
        ("simon_wait_input :477-498 → valida elemento a elemento: btn == expected.", 0, True),
        ("Acertar todo → crece hasta SIMON_MAX_SEQ 64  :522-528", 0, True),
        ("Fallar → game_over + reinicio a longitud 1  :511-519", 0, True),
        ("Secuencia determinista según la semilla.", 0, False),
    ])

    # 9. Precisión
    add_bullet_slide(prs, "Precisión y timing (por qué µs)", [
        ("time_us_32(): contador hardware de 1 µs, wrap cada 2³² µs ≈ 71,58 min.", 0, False),
        ("Timestamp en la IRQ del PIO (no en el bucle) → jitter mínimo.", 0, False),
        ("La resta sin signo absorbe el wrap sin código especial.", 0, False),
        ("Debounce por PIO: una SM por botón, lockout ~5 ms, sin CPU.", 0, False),
    ])

    # 10. Red + OTA
    add_bullet_slide(prs, "Red + OTA", [
        ("Servidor web raw lwIP (request[1024], g_body[8192], html_escape).", 0, False),
        ("Wi-Fi STA + respaldo AP (reconexión: STA_MAX_RETRIES 3, backoff 1→15 s).", 0, False),
        ("Captive portal (DNS :53) + DHCP (DORA :67) + mDNS picoreflex.local (UDP 5353).", 0, False),
        ("Auth PIN: WEB_PIN 1234, cookie/Bearer, rate-limit 60 req/s + 5 login/s.", 0, False),
        ("OTA A/B: bootloader 0x10000000 + app 0x10010000 + staging + CRC32 + rollback.", 0, False),
    ])

    # 11. Hardware
    add_bullet_slide(prs, "Hardware", [
        ("Display OLED SSD1306/SH1106 (I2C1, 0x3C/0x3D, selección runtime persistida).", 0, False),
        ("Debounce por PIO (button_debounce.pio, 26 instrucciones, 1 SM/botón).", 0, False),
        ("Buzzer PWM (GPIO16, f = clk_sys / (clkdiv·(wrap+1)), duty 50 %).", 0, False),
        ("Batería ADC (GPIO26/ADC0, divisor 10 kΩ/22 kΩ, curva 4200/3700/3000 mV).", 0, False),
        ("Watchdog hardware 8000 ms (ambos núcleos refrescan).", 0, False),
    ])

    # 12. Cierre
    add_bullet_slide(prs, "Cierre — qué van a preguntar", [
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

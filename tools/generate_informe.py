# -*- coding: utf-8 -*-
"""Genera Informe_PicoReflex.docx a partir del contenido consolidado del proyecto."""
from docx import Document
from docx.shared import Pt, RGBColor
from docx.enum.text import WD_ALIGN_PARAGRAPH
from docx.oxml.ns import qn


def set_mono(run):
    run.font.name = "Consolas"
    run._element.rPr.rFonts.set(qn("w:cs"), "Consolas")


def code_block(doc, text):
    for line in text.split("\n"):
        p = doc.add_paragraph()
        p.paragraph_format.space_before = Pt(0)
        p.paragraph_format.space_after = Pt(0)
        r = p.add_run(line)
        set_mono(r)
        r.font.size = Pt(9)


doc = Document()

# Estilo base
style = doc.styles["Normal"]
style.font.name = "Calibri"
style.font.size = Pt(11)

# ---- Título ----
t = doc.add_heading("PicoReflex — Informe Técnico", level=0)
sub = doc.add_paragraph()
sub.alignment = WD_ALIGN_PARAGRAPH.CENTER
r = sub.add_run("Juego de reflejos sobre Raspberry Pi Pico W\n")
r.bold = True
doc.add_paragraph(
    "Materia: Microprocesadores — Tecnicatura en Automatización y Robótica · "
    "Universidad Nacional del Litoral · Año 2026"
).alignment = WD_ALIGN_PARAGRAPH.CENTER

# ---- 1. En qué consiste el proyecto ----
doc.add_heading("1. En qué consiste el proyecto", level=1)
doc.add_paragraph(
    "PicoReflex es un dispositivo de entrenamiento de reflejos (juego de reacción) "
    "construido sobre una Raspberry Pi Pico W. El sistema enciende de forma aleatoria "
    "uno de cinco LEDs y mide, con resolución de microsegundos, el tiempo que tarda el "
    "jugador en pulsar el botón asociado a ese LED."
)
doc.add_paragraph(
    "Incluye tres modos de juego (entrenamiento libre, contrarreloj y muerte súbita) y "
    "tres niveles de dificultad (tolerancia de 1000 / 500 / 250 ms). La secuencia de LEDs "
    "puede generarse con una semilla fija (reproducible) o aleatoria. Todo el dispositivo "
    "es configurable y supervisable desde un servidor web embebido (Wi-Fi, lwIP) sin "
    "recompilar el firmware, y soporta actualización de firmware por aire (OTA A/B con "
    "rollback)."
)
doc.add_paragraph(
    "El firmware usa FreeRTOS en dos núcleos: el núcleo 0 ejecuta la lógica del juego "
    "(máquina de estados, temporización por interrupción y PRNG) y el núcleo 1 gestiona "
    "la red (Wi-Fi/AP), el servidor web y el refresco del display OLED."
)

# ---- 2. Qué lleva el proyecto (componentes) ----
doc.add_heading("2. Componentes del proyecto", level=1)
doc.add_paragraph("El hardware se compone de los siguientes elementos:")

components = [
    ("Raspberry Pi Pico W", "RP2040 (doble núcleo Cortex-M0+) + chip inalámbrico CYW43 (Wi-Fi 2.4 GHz)."),
    ("5 LEDs indicadores", "GPIO 2–6, con resistencia limitadora de 330 Ω en serie (ánodo a GPIO, cátodo a GND)."),
    ("5 pulsadores", "GPIO 7–11, entrada digital con pull-up interno (~50 kΩ) y debounce por PIO (flanco de bajada)."),
    ("Display OLED 128×64", "I2C1 (SDA=GPIO14, SCL=GPIO15, 400 kHz, dirección 0x3C/0x3D). Compatible SSD1306 (0.96\") y SH1106 (1.3\")."),
    ("Buzzer pasivo", "GPIO16, tonos por PWM (acierto, fallo, cuenta regresiva y game over)."),
    ("Medición de batería", "GPIO26 (ADC0) con divisor resistivo externo 10 kΩ / 22 kΩ para LiPo 1S (3.0–4.2 V)."),
    ("LED onboard", "Controlado por el CYW43, indica el estado de la conexión Wi-Fi."),
    ("Flash interno (2 MB)", "Persistencia de configuración + partición OTA A/B con bootloader de segundo nivel."),
]
for name, desc in components:
    p = doc.add_paragraph(style="List Bullet")
    r = p.add_run(name + ": ")
    r.bold = True
    p.add_run(desc)

# ---- 3. Esquema de conexión ----
doc.add_heading("3. Esquema de conexión", level=1)

code_block(doc, """  LED 1 ──[330 Ω]── GPIO2          GPIO7  ──[P1]── GND
  LED 2 ──[330 Ω]── GPIO3          GPIO8  ──[P2]── GND
  LED 3 ──[330 Ω]── GPIO4          GPIO9  ──[P3]── GND
  LED 4 ──[330 Ω]── GPIO5          GPIO10 ──[P4]── GND
  LED 5 ──[330 Ω]── GPIO6          GPIO11 ──[P5]── GND

  OLED SDA ── GPIO14 (I2C1)        GPIO15 ── OLED SCL
        └──[4.7 kΩ]── 3V3

  Buzzer ──[1 kΩ]── GPIO16 (PWM)

  VBAT ──[10 kΩ]──┬─ GPIO26 (ADC0)
                  └─[22 kΩ]── GND""")

doc.add_paragraph()

table = doc.add_table(rows=1, cols=3)
table.style = "Light Grid Accent 1"
hdr = table.rows[0].cells
for i, txt in enumerate(["Periférico", "Pin Pico W", "Detalle"]):
    hdr[i].paragraphs[0].add_run(txt).bold = True

rows = [
    ("LED 1–5", "GPIO 2–6", "Salida digital (alto = encendido), R 330 Ω"),
    ("Pulsador 1–5", "GPIO 7–11", "Entrada pull-up interno, activo en BAJO"),
    ("OLED SDA / SCL", "GPIO 14 / 15", "I2C1, 400 kHz, 0x3C/0x3D, pull-up 4.7 kΩ a 3V3"),
    ("Buzzer pasivo", "GPIO 16", "PWM, driver a transistor recomendado"),
    ("Batería", "GPIO 26 (ADC0)", "Divisor externo R1=10 kΩ / R2=22 kΩ"),
    ("LED onboard (Wi-Fi)", "CYW43", "Integrado en el módulo"),
]
for a, b, c in rows:
    cells = table.add_row().cells
    cells[0].text = a
    cells[1].text = b
    cells[2].text = c

p = doc.add_paragraph()
r = p.add_run("Nota de alimentación: ")
r.bold = True
p.add_run(
    "el display OLED es de 3.3 V. Conectar VCC → 3V3(OUT) (pin 36) y GND → GND; "
    "nunca a VBUS (5 V), que puede dañar el módulo y llevar 5 V a los GPIO14/15."
)

# ---- 4. Problemas y soluciones ----
doc.add_heading("4. Problemas encontrados y cómo se resolvieron", level=1)

problems = [
    ("GPIO29 ocupado en la Pico W",
     "El ADC3 (GPIO29) está reservado por el SPI del Wi-Fi y el divisor interno VSYS/3, "
     "por lo que no podía medirse la batería ahí. Se midió por GPIO26 (ADC0) con un "
     "divisor externo 10 kΩ/22 kΩ, evitando el conflicto."),
    ("Secuencia reproducible (semilla fija)",
     "La API pico_rand no permite fijar la semilla (se auto-siembra). Se implementó un "
     "generador xorshift32 propio para lograr una secuencia de LEDs idéntica en cada "
     "reinicio con una semilla dada."),
    ("Escritura segura en Flash con dos núcleos",
     "Escribir Flash mientras se ejecuta código desde Flash exige coordinar ambos núcleos. "
     "Se usó flash_safe_execute() / flash_safe_execute_core_init() del SDK."),
    ("Reinicio al guardar configuración (pasaba a modo AP)",
     "flash_safe_execute() corría en el Core 1 (red) bajo FreeRTOS SMP, donde __wfe() no "
     "tiene wakeup periódico: el timeout de la zona segura nunca se re-evalúa, se colgaba, "
     "el watchdog (8 s) reiniciaba con el Flash a medio borrar, el checksum fallaba y hacía "
     "reset de fábrica (sin credenciales → AP). Se fijó la afinidad de tareas: game_task → "
     "Core 0 (escribe Flash) y network_task → Core 1."),
    ("Cuelgue en cyw43_arch_init()",
     "Con pico_cyw43_arch_lwip_sys_freertos, cyw43_arch_init() crea la tarea tcpip con "
     "TCPIP_THREAD_STACKSIZE, que estaba indefinido (default 0) → la tarea nunca se creaba "
     "y el arranque bloqueaba. Se definió TCPIP_THREAD_STACKSIZE 1024 y los mbox/recvmbox "
     "en lwipopts.h."),
    ("USB 'dispositivo no reconocido' tras el bootloader",
     "El bootloader saltaba a la app con las IRQ enmascaradas (PRIMASK=1), así que la IRQ "
     "USB no podía correr durante stdio_init_all() y el host no recibía descriptor válido. "
     "jump_to_app() ahora replica un reset limpio (limpia NVIC, apaga SysTick, fija VTOR y "
     "salta con cpsie i)."),
    ("Enlace fallaba en Windows (memmap_default.incl)",
     "El g++ ARM de Windows descarta las flags -L cuando los objetos van por @rsp (Ninja), "
     "así que ld no resolvía los INCLUDE del linker script del SDK. Se genera un linker "
     "script autocontenido (tools/generate_linker_script.py) e inyectado por -T."),
    ("PANIC de Wi-Fi (MEMP_SYS_TIMEOUT vacío)",
     "El default de lwIP (MEMP_NUM_SYS_TIMEOUT = 9) era insuficiente para mDNS + keepalive "
     "+ DHCP, y el pool se agotaba al asociarse. Se subió a 32 en lwipopts.h."),
    ("Reconexión en caliente AP→STA fallaba",
     "Al conmutar del AP al STA sin reiniciar, el join se lanzaba con la radio aún cerrando "
     "el modo anterior. Se añadió un delay de asentamiento de 200 ms antes de network_up()."),
    ("El AP conectaba y se desconectaba (sin IP)",
     "El CYW43 no trae servidor DHCP en modo AP. Se integró un servidor DHCP propio "
     "(dhcpserver.c, MIT) que asigna 192.168.4.16–23 a los clientes."),
    ("Reinicio al escanear/guardar Wi-Fi (stack overflow)",
     "El Core 1 (servidor web) corría con 2 KB de stack y desbordaba (hard fault). Se "
     "amplió a 4 KB con PICO_CORE1_STACK_SIZE=0x1000."),
    ("SSID corrupto al guardar",
     "url_decode() copiaba hasta el final de la query, dejando 'ssid&pass=...' dentro del "
     "SSID. Se corrigió para que corte en el separador '&'."),
    ("Display OLED en negro pese a 'detectado en 0x3C'",
     "La detección I2C solo confirma ACK (SSD1306 y SH1106 responden igual en 0x3C). El "
     "init mandaba una sola fuente de tensión por chip: si el panel real era el otro "
     "controlador, su comando de bomba se ignoraba y el panel no encendía. Ambas secuencias "
     "ahora mandan charge-pump (0x8D,0x14) y DC-DC (0xAD,0x8B), con pre-carga/VCOM "
     "corregidos."),
    ("Display OLED definitivamente en negro (conclusión de hardware)",
     "Con el auto-test 'Display All ON' (0xA5) durante 1 s, el chip devuelve ACK pero el "
     "panel no emite destello: el controlador está vivo pero el panel no enciende. Es un "
     "defecto físico del módulo (charge-pump/DC-DC muerto o flex agrietado), no reparable "
     "por software; se documentó el método de diagnóstico para separar este caso del de "
     "RAM/direccionamiento."),
]
for i, (title, body) in enumerate(problems, 1):
    p = doc.add_paragraph()
    r = p.add_run(f"{i}. {title}")
    r.bold = True
    doc.add_paragraph(body).paragraph_format.space_after = Pt(8)

doc.save("Informe_PicoReflex.docx")
print("OK -> Informe_PicoReflex.docx")

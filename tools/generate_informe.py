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

# Estilo base: cuerpo en Inter (APA 7.ª ed.).
style = doc.styles["Normal"]
style.font.name = "Inter"
style.font.size = Pt(11)
# Cuerpo uniforme en Inter: forzamos también w:cs (w:ascii/hAnsi ya los fija .name).
style._element.rPr.rFonts.set(qn("w:cs"), "Inter")


def add_reference(doc, text):
    """Agrega una referencia APA 7 con sangría francesa (0.5 in)."""
    p = doc.add_paragraph()
    p.paragraph_format.left_indent = Pt(36)
    p.paragraph_format.first_line_indent = Pt(-36)
    p.paragraph_format.space_after = Pt(6)
    p.add_run(text)
    return p


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
    "construido sobre una Raspberry Pi Pico W (Raspberry Pi Ltd, 2023). El sistema "
    "enciende de forma aleatoria uno de cinco LEDs y mide, con resolución de "
    "microsegundos, el tiempo que tarda el jugador en pulsar el botón asociado a ese LED."
)
doc.add_paragraph(
    "Incluye cuatro modos de juego (entrenamiento libre, contrarreloj, muerte súbita y "
    "Simón Dice) y tres niveles de dificultad (tolerancia de 1000 / 500 / 250 ms). La "
    "secuencia de LEDs puede generarse con una semilla fija (reproducible) o aleatoria. "
    "Todo el dispositivo es configurable y supervisable desde un servidor web embebido "
    "(Wi-Fi, lwIP) sin recompilar el firmware, y soporta actualización de firmware por "
    "aire (OTA A/B con rollback)."
)
doc.add_paragraph(
    "El firmware usa FreeRTOS (Real Time Engineers Ltd, n.d.) en dos núcleos: el núcleo 0 "
    "ejecuta la lógica del juego (máquina de estados, temporización por interrupción y "
    "PRNG) y el núcleo 1 gestiona la red (Wi-Fi/AP), el servidor web y el refresco del "
    "display OLED."
)
doc.add_paragraph(
    "El modo de juego, la dificultad y la semilla se persisten en Flash (formato v7), de "
    "modo que al reiniciar se recupera la configuración anterior en lugar de volver a los "
    "valores por defecto. El controlador del display OLED —SSD1306 (0.96\") o SH1106 "
    "(1.3\")— se selecciona en runtime desde la interfaz web y también se persiste. Desde "
    "el estado de reposo, la ronda arranca pulsando cualquiera de los cinco botones."
)

# ---- 2. Qué lleva el proyecto (componentes) ----
doc.add_heading("2. Componentes del proyecto", level=1)
doc.add_paragraph("El hardware se compone de los siguientes elementos:")

components = [
    ("Raspberry Pi Pico W", "RP2040 (doble núcleo Cortex-M0+, Raspberry Pi Ltd, 2024) + chip inalámbrico CYW43 (Wi-Fi 2.4 GHz, Infineon Technologies, n.d.)."),
    ("5 LEDs indicadores", "GPIO 2–6, con resistencia limitadora de 330 Ω en serie (ánodo a GPIO, cátodo a GND)."),
    ("5 pulsadores", "GPIO 7–11, entrada digital con pull-up interno (~50 kΩ) y debounce por PIO (flanco de bajada)."),
    ("Display OLED 128×64", "I2C1 (SDA=GPIO14, SCL=GPIO15, 400 kHz, dirección 0x3C/0x3D). Compatible SSD1306 (0.96\", Solomon Systech, 2008) y SH1106 (1.3\", Sino Wealth, n.d.)."),
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

# Esquema circuital completo (PNG rasterizado a partir de docs/img/Schematic.svg).
doc.add_picture("docs/img/Schematic.png", width=Pt(450))
doc.paragraphs[-1].alignment = WD_ALIGN_PARAGRAPH.CENTER
cap = doc.add_paragraph()
cap.alignment = WD_ALIGN_PARAGRAPH.CENTER
_cr = cap.add_run("Figura 1. Esquema circuital del PicoReflex (Schematic.svg).")
_cr.italic = True
_cr.font.size = Pt(9)

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
    "el fabricante del módulo OLED indica un rango de 3.3–5 V, pero se recomienda "
    "VCC → 3V3(OUT) (pin 36) y GND → GND: alimentar a 5 V (VBUS, pin 40) puede llevar "
    "5 V a los GPIO14/15 (I2C) a través de los pull-up y dañar el RP2040."
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
    ("Modo y dificultad volvían a los valores por defecto al reiniciar",
     "Solo se persistían el nombre, el ranking y las credenciales Wi-Fi. Se amplió el "
     "formato de Flash (PersistentData v6→v7) con game_mode, game_difficulty, seed, "
     "round_seconds y stimulus_min/max, restaurados al arrancar; además, el disparador de "
     "inicio acepta ahora cualquiera de los cinco botones desde el estado de reposo."),
]
for i, (title, body) in enumerate(problems, 1):
    p = doc.add_paragraph()
    r = p.add_run(f"{i}. {title}")
    r.bold = True
    doc.add_paragraph(body).paragraph_format.space_after = Pt(8)

# ---- Referencias (APA 7.ª ed.) ----
doc.add_heading("Referencias", level=1)
referencias = [
    "Dunkels, A. (2001). *lwIP — A lightweight TCP/IP stack*. https://savannah.nongnu.org/projects/lwip/",
    "Infineon Technologies. (n.d.). *CYW43439: Single-chip IEEE 802.11 b/g/n wireless LAN and Bluetooth 5.1 combo*. https://www.infineon.com/",
    "Raspberry Pi Ltd. (2023). *Raspberry Pi Pico W datasheet*. https://datasheets.raspberrypi.com/picow/pico-w-datasheet.pdf",
    "Raspberry Pi Ltd. (2024). *RP2040 datasheet: A microcontroller by Raspberry Pi*. https://datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf",
    "Raspberry Pi Ltd. (2025). *Pico SDK documentation* (versión 2.3.0). https://www.raspberrypi.com/documentation/pico-sdk/",
    "Raspberry Pi Ltd. (2025). *Pico SDK: hardware_pwm* (PWM para buzzer pasivo). https://www.raspberrypi.com/documentation/pico-sdk/hardware.html#hardware_pwm",
    "Real Time Engineers Ltd. (n.d.). *FreeRTOS: Real-time operating system for microcontrollers*. https://www.freertos.org/",
    "Sino Wealth. (n.d.). *SH1106: 132 × 64 dot matrix OLED/PLED segment/common driver with controller*. https://www.displayfuture.com/Display/datasheet/controller/SH1106.pdf",
    "Solomon Systech. (2008). *SSD1306: 128 × 64 dot matrix OLED/PLED segment/common driver with controller* (Rev. 2.2). https://cdn-shop.adafruit.com/datasheets/SSD1306.pdf",
]
for ref in referencias:
    add_reference(doc, ref)

# ---- Aclaración sobre el uso de inteligencia artificial ----
doc.add_heading("Aclaración sobre el uso de inteligencia artificial", level=1)
doc.add_paragraph(
    "Se deja constancia de que se utilizó una herramienta de inteligencia artificial "
    "como asistencia para el formato y la presentación del documento, así como para la "
    "revisión y corrección ortográfica y gramatical del texto. El contenido técnico, los "
    "cálculos, las ecuaciones y las comparaciones se presentan conforme al desarrollo del "
    "trabajo práctico y a los análisis realizados."
)

# ---- Enlaces ----
doc.add_heading("Enlaces", level=1)
enlaces = [
    ("Documentación (sitio MkDocs)", "https://alessandroklein.github.io/Docs/picoreflex/"),
    ("Repositorio del código", "https://github.com/AlessandroKlein/PicoReflex"),
    ("Repositorio de la documentación", "https://github.com/AlessandroKlein/Docs"),
]
for label, url in enlaces:
    p = doc.add_paragraph(style="List Bullet")
    r = p.add_run(label + ": ")
    r.bold = True
    p.add_run(url)

doc.save("Informe_PicoReflex.docx")
print("OK -> Informe_PicoReflex.docx")

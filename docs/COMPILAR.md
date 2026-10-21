# Compilar y Flashear — PicoReflex

## Requisitos

- [Raspberry Pi Pico VS Code Extension](https://marketplace.visualstudio.com/items?itemName=raspberry-pi.raspberry-pi-pico)
  (instala automáticamente el SDK, el toolchain `arm-none-eabi-gcc`, CMake, Ninja
  y `picotool` en `~/.pico-sdk`).
- Una Raspberry Pi **Pico W**.
- **FreeRTOS-Kernel** (obligatorio desde v0.5.0): el proyecto usa FreeRTOS SMP,
  que no viene incluido en el SDK. Clonarlo una vez:

  ```powershell
  git clone --branch V11.1.0 https://github.com/FreeRTOS/FreeRTOS-Kernel.git %USERPROFILE%\.pico-sdk\FreeRTOS-Kernel
  ```

  El `CMakeLists.txt` lo localiza vía `FreeRTOS_Kernel_import.cmake` (variable
  `FREERTOS_KERNEL_PATH`). Ver `docs/FREERTOS.md` para el detalle.

## Configurar las credenciales Wi-Fi (STA)

Hay dos formas de definir la red Wi-Fi:

1. **Por defecto (compilación):** editar `include/config.hpp` y definir
   `WIFI_SSID_DEFAULT` / `WIFI_PASSWORD_DEFAULT`.
2. **En caliente (recomendado):** desde la web, en la página `/wifi`, ingresar
   SSID y contraseña. Se persisten en Flash y el dispositivo se re-conecta solo;
   no hace falta recompilar.

Si la conexión a la red falla en 10 s, el dispositivo pasa automáticamente a
modo **Access Point** (`PicoW-ReactionGame` / `12345678`, IP `192.168.4.1`).

## Compilar

### Opción A — Visual Studio Code (recomendado)

1. Abrir la carpeta del proyecto en VS Code con la extensión Pico instalada.
2. En la barra inferior, verificar **Board = `pico_w`** y **SDK = 2.3.0**.
3. `Ctrl+Shift+P` → **Raspberry Pi Pico: Configure Project** → **Compile Project**.
4. Se genera `build/PicoReflex.uf2`.

### Opción B — Línea de comandos

```powershell
$env:PICO_SDK_PATH = "$env:USERPROFILE\.pico-sdk\sdk\2.3.0"
$env:PICO_TOOLCHAIN_PATH = "$env:USERPROFILE\.pico-sdk\toolchain\15_2_Rel1"
cmake -G Ninja -B build -S .
cmake --build build
```

> **Stack del Core 1 (importante):** el servidor web corre en el Core 1 y necesita
> **4 KB** de stack (`PICO_CORE1_STACK_SIZE=0x1000`). Ya está definido en
> `CMakeLists.txt` con `add_compile_definitions(PICO_CORE1_STACK_SIZE=0x1000)`; el
> valor por defecto del SDK es 2 KB y provocaba *hard fault* al reconectar/escanear.

## Flashear

1. Mantener pulsado **BOOTSEL** y conectar el USB (o reconectar). La Pico aparece
   como unidad de almacenamiento USB (`RPI-RP2`).
2. Copiar `build/PicoReflex.uf2` a la unidad, o usar:
   ```powershell
   picotool load build/PicoReflex.uf2 -f
   ```

## Usar

### Credenciales del Access Point (modo respaldo)

Cuando la Pico W no logra conectarse a la red STA, levanta su propio Access Point:

| Campo | Valor |
|---|---|
| **Red (SSID)** | `PicoW-ReactionGame` |
| **Seguridad** | **WPA2** (clave `12345678`) |
| **IP del panel** | `192.168.4.1` |
| **Canal** | `1` (fijo) |

> El AP usa **WPA2** con clave `12345678` y canal fijo 1. El **servidor DHCP
> integrado** (`src/dhcpserver.c`) asigna IPs `192.168.4.16`–`192.168.4.23` a los
> clientes; sin ese DHCP el teléfono se asociaba pero nunca recibía IP.

1. Conectar a la red `PicoW-ReactionGame` (si quedó en AP) o a la IP que se
   muestre en el display (modo STA).
2. Abrir `http://192.168.4.1` (AP) o la IP del dispositivo (STA). En modo AP,
   cualquier URL que abras redirige automáticamente a la página (captive portal).
3. Desde la web (páginas separadas): semilla (`/seed`), juego (`/game`), OLED
   (`/display`) y Wi-Fi (`/wifi`, con "Mostrar contraseña"), más los controles
   iniciar, detener y reiniciar.
4. Físicamente: **presionar dos botones a la vez** para iniciar la ronda; hay una
   **cuenta regresiva** configurable (por defecto 3 s) antes del primer LED.
   También se puede iniciar desde `/start`.
5. El **OLED se puede apagar** desde `/display` (toda la info sigue en la web).

### Solución de problemas del AP (no conecta)

1. **Forzar modo AP:** mantené presionado el **botón 0** mientras conectás la
   alimentación (o reiniciás). La Pico salta el intento de STA y arranca directo
   en `PicoW-ReactionGame`. Esto descarta el caso "quedó en STA".
2. **Ver el estado por USB:** conectá el USB (no BOOTSEL) y abrí un monitor serie
   a 115200 baud. El firmware imprime:
   - `[game] Boton 0 presionado al arrancar -> forzando modo AP`
   - `[web] Intentando STA: ...` o `[web] STA omitido (forzar AP)`
   - `[web] Modo AP: PicoW-ReactionGame / clave 12345678 / IP 192.168.4.1`
   - `[web] Conectado a STA, IP ...`
3. **Esperar:** si no se fuerza AP, la Pico intenta STA durante **10 s** antes de
   levantar el AP. Esperá ~12-15 s desde el encendido.
4. **Alimentación:** para probar, alimentá por **USB** (estable 5 V). Una batería
   débil o un *boost* insuficiente puede dejar el Wi-Fi sin corriente suficiente
   (el CYW43 necesita ~100-150 mA).
5. **LED onboard:** fijo = conectado a STA; parpadeo = modo AP; apagado = Wi-Fi
   sin inicializar.

## Depurar (printf por USB)

El programa usa `stdio` por USB. Conectar el USB (no en modo BOOTSEL) y abrir el
puerto serie (por ejemplo, PuTTY / `minicom`) a 115200 baud. Ver el estado de red
y las credenciales del AP en tiempo real.

## Regenerar la fuente 5x7 (opcional)

```powershell
python tools\gen_font.py   # regenera include/font5x7.hpp
```

### ¿Qué es la fuente 5x7?

Es la **tipografía de mapa de bits** que usa el display OLED (SSD1306) para
dibujar texto. Cada carácter ocupa una celda de **5 columnas × 7 filas** de
píxeles. Es la fuente clásica de los OLED monocromos: compacta, legible y fácil
de almacenar.

### ¿Por qué hay que "regenerarla"?

Los glifos (las letras/dígitos) están definidos **como ASCII-art** dentro de
`tools/gen_font.py` (cada carácter son 7 líneas de 5 caracteres, `#` = píxel
encendido). El firmware **no entiende ASCII-art**: necesita un array de bytes.
El script traduce ese dibujo a un array C++ (`include/font5x7.hpp`):

```
'0': [".###.", "#...#", "#..##", "#.#.#", "##..#", "#...#", ".###."]
  → {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}
```

Solo hay que ejecutar el script **si se modifica un glifo o se agrega un carácter
nuevo** (por ejemplo, un símbolo que falte). El archivo `font5x7.hpp` está marcado
como "NO EDITAR A MANO": cualquier cambio se hace en `gen_font.py` y se regenera.

### Cómo funciona el formato

- `FONT5X7[95][5]`: **95 glifos** (ASCII imprimible `0x20`..`0x7E`), **5 bytes cada
  uno**.
- Cada byte es **una columna** del carácter; el **bit 0 es la fila superior**
  (por eso el OLED dibuja "apaisado" los 5 bytes).
- Se indexa por `carácter - 0x20`: `FONT5X7['A' - 0x20]`.
- El driver (`src/display_driver.cpp`) recorre esos 5 bytes y setea/limpia los
  píxeles correspondientes en el framebuffer de 128×64.

### Referencia

- `tools/gen_font.py` — generador (ASCII-art → array C++).
- `include/font5x7.hpp` — salida generada (no editar a mano).
- `src/display_driver.cpp` — consumo de la fuente (`draw_char_5x7`).

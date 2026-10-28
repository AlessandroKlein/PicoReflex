# Compilar y Flashear — PicoReflex

## Requisitos

- [Raspberry Pi Pico VS Code Extension](https://marketplace.visualstudio.com/items?itemName=raspberry-pi.raspberry-pi-pico)
  (instala automáticamente el SDK, el toolchain `arm-none-eabi-gcc`, CMake, Ninja
  y `picotool` en `~/.pico-sdk`).
- Una Raspberry Pi **Pico W**.
- **Placa y toolchain:** `PICO_BOARD=pico_w` (fijado en `CMakeLists.txt`) con el
  toolchain oficial `arm-none-eabi-gcc` del **Pico SDK 2.3.0** (C/C++). No se usa
  Arduino ni MicroPython.
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
modo **Access Point** (`PicoW-ReactionGame`, IP `192.168.4.1`). La clave del AP
es **fija `12345678`** por defecto (igual para cualquier placa) y se puede cambiar
desde `/wifi` (ver "Credenciales del Access Point" más abajo).

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

> **Stack de la tarea de red (importante):** el servidor web corre en la tarea de
> red (`network_task`) y necesita **4 KB** de stack (`PICO_CORE1_STACK_SIZE=0x1000`).
> Ya está definido en `CMakeLists.txt` con
> `add_compile_definitions(PICO_CORE1_STACK_SIZE=0x1000)`; el valor por defecto
> del SDK es 2 KB y provocaba *hard fault* al reconectar/escanear.

## Opciones de compilación (macros)

El firmware se compila con el **Pico SDK oficial (C/C++)** y el toolchain
`arm-none-eabi-gcc` para la placa **Raspberry Pi Pico W** (`PICO_BOARD=pico_w`,
fijado en `CMakeLists.txt`). Estas macros seleccionan variantes de hardware y
seguridad (se definen por `#ifndef` en `include/config.hpp`, salvo `WEB_PIN`):

| Macro | Valor por defecto | Efecto |
|---|---|---|
| `PICOREFLEX_WS2812` | `0` | `1` = LEDs direccionables WS2812 (NeoPixel) en GPIO28 por PIO; `0` = 5 LEDs GPIO 2..6 |
| `PICOREFLEX_DISPLAY_CONTROLLER` | `0` | Solo fija el controlador **por defecto** (`0` = SSD1306, `1` = SH1106); se elige en runtime desde `/display` y se persiste en Flash |
| `WEB_PIN` | `"1234"` | PIN de autenticación del panel web (endpoints de control) |
| `WIFI_SSID_DEFAULT` / `WIFI_PASSWORD_DEFAULT` | `""` | Credenciales STA iniciales (editables desde `/wifi`) |

```powershell
# Ejemplo: activar WS2812 y cambiar el PIN
cmake -B build -S . -DCMAKE_CXX_FLAGS="-DPICOREFLEX_WS2812=1 -DWEB_PIN=\"9876\""
```

## Flashear

La primera vez (o tras un borrado total de la Flash) hay que grabar **dos**
imágenes: el **bootloader** de segundo nivel y la **aplicación**. A partir de ahí,
las actualizaciones se hacen **por aire** (ver
[Actualización por aire (OTA)](#actualización-por-aire-ota)), sin USB.

1. Mantener pulsado **BOOTSEL** y conectar el USB (o reconectar). La Pico aparece
   como unidad de almacenamiento USB (`RPI-RP2`).
2. Grabar el **bootloader** y luego la **aplicación** (en este orden):
   ```powershell
   picotool load build/PicoReflex_bootloader.uf2 -f
   picotool load build/PicoReflex.uf2 -f
   ```
   (O copiar cada `.uf2` a la unidad `RPI-RP2`, reiniciando en BOOTSEL entre uno
   y otro.)

## Actualización por aire (OTA)

El firmware se puede actualizar desde la web **sin USB**, con verificación de
integridad (CRC32) y *rollback* automático a la imagen anterior si la verificación
falla.

### Mapa de la Flash (2 MB)

| Offset      | Tamaño | Uso |
|---|---|---|
| `0x000000` | 64 KB   | Bootloader de segundo nivel |
| `0x010000` | 896 KB  | Slot de aplicación (imagen en ejecución) |
| `0x0F0000` | 896 KB  | Slot de *staging* (imagen descargada, aún no conmutada) |
| `0x1E0000` | 4 KB    | Metadatos OTA (estado de commit + CRC) |
| `0x1FF000` | 4 KB    | Datos persistentes (récords/ajustes) |

### Flujo de actualización

1. Abrir la web en la página **OTA** (`/ota`).
2. Seleccionar `build/PicoReflex.bin` (la imagen de aplicación) y pulsar
   **Actualizar**. El navegador calcula el CRC32 y lo envía en la cabecera
   `X-CRC32`.
3. La Pico descarga el binario al slot de *staging* sector a sector, calculando el
   CRC32 en caliente. Si el CRC no coincide, **rechaza** la actualización y sigue
   con la imagen actual.
4. Si el CRC coincide, escribe los metadatos de *commit* y se **reinicia**.
5. El bootloader verifica de nuevo el CRC del *staging* y, si es correcto, lo copia
   al slot de aplicación y arranca la nueva imagen. Si algo falla, arranca la
   imagen anterior (**rollback**).

> La imagen que se sube es el binario de **aplicación** (`build/PicoReflex.bin`),
> enlazado en `0x10010000`. El bootloader no se actualiza por OTA (es estable y
> pequeño); si se modifica, se reflashea por USB.

## Usar

### Credenciales del Access Point (modo respaldo)

Cuando la Pico W no logra conectarse a la red STA, levanta su propio Access Point:

| Campo | Valor |
|---|---|
| **Red (SSID)** | `PicoW-ReactionGame` |
| **Seguridad** | **WPA2** (clave por defecto `12345678`, configurable desde `/wifi`) |
| **IP del panel** | `192.168.4.1` |
| **Canal** | `1` (fijo) |

> El AP usa **WPA2** con canal fijo 1. La clave por defecto es **fija `12345678`**
> (igual para cualquier placa) y se muestra en el display al entrar en modo AP
> (`PASS:xxxx`); se puede cambiar desde `/wifi` (8..15 caracteres) y se persiste
> en Flash. El **servidor DHCP integrado**
> (`src/dhcpserver.c`) asigna IPs `192.168.4.16`–`192.168.4.23` a los clientes; sin
> ese DHCP el teléfono se asociaba pero nunca recibía IP.

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

### Solución de problemas del STA (no carga `http://<IP>`)

Si la placa conecta a STA (el display muestra la IP y el LED queda fijo) pero el
navegador **no carga** `http://10.10.x.x`, la causa casi nunca es el firmware: el
servidor HTTP escucha en `0.0.0.0:80` (todas las interfaces, STA incluida) y no
hay firewall ni filtro en el stack lwIP. Lo más probable es **aislamiento entre
clientes del router/AP** (muy común en redes corporativas, de invitados/guest o
puntos de acceso con "AP isolation" activado), que impide que un dispositivo
(por ejemplo, el teléfono) se comunique con otro dispositivo de la misma LAN
(la Pico W). Para confirmarlo y resolverlo:

1. **Probar desde otro equipo de la LAN:** si una PC/otro teléfono de la MISMA
   red tampoco abre la IP, es aislamiento del router, no de la placa.
2. **Revisar el router/AP:** buscar una opción llamada **"AP isolation"**,
   **"aislamiento de clientes"**, **"client isolation"**, **"guest network"** o
   **"red de invitados"** y desactivarla (o mover la Pico y el teléfono a una red
   sin aislamiento). En redes corporativas puede ser política y no modificable.
3. **Descartar el firmware:** arrancá forzando AP (botón 0 al encender) y abrí
   `http://192.168.4.1`. Si ahí SÍ carga, el HTTP de la placa funciona y el
   problema es 100 % de la red STA, no de la Pico.
4. **IP correcta:** verificá que la IP que usás sea la que imprime el puerto
   serie / display (`[web] STA activa, IP ...`). Un DHCP que rotó la IP entre
   arranques deja la URL vieja apuntando a nada; recargá con la IP actual.

> Nota: el servidor HTTP también responde por el nombre mDNS `picoreflex.local`
> (solo en modo STA), que evita depender de la IP. Si tampoco resuelve, es otro
> síntoma de aislamiento (mDNS usa multicast UDP, que el aislamiento también
> bloquea).

## Depurar (printf por USB)

El programa usa `stdio` por USB. Conectar el USB (no en modo BOOTSEL) y abrir el
puerto serie (por ejemplo, PuTTY / `minicom`) a 115200 baud. Ver el estado de red
y las credenciales del AP en tiempo real.

## Regenerar la fuente 5x7 (opcional)

```powershell
python tools\gen_font.py   # regenera include/font5x7.hpp
```

### ¿Qué es la fuente 5x7?

Es la **tipografía de mapa de bits** que usa el display OLED (SSD1306/SH1106) para
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

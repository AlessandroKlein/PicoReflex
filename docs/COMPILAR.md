# Compilar y Flashear — PicoReflex

## Requisitos

- [Raspberry Pi Pico VS Code Extension](https://marketplace.visualstudio.com/items?itemName=raspberry-pi.raspberry-pi-pico)
  (instala automáticamente el SDK, el toolchain `arm-none-eabi-gcc`, CMake, Ninja
  y `picotool` en `~/.pico-sdk`).
- Una Raspberry Pi **Pico W**.

## Configurar las credenciales Wi-Fi (STA)

Editar `include/web_server.hpp` y definir:

```cpp
#define WIFI_SSID     "TU_RED_WIFI"
#define WIFI_PASSWORD "TU_PASSWORD"
```

Si la conexión a esta red falla en 10 s, el dispositivo pasa automáticamente a
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

## Flashear

1. Mantener pulsado **BOOTSEL** y conectar el USB (o reconectar). La Pico aparece
   como unidad de almacenamiento USB (`RPI-RP2`).
2. Copiar `build/PicoReflex.uf2` a la unidad, o usar:
   ```powershell
   picotool load build/PicoReflex.uf2 -f
   ```

## Usar

1. Conectar a la red `PicoW-ReactionGame` (si quedó en AP) o a la IP que se
   muestre en el display (modo STA).
2. Abrir `http://192.168.4.1` (AP) o la IP del dispositivo (STA).
3. Desde la web: configurar semilla/dificultad/modo/velocidad/nombre, iniciar,
   detener y reiniciar estadísticas.
4. Físicamente: presionar un botón para iniciar una ronda.

## Depurar (printf por USB)

El programa usa `stdio` por USB. Conectar el USB (no en modo BOOTSEL) y abrir el
puerto serie (por ejemplo, PuTTY / `minicom`) a 115200 baud.

## Regenerar la fuente 5x7 (opcional)

```powershell
python tools\gen_font.py   # regenera include/font5x7.hpp
```

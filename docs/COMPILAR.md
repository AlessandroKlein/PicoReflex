# Compilar y Flashear — PicoReflex

## Requisitos

- [Raspberry Pi Pico VS Code Extension](https://marketplace.visualstudio.com/items?itemName=raspberry-pi.raspberry-pi-pico)
  (instala automáticamente el SDK, el toolchain `arm-none-eabi-gcc`, CMake, Ninja
  y `picotool` en `~/.pico-sdk`).
- Una Raspberry Pi **Pico W**.

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
| **Clave** | `12345678` |
| **IP del panel** | `192.168.4.1` |

> Si tu dispositivo no puede conectarse a la red con la clave `12345678`, probá
> "olvidar" la red en el teléfono y volver a conectarte (el AP usa WPA2 mixto).
> Verificá también que la Pico no se haya conectado a una red STA guardada en
> Flash (en ese caso el AP no aparece).

1. Conectar a la red `PicoW-ReactionGame` con la clave `12345678` (si quedó en
   AP) o a la IP que se muestre en el display (modo STA).
2. Abrir `http://192.168.4.1` (AP) o la IP del dispositivo (STA). En modo AP,
   cualquier URL que abras redirige automáticamente a la página (captive portal).
3. Desde la web (páginas separadas): semilla (`/seed`), juego (`/game`), OLED
   (`/display`), Wi-Fi (`/wifi`) y nombre (`/player`), más los controles iniciar,
   detener y reiniciar.
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

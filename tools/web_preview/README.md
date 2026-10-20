# Test del frontend (web_ui) en el host

Permite probar las páginas HTML del PicoReflex **sin hardware**: compila
`src/web_ui.cpp` en el host (x86) con mocks del Pico SDK y genera el HTML de cada
ruta para inspeccionarlo en un navegador.

## Requisitos

- Un compilador C++ host (`g++`). En Windows sirve MinGW-w64 (`g++.exe`); en Linux
  `sudo apt install g++`.

## Uso

```bash
# Desde la raíz del proyecto:
g++ -std=c++17 -Itools/web_preview/mock -Iinclude \
    src/web_ui.cpp tools/web_preview/main.cpp -o web_preview.exe
./web_preview.exe
```

Genera `web_preview_{home,seed,game,display,wifi,player,xss}.html`. Ábrelos en un
navegador para revisar el diseño, la tabla de tiempos y la navegación.

## Qué cubre

- Render de las 6 rutas (`/`, `/seed`, `/game`, `/display`, `/wifi`, `/player`).
- Escape anti-XSS: `main.cpp` inyecta un `user_name` y un `wifi_ssid` maliciosos y
  verifica que **no** aparezca `<script>`/`onerror=` sin escapar en el HTML.

## Mocks

`mock/pico/stdlib.h` y `mock/config.hpp` reproducen los tipos que usa `web_ui.cpp`
sin dependencias del Pico SDK. Los accesores (`config_get`, `stats_get`, etc.) se
implementan en `main.cpp` con datos de ejemplo.

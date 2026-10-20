# PicoReflex — Juego de Reacción Rápida Avanzado (Raspberry Pi Pico W)
# Build system basado en el Pico SDK (CMake).
#
# Para compilar:
#   1. Tener PICO_SDK_PATH apuntando al SDK (o el plugin VS Code de Pico).
#   2. cmake -B build && cmake --build build
#
# Librerías enlazadas y su justificación (ver docs/DECISIONES.md):
#   pico_cyw43_arch_lwip_threadsafe_background -> Wi-Fi + lwIP (multi-núcleo)
#   pico_multicore    -> dual-core (multicore_launch_core1)
#   pico_flash        -> flash_safe_execute (escritura segura entre núcleos)
#   pico_rand         -> get_rand_32 (semilla aleatoria)
#   hardware_*        -> GPIO, ADC, PWM, I2C, DMA, Timer, Flash, Clocks

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.3.0)
set(toolchainVersion 15_2_Rel1)
set(picotoolVersion 2.3.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico_w CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(PicoReflex C CXX ASM)

# El Core 1 aloja el servidor web (cyw43_arch + lwIP + HTTP + escaneo). El stack
# por defecto es de 2 KB (0x800), insuficiente: desbordaba y crasheaba al reconectar
# o escanear. Se amplía a 4 KB (máximo que cabe en SCRATCH_X junto al stack del Core 0).
add_compile_definitions(PICO_CORE1_STACK_SIZE=0x1000)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# ---------------------------------------------------------------------------
# Fuentes del proyecto
# ---------------------------------------------------------------------------
add_executable(PicoReflex
    src/main.cpp
    src/shared_state.cpp
    src/game_logic.cpp
    src/audio.cpp
    src/display_driver.cpp
    src/battery_monitor.cpp
    src/flash_storage.cpp
    src/web_server.cpp
    src/web_ui.cpp
    src/captive_portal.cpp
    src/dhcpserver.c
)

pico_set_program_name(PicoReflex "PicoReflex")
pico_set_program_version(PicoReflex "0.4.4")

# Salida de depuración (printf): USB (CDC) y UART0 (GPIO0=TX, GPIO1=RX, 115200).
# El USB es el más cómodo (conectar la Pico y abrir el puerto COM a 115200).
# El UART0 sirve de respaldo con un adaptador USB-serial (FTDI).
pico_enable_stdio_uart(PicoReflex 1)
pico_enable_stdio_usb(PicoReflex 1)

# Directorios de includes del proyecto (include/ y la raíz para lwipopts.h).
target_include_directories(PicoReflex PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/include
)

# ---------------------------------------------------------------------------
# Librerías
# ---------------------------------------------------------------------------
target_link_libraries(PicoReflex
    # Runtime y utilidades
    pico_stdlib
    pico_sync
    pico_util
    # Red (Wi-Fi + lwIP), modo thread-safe para usar desde 2 núcleos
    pico_cyw43_arch_lwip_threadsafe_background
    # Núcleos y almacenamiento
    pico_multicore
    pico_flash
    pico_rand
    pico_low_power
    # Periféricos hardware
    hardware_gpio
    hardware_adc
    hardware_pwm
    hardware_i2c
    hardware_dma
    hardware_timer
    hardware_flash
    hardware_clocks
)

pico_add_extra_outputs(PicoReflex)

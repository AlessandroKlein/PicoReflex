# PicoReflex — Juego de Reacción Rápida Avanzado (Raspberry Pi Pico W)
# Build system basado en el Pico SDK (CMake).
#
# Para compilar:
#   1. Tener PICO_SDK_PATH apuntando al SDK (o el plugin VS Code de Pico).
#   2. cmake -B build && cmake --build build
#
# Librerías enlazadas y su justificación (ver docs/DECISIONES.md):
#   pico_cyw43_arch_lwip_sys_freertos -> Wi-Fi + lwIP (NO_SYS=0, tarea tcpip)
#   pico_lwip_mdns    -> mDNS picoreflex.local (apps/mdns del lwIP)
#   FreeRTOS-Kernel-Heap4 -> scheduler SMP (2 núcleos, heap_4)
#   pico_flash        -> flash_safe_execute (escritura segura entre núcleos)
#   pico_rand         -> get_rand_32 (semilla aleatoria)
#   pico_bootrom      -> reset_usb_boot (bootloader OTA)
#   hardware_*        -> GPIO, ADC, PWM, I2C, DMA, Timer, Flash, Clocks, PIO

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

# Stack inicial del Core 1. El port FreeRTOS SMP arranca el Core 1 con
# multicore_launch_core1(), así que este stack (4 KB) se usa durante el arranque
# del scheduler en ese núcleo.
add_compile_definitions(PICO_CORE1_STACK_SIZE=0x1000)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# FreeRTOS (SMP, 2 núcleos): localiza el kernel y agrega el port RP2040.
# Requiere clonar FreeRTOS-Kernel (ver docs/COMPILAR.md y docs/FREERTOS.md).
include(FreeRTOS_Kernel_import.cmake)

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
    src/led_driver.cpp
    src/ota.c
)

# Programas PIO.
pico_generate_pio_header(PicoReflex ${CMAKE_CURRENT_LIST_DIR}/ws2812.pio)
pico_generate_pio_header(PicoReflex ${CMAKE_CURRENT_LIST_DIR}/button_debounce.pio)

pico_set_program_name(PicoReflex "PicoReflex")
pico_set_program_version(PicoReflex "0.9.6")

# Salida de depuración (printf): USB (CDC) y UART0 (GPIO0=TX, GPIO1=RX, 115200).
# El USB es el más cómodo (conectar la Pico y abrir el puerto COM a 115200).
# El UART0 sirve de respaldo con un adaptador USB-serial (FTDI).
pico_enable_stdio_uart(PicoReflex 1)
pico_enable_stdio_usb(PicoReflex 1)

# Diagnóstico de bring-up: espera hasta 3 s a que un terminal abra el puerto CDC
# antes de seguir, para no perder los mensajes de arranque por USB (sin esto, el
# arranque imprime en los primeros ~10 ms y se pierde si el terminal abre tarde).
# El UART0 (GPIO0=TX/GPIO1=RX) siempre emite. Se puede quitar en producción si
# el retardo de arranque molesta.
target_compile_definitions(PicoReflex PRIVATE PICO_STDIO_USB_CONNECT_WAIT_TIMEOUT_MS=3000)

# Versión en runtime (botón "Comprobar actualizaciones" de /ota).
# Mantener sincronizado con pico_set_program_version() de arriba.
target_compile_definitions(PicoReflex PRIVATE PICOREFLEX_VERSION="0.9.4")
# Versión en runtime (botón "Comprobar actualizaciones" de /ota).
# Mantener sincronizado con pico_set_program_version() de arriba.
target_compile_definitions(PicoReflex PRIVATE PICOREFLEX_VERSION="0.9.4")

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
    # FreeRTOS (SMP, 2 núcleos) con heap_4 (malloc dinámico)
    FreeRTOS-Kernel-Heap4
    # Red (Wi-Fi + lwIP), modo sys_freertos (lwIP en NO_SYS=0 con tarea tcpip)
    pico_cyw43_arch_lwip_sys_freertos
    # mDNS (picoreflex.local): fuentes apps/mdns del lwIP (requiere LWIP_MDNS_RESPONDER)
    pico_lwip_mdns
    # Almacenamiento
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
    hardware_pio
)

pico_add_extra_outputs(PicoReflex)

# ---------------------------------------------------------------------------
# OTA: bootloader de segundo nivel + relocalización de la aplicación
# ---------------------------------------------------------------------------
# Diagnóstico de bring-up: con PICOREFLEX_STANDALONE=ON la app se enlaza en la
# dirección por defecto del SDK (0x10000000, 2 MB), SIN bootloader ni offset,
# para flashear la app sola y descartar el camino bootloader+offset.
option(PICOREFLEX_STANDALONE "Enlazar la app en 0x10000000 sin bootloader (diagnóstico)" OFF)

if(NOT PICOREFLEX_STANDALONE)
    # La aplicación se enlaza SIEMPRE en el slot de aplicación (0x10010000, 896 KB)
    # para que el bootloader pueda conmutar/volver atrás entre imágenes sin re-vincular.
    # El fichero linker/pico_flash_region.ld sobreescribe la región FLASH del SDK
    # (por defecto ORIGIN=0x10000000, 2 MB). Ver include/ota.h para el mapa completo.
    pico_add_linker_script_override_path(PicoReflex
        ${CMAKE_CURRENT_LIST_DIR}/linker
        FILES pico_flash_region.ld
    )
endif()

# Bootloader de segundo nivel (0x10000000, mononúcleo, sin FreeRTOS/lwIP/CYW43):
# verifica el CRC de la imagen en staging y, si es correcta, la copia al slot de
# aplicación y salta a ella; si está corrupta, arranca la imagen anterior
# (rollback). Comparte src/ota.c (CRC32 + metadatos) con la aplicación.
add_executable(PicoReflex_bootloader
    src/bootloader.c
    src/ota.c
)

target_include_directories(PicoReflex_bootloader PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/include
)

target_link_libraries(PicoReflex_bootloader
    pico_stdlib
    hardware_flash
    pico_bootrom   # reset_usb_boot (modo BOOTSEL por USB si no hay app válida)
)

# El bootloader es UART0-ONLY para el diagnóstico (GPIO0=TX, GPIO1=RX, 115200):
# NO inicializa USB CDC, de modo que la app pasa a ser el único dueño del
# controlador USB y lo inicializa limpio (evita "USB no reconocido" en Windows).
pico_enable_stdio_usb(PicoReflex_bootloader 0)
pico_enable_stdio_uart(PicoReflex_bootloader 1)

pico_add_extra_outputs(PicoReflex_bootloader)

# ---------------------------------------------------------------------------
# Linker script autocontenido (workaround para g++ en Windows)
# ---------------------------------------------------------------------------
# El driver g++ de la toolchain ARM en Windows DESCARTa las flags -L
# (-Wl,-L, -Xlinker -L o @rsp) cuando los objetos se pasan mediante fichero de
# respuesta @rsp (lo que hace Ninja al enlazar cientos de objetos). ld usa esas
# -L para resolver las directivas INCLUDE del linker script del SDK, por lo que
# el enlace falla con "cannot open linker script file memmap_default.incl".
# En Linux no falla (g++ conserva -L), pero para que el build sea reproducible
# en Windows generamos, por target, un linker script con todos los INCLUDE
# resueltos en línea y lo pasamos por -T (ruta absoluta, sin depender de -L).
# Ver tools/generate_linker_script.py.
#
# NOTA: el script se genera en tiempo de CONFIGURACIÓN; si cambias
# linker/pico_flash_region.ld (o el SDK), reconfigura CMake para regenerarlo.
set(_picoreflex_platform ${PICO_SDK_PATH}/src/rp2040/pico_platform)
set(_picoreflex_linker_common_dirs
    ${_picoreflex_platform}/script_include
    ${PICO_SDK_PATH}/src/rp2_common/pico_standard_link/script_include
    ${CMAKE_BINARY_DIR}/pico-sdk/src/rp2_common/pico_standard_link
)

find_package(Python3 QUIET COMPONENTS Interpreter)

function(picoreflex_self_contained_linker_script TARGET)
    # Rutas de override (pico_add_linker_script_override_path) van PRIMERO,
    # replicando el orden de búsqueda -L de ld.
    get_target_property(_override_dirs ${TARGET} PICO_TARGET_LINKER_SCRIPT_OVERRIDE_PATHS)
    if(_override_dirs STREQUAL "_override_dirs-NOTFOUND")
        set(_override_dirs "")
    endif()

    set(_search_dirs ${_override_dirs} ${_picoreflex_linker_common_dirs})
    string(JOIN "|" _search_str ${_search_dirs})
    set(_out ${CMAKE_BINARY_DIR}/${TARGET}_memmap.ld)

    if(NOT Python3_EXECUTABLE)
        message(FATAL_ERROR
            "PicoReflex requiere Python3 para generar el linker script autocontenido "
            "(tools/generate_linker_script.py). Instala Python3 o añádelo al PATH.")
    endif()

    execute_process(
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_LIST_DIR}/tools/generate_linker_script.py
            --entry ${_picoreflex_platform}/memmap_default.ld
            --search ${_search_str}
            --out ${_out}
        RESULT_VARIABLE _gen_result
        OUTPUT_VARIABLE _gen_output
        ERROR_VARIABLE _gen_error
    )

    if(NOT _gen_result EQUAL 0)
        message(FATAL_ERROR
            "No se pudo generar el linker script autocontenido para ${TARGET}: "
            "${_gen_output} ${_gen_error}")
    endif()

    # PICO_TARGET_LINKER_SCRIPT es la propiedad que el SDK usa como -T (ver
    # pico_standard_link/CMakeLists.txt: $<TARGET_PROPERTY:PICO_TARGET_LINKER_SCRIPT>).
    set_target_properties(${TARGET} PROPERTIES PICO_TARGET_LINKER_SCRIPT ${_out})
    # Re-enlazar si cambia el script generado.
    set_property(TARGET ${TARGET} APPEND PROPERTY LINK_DEPENDS ${_out})
endfunction()

picoreflex_self_contained_linker_script(PicoReflex)
picoreflex_self_contained_linker_script(PicoReflex_bootloader)

# ---------------------------------------------------------------------------
# Reporte de tamaño de imagen (.text/.rodata/.data/.bss) como artefacto de CI.
#
# `cmake --build build --target size` genera build/size-report.txt usando la
# herramienta arm-none-eabi-size del toolchain. El workflow de CI ejecuta este
# mismo objetivo y publica el fichero como artefacto (ver .github/workflows/ci.yml).
# ---------------------------------------------------------------------------
get_filename_component(_PICOREFLEX_TOOLCHAIN_BIN "${CMAKE_C_COMPILER}" DIRECTORY)
if(CMAKE_HOST_WIN32)
    set(_PICOREFLEX_SIZE_SUFFIX ".exe")
else()
    set(_PICOREFLEX_SIZE_SUFFIX "")
endif()
set(PICOREFLEX_SIZE_TOOL "${_PICOREFLEX_TOOLCHAIN_BIN}/arm-none-eabi-size${_PICOREFLEX_SIZE_SUFFIX}")

add_custom_target(size
    COMMAND ${CMAKE_COMMAND}
        -DSIZE_TOOL=${PICOREFLEX_SIZE_TOOL}
        -DAPP_ELF=$<TARGET_FILE:PicoReflex>
        -DBOOT_ELF=$<TARGET_FILE:PicoReflex_bootloader>
        -DOUT=${CMAKE_BINARY_DIR}/size-report.txt
        -P ${CMAKE_CURRENT_LIST_DIR}/tools/size_report.cmake
    DEPENDS PicoReflex PicoReflex_bootloader
    COMMENT "Generando reporte de tamaño (size-report.txt)"
    VERBATIM
)

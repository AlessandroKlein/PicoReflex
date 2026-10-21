# tools/size_report.cmake
#
# Genera el reporte de tamaño de imagen de PicoReflex (aplicación + bootloader
# de segundo nivel). Lo invoca el objetivo CMake `size`:
#
#   cmake --build build --target size
#
# o manualmente:
#
#   cmake -DSIZE_TOOL=<arm-none-eabi-size> \
#         -DAPP_ELF=<PicoReflex.elf> \
#         -DBOOT_ELF=<PicoReflex_bootloader.elf> \
#         -DOUT=<build/size-report.txt> \
#         -P tools/size_report.cmake
#
# El reporte resultante incluye las secciones .text/.rodata/.data/.bss (y el
# resto de secciones) en formato SysV de `arm-none-eabi-size -A`, de modo que
# el CI puede publicarlo como artefacto y comparar el uso de Flash/RAM entre
# versiones.

if(NOT SIZE_TOOL OR NOT APP_ELF OR NOT BOOT_ELF OR NOT OUT)
    message(FATAL_ERROR "size_report.cmake requiere -DSIZE_TOOL -DAPP_ELF -DBOOT_ELF -DOUT")
endif()

string(TIMESTAMP _now "%Y-%m-%d %H:%M:%S %Z")

file(WRITE "${OUT}" "PicoReflex — reporte de tamaño de imagen\n")
file(APPEND "${OUT}" "Generado: ${_now}\n")
file(APPEND "${OUT}" "Herramienta: ${SIZE_TOOL}\n\n")

function(_emit_section _name _elf)
    file(APPEND "${OUT}" "=== ${_name} ===\n")
    execute_process(
        COMMAND "${SIZE_TOOL}" -A "${_elf}"
        OUTPUT_VARIABLE _out
        ERROR_VARIABLE _err
        RESULT_VARIABLE _rc
    )
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "size -A falló para ${_elf}: ${_err}")
    endif()
    file(APPEND "${OUT}" "${_out}\n")
endfunction()

_emit_section("Aplicación (PicoReflex.elf)" "${APP_ELF}")
_emit_section("Bootloader (PicoReflex_bootloader.elf)" "${BOOT_ELF}")

message(STATUS "Reporte de tamaño escrito en ${OUT}")

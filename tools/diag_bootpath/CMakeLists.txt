cmake_minimum_required(VERSION 3.13)

# Mismo SDK y toolchain que PicoReflex (fallback si no llegan por -D/env).
if (NOT DEFINED PICO_SDK_PATH)
    set(PICO_SDK_PATH "C:/Users/Alessandro/.pico-sdk/sdk/2.3.0" CACHE PATH "Pico SDK path" FORCE)
endif()
if (NOT DEFINED PICO_TOOLCHAIN_PATH)
    set(PICO_TOOLCHAIN_PATH "C:/Users/Alessandro/.pico-sdk/toolchain/15_2_Rel1" CACHE PATH "Pico toolchain path" FORCE)
endif()

set(PICO_BOARD pico_w CACHE STRING "Board type" FORCE)

include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(blink_at_offset C CXX)

pico_sdk_init()

add_executable(blink_at_offset blink.c)

target_link_libraries(blink_at_offset PRIVATE
    pico_stdlib
    pico_cyw43_arch_none
)

pico_add_extra_outputs(blink_at_offset)

# ---------------------------------------------------------------------------
# Relocalización al slot de aplicación (0x10010000), IGUAL que PicoReflex.
# Reutiliza el MISMO fichero linker/pico_flash_region.ld del repo raíz para que
# este blink mínimo caiga exactamente donde cae la app de PicoReflex (slot A).
# ---------------------------------------------------------------------------
pico_add_linker_script_override_path(blink_at_offset
    ${CMAKE_CURRENT_LIST_DIR}/../../linker
    FILES pico_flash_region.ld
)

# ---------------------------------------------------------------------------
# Linker script autocontenido (mismo workaround t15 que PicoReflex).
# Reutiliza tools/generate_linker_script.py con el mismo orden de búsqueda -L:
# override (linker/) PRIMERO, luego los script_include del SDK y el build-dir.
# ---------------------------------------------------------------------------
set(_diag_platform ${PICO_SDK_PATH}/src/rp2040/pico_platform)
set(_diag_linker_common_dirs
    ${_diag_platform}/script_include
    ${PICO_SDK_PATH}/src/rp2_common/pico_standard_link/script_include
    ${CMAKE_BINARY_DIR}/pico-sdk/src/rp2_common/pico_standard_link
)

find_package(Python3 QUIET COMPONENTS Interpreter)

function(diag_self_contained_linker_script TARGET)
    get_target_property(_override_dirs ${TARGET} PICO_TARGET_LINKER_SCRIPT_OVERRIDE_PATHS)
    if(_override_dirs STREQUAL "_override_dirs-NOTFOUND")
        set(_override_dirs "")
    endif()

    set(_search_dirs ${_override_dirs} ${_diag_linker_common_dirs})
    string(JOIN "|" _search_str ${_search_dirs})
    set(_out ${CMAKE_BINARY_DIR}/${TARGET}_memmap.ld)

    if(NOT Python3_EXECUTABLE)
        message(FATAL_ERROR
            "blink_at_offset requiere Python3 para generar el linker script autocontenido "
            "(tools/generate_linker_script.py). Instala Python3 o añádelo al PATH.")
    endif()

    execute_process(
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_LIST_DIR}/../../tools/generate_linker_script.py
            --entry ${_diag_platform}/memmap_default.ld
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

    set_target_properties(${TARGET} PROPERTIES PICO_TARGET_LINKER_SCRIPT ${_out})
    set_property(TARGET ${TARGET} APPEND PROPERTY LINK_DEPENDS ${_out})
endfunction()

diag_self_contained_linker_script(blink_at_offset)

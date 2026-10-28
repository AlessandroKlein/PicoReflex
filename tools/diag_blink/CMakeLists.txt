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

project(blink C CXX)

pico_sdk_init()

add_executable(blink blink.c)

target_link_libraries(blink PRIVATE
    pico_stdlib
    pico_cyw43_arch_none
)

pico_add_extra_outputs(blink)

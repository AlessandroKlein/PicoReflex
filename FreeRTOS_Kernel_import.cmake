# Localiza el kernel FreeRTOS y agrega el port RP2040 (SMP) al build.
#
# Uso:
#   - Define FREERTOS_KERNEL_PATH (cache o variable de entorno) al path del repo
#     FreeRTOS-Kernel, o
#   - Clonalo como hermano del SDK:  <PICO_SDK_PATH>/../../FreeRTOS-Kernel
#     (p. ej. C:\Users\<user>\.pico-sdk\FreeRTOS-Kernel).

if (DEFINED ENV{FREERTOS_KERNEL_PATH} AND (NOT FREERTOS_KERNEL_PATH))
    set(FREERTOS_KERNEL_PATH $ENV{FREERTOS_KERNEL_PATH})
endif()

if (NOT FREERTOS_KERNEL_PATH)
    set(FREERTOS_KERNEL_PATH "${PICO_SDK_PATH}/../../FreeRTOS-Kernel")
endif()

set(FREERTOS_KERNEL_PATH "${FREERTOS_KERNEL_PATH}" CACHE PATH "Path to the FreeRTOS Kernel")

if (NOT EXISTS "${FREERTOS_KERNEL_PATH}/portable/ThirdParty/GCC/RP2040/CMakeLists.txt")
    message(FATAL_ERROR
        "FreeRTOS-Kernel no encontrado en ${FREERTOS_KERNEL_PATH}. "
        "Clonalo con: git clone --branch V11.1.0 https://github.com/FreeRTOS/FreeRTOS-Kernel.git ${FREERTOS_KERNEL_PATH}")
endif()

add_subdirectory(${FREERTOS_KERNEL_PATH}/portable/ThirdParty/GCC/RP2040 FREERTOS_KERNEL)

# PicoReflex — Entorno de compilación reproducible (Raspberry Pi Pico SDK)
#
# Construye una imagen con todo lo necesario para compilar el firmware de la
# Pico W: toolchain ARM (arm-none-eabi-gcc), CMake, Ninja y el Pico SDK 2.3.0
# con sus submódulos (lwIP, cyw43-driver, tinyusb, mbedtls, btstack).
#
# Uso:
#   docker build -t picoreflex-build .
#   docker run --rm -v "${PWD}:/project" picoreflex-build cmake -B build -S .
#   docker run --rm -v "${PWD}:/project" picoreflex-build cmake --build build
#   # el .uf2 queda en ./build/
#
# Ver también: .github/workflows/ci.yml (CI) y docker-compose.yml.

FROM debian:bookworm-slim

# Versión del SDK (debe coincidir con CMakeLists.txt / la extensión VS Code).
ARG PICO_SDK_TAG=2.3.0

# Dependencias de compilación.
#  - gcc-arm-none-eabi + libnewlib-arm-none-eabi: toolchain para RP2040 (Cortex-M0+).
#  - cmake + ninja-build: sistema de build del SDK.
#  - git + python3: el SDK usa git (submódulos) y scripts de python.
RUN apt-get update && apt-get install -y --no-install-recommends \
        cmake \
        ninja-build \
        gcc-arm-none-eabi \
        libnewlib-arm-none-eabi \
        libstdc++-arm-none-eabi-newlib \
        build-essential \
        git \
        ca-certificates \
        python3 \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

# Descarga el Pico SDK con sus submódulos (lwIP, cyw43-driver, tinyusb, ...).
RUN git clone --depth 1 --branch "${PICO_SDK_TAG}" \
        https://github.com/raspberrypi/pico-sdk.git /opt/pico-sdk \
    && git -C /opt/pico-sdk submodule update --init --depth 1

ENV PICO_SDK_PATH=/opt/pico-sdk

# Directorio de trabajo donde se monta el proyecto.
WORKDIR /project

# Comando por defecto: compilar (configura + build + genera el .uf2).
CMD ["sh", "-c", "cmake -B build -S . && cmake --build build -j"]

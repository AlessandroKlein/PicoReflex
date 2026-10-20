// Mock de pico/stdlib.h para compilar el "frontend" (web_ui) en el host (x86).
// Solo provee los tipos básicos que usa web_ui.cpp; no tiene hardware.
#ifndef _PICO_STDLIB_H
#define _PICO_STDLIB_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
typedef unsigned int uint;
#endif

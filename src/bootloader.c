/**
 * @file   bootloader.c
 * @brief  Bootloader de segundo nivel para OTA A/B (staging + commit).
 *
 * El bootloader se enlaza en 0x10000000 (primeros 64 KB). La aplicación se enlaza
 * SIEMPRE en el slot de aplicación (0x10010000). El flujo es:
 *
 *   1. Si los metadatos tienen un commit pendiente, verifica el CRC de la imagen
 *      en STAGING (0x100F0000). Si es correcto, la COPIA al slot de aplicación y
 *      verifica la copia antes de conmutar (commit atómico). Si el staging está
 *      corrupto, limpia el commit y arranca la imagen anterior (ROLLBACK).
 *   2. Si no hay commit pendiente, verifica el CRC de la imagen comprometida y
 *      salta a ella.
 *   3. Si no hay metadatos válidos (instalación inicial), hace una comprobación
 *      de sanidad de la tabla de vectores y salta, o entra en modo USB.
 *
 * El bootloader es mononúcleo y NO usa FreeRTOS ni flash_safe_execute: la copia
 * se hace sector a sector con un buffer de 4 KB en RAM, llamando directamente a
 * flash_range_erase/program (que son RAM-residentes y gestionan el XIP vía boot2).
 */

#include "pico/stdlib.h"
#include "pico/stdio_uart.h"       // stdio_uart_init() — solo UART, sin tocar USB
#include "pico/bootrom.h"          // reset_usb_boot()
#include "hardware/flash.h"
#include "hardware/address_mapped.h" // XIP_BASE
#include "hardware/structs/scb.h"    // scb_hw->vtor
#include "hardware/structs/nvic.h"   // nvic_hw->icer/icpr
#include "hardware/structs/systick.h" // systick_hw->csr

#include <string.h>
#include <stdio.h>

#include "ota.h"

// -----------------------------------------------------------------------------
// Salto a la aplicación (slot de aplicación, 0x10010000)
// -----------------------------------------------------------------------------
// El vector de reset de la app está en slot_base + 0x100 (tras el boot2). Se
// valida que MSP apunte a RAM y que el PC apunte a Flash antes de saltar, para
// no arrancar basura si el slot está vacío/corrupto.
static void __attribute__((noreturn)) jump_to_app(void) {
    uint32_t vt_addr = XIP_BASE + OTA_APP_OFFSET + OTA_SLOT_VECTOR;
    const uint32_t* vt = (const uint32_t*)vt_addr;
    uint32_t msp   = vt[0];
    uint32_t reset = vt[1];

    printf("[boot] saltando a app (vt=0x%08x msp=0x%08x reset=0x%08x)\n",
           vt_addr, msp, reset);

    bool msp_ok   = (msp >= 0x20000000u) && (msp <= 0x20042000u); // RAM (264 KB), tope incluido.
    bool reset_ok = ((reset & 0xFF000000u) == 0x10000000u);      // Flash XIP.

    if (!msp_ok || !reset_ok) {
        // Sin imagen válida: entra en modo BOOTSEL (USB) para re-flashear.
        printf("[boot] sin app valida -> USB\n");
        reset_usb_boot(0, 0);
        for (;;) { __asm volatile ("wfi"); }
    }

    // Estado equivalente a un reset limpio antes de saltar: deshabilita y limpia
    // TODAS las IRQ externas y SysTick, apunta VTOR a la tabla de vectores de la
    // app, y salta con las interrupciones HABILITADAS (PRIMASK=0). Si se dejara
    // PRIMASK=1 (cpsid i), la app no podría atender la IRQ USB (tud_task corre en
    // esa IRQ) durante su stdio_init_all() y Windows mostraría "USB no reconocido"
    // en vez de un puerto COM. La app re-copiará la tabla a RAM en su runtime_init.
    nvic_hw->icer   = 0xFFFFFFFFu;  // deshabilita IRQ externas 0..31
    nvic_hw->icpr   = 0xFFFFFFFFu;  // limpia IRQ pendientes
    systick_hw->csr = 0;            // apaga SysTick (el bootloader no lo usa)
    scb_hw->vtor    = vt_addr;
    __asm volatile (
        "msr msp, %0\n"
        "cpsie i\n"                 // PRIMASK=0: interrupciones habilitadas
        "dsb\n"
        "isb\n"
        "bx  %1\n"
        : : "r" (msp), "r" (reset) : "memory");
    __builtin_unreachable();
}

// -----------------------------------------------------------------------------
// Copia flash -> flash (staging -> aplicación), sector a sector
// -----------------------------------------------------------------------------
static void flash_copy(uint32_t src_off, uint32_t dst_off, uint32_t len) {
    // Buffer de 4 KB en RAM (bss del bootloader).
    static uint8_t buf[FLASH_SECTOR_SIZE];

    uint32_t sectors = (len + FLASH_SECTOR_SIZE - 1u) / FLASH_SECTOR_SIZE;
    for (uint32_t i = 0; i < sectors; i++) {
        uint32_t s = i * FLASH_SECTOR_SIZE;
        uint32_t chunk = len - s;
        if (chunk > FLASH_SECTOR_SIZE) chunk = FLASH_SECTOR_SIZE;

        // Lee el sector origen por XIP y rellena el resto con 0xFF (borrado).
        memcpy(buf, (const void*)(XIP_BASE + src_off + s), chunk);
        memset(buf + chunk, 0xFF, FLASH_SECTOR_SIZE - chunk);

        // Borra el sector destino y programa páginas de 256 B.
        uint32_t prog = (chunk + FLASH_PAGE_SIZE - 1u) & ~(FLASH_PAGE_SIZE - 1u);
        flash_range_erase(dst_off + s, FLASH_SECTOR_SIZE);
        flash_range_program(dst_off + s, buf, prog);
    }
}

// -----------------------------------------------------------------------------
// Punto de entrada del bootloader
// -----------------------------------------------------------------------------
int main() {
    // Solo UART: el bootloader NO debe inicializar USB CDC. Si lo hiciera, la
    // app (que también llama stdio_init_all) re-inicializaría el controlador USB
    // ya activo y Windows mostraría "USB desconocido" en vez de un puerto COM.
    // El USB lo reserva la app, que es quien lo necesita para el terminal.
    stdio_uart_init();
    printf("[boot] inicio\n");

    ota_metadata_t meta;
    ota_meta_read(&meta);
    bool meta_ok = ota_meta_valid(&meta);

    printf("[boot] meta valid=%d commit=%u app_len=%u\n",
           meta_ok, meta.commit_state, meta.app_len);

    // --- 1. Commit pendiente: conmutar (o hacer rollback si el staging falla). ---
    if (meta_ok && meta.commit_state == OTA_COMMIT_PENDING) {
        bool staging_ok =
            (meta.staging_len > 0u && meta.staging_len <= OTA_SLOT_SIZE) &&
            ota_image_verify(OTA_STAGING_OFFSET, meta.staging_len, meta.staging_crc32);

        if (staging_ok) {
            // Copia staging -> aplicación y verifica la copia ANTES de conmutar.
            flash_copy(OTA_STAGING_OFFSET, OTA_APP_OFFSET, meta.staging_len);
            if (ota_image_verify(OTA_APP_OFFSET, meta.staging_len, meta.staging_crc32)) {
                meta.commit_state = OTA_COMMIT_IDLE;
                meta.app_len      = meta.staging_len;
                meta.app_crc32    = meta.staging_crc32;
                meta.staging_len  = 0;
                meta.staging_crc32 = 0;
                ota_meta_finalize(&meta);
                ota_meta_write_raw(&meta);
                jump_to_app();
            }
            // La copia falló (fallo raro de escritura): se conserva el commit
            // pendiente para reintentar en el próximo arranque y se cae a USB.
            printf("[boot] copia fallida -> USB\n");
            reset_usb_boot(0, 0);
            for (;;) { __asm volatile ("wfi"); }
        }

        // Staging corrupto: ROLLBACK. Se limpia el commit y se arranca la imagen
        // anterior, que sigue intacta en el slot de aplicación.
        meta.commit_state = OTA_COMMIT_IDLE;
        meta.staging_len  = 0;
        meta.staging_crc32 = 0;
        ota_meta_finalize(&meta);
        ota_meta_write_raw(&meta);
        jump_to_app();
    }

    // --- 2. Sin commit pendiente: arrancar la imagen comprometida. ---
    if (meta_ok) {
        bool app_known = (meta.app_len > 0u && meta.app_len <= OTA_SLOT_SIZE);
        if (app_known && ota_image_verify(OTA_APP_OFFSET, meta.app_len, meta.app_crc32)) {
            jump_to_app();
        }
        if (app_known) {
            // Imagen comprometida corrupta y sin staging válido: USB.
            printf("[boot] app corrupta -> USB\n");
            reset_usb_boot(0, 0);
            for (;;) { __asm volatile ("wfi"); }
        }
        // app_len desconocido (0): caer a la comprobación de vectores.
    }

    // --- 3. Sin metadatos (instalación inicial) o app_len desconocido. ---
    jump_to_app();   // jump_to_app valida la tabla de vectores y decide.

    return 0;        // No alcanzable.
}

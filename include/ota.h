/**
 * @file   ota.h
 * @brief  Actualización de firmware por aire (OTA) con partición A/B + staging.
 *
 * Mapa de la Flash QSPI de 2 MB (base XIP 0x10000000):
 *
 *   Offset        Tamaño      Uso
 *   ------------  ----------  ------------------------------------------------
 *   0x00000000    64 KB       Bootloader de segundo nivel (0x10000000).
 *   0x00010000    896 KB      Slot de APLICACIÓN (0x10010000). La app se enlaza
 *                             aquí; es la única imagen que se ejecuta.
 *   0x000F0000    896 KB      Slot de STAGING (0x100F0000). Aquí se descarga y
 *                             verifica la imagen nueva ANTES de conmutar.
 *   0x001E0000    4 KB        Metadatos OTA (0x101E0000): estado de commit, CRC.
 *   0x001FF000    4 KB        Datos persistentes (flash_storage, sin cambios).
 *
 * Flujo de actualización (rollback seguro):
 *   1. La web sube el binario por POST a /ota/upload (con su CRC32 en X-CRC32).
 *   2. La app lo graba sector a sector en STAGING calculando el CRC en caliente.
 *   3. Si el CRC coincide, la app escribe los metadatos con commit_state=PENDING
 *      y reinicia. Si no coincide, NO conmuta (sigue corriendo la imagen actual).
 *   4. El bootloader lee los metadatos: si hay commit pendiente, verifica el CRC
 *      del staging y lo COPIA al slot de aplicación antes de saltar. Si el
 *      staging está corrupto, arranca la imagen anterior (rollback).
 *
 * Este encabezado es compartido por la aplicación y el bootloader (C/C++).
 */

#ifndef PICO_REFLEX_OTA_H
#define PICO_REFLEX_OTA_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// -----------------------------------------------------------------------------
// Mapa de particiones (offsets absolutos dentro de la Flash de 2 MB)
// -----------------------------------------------------------------------------
#define OTA_BOOTLOADER_OFFSET 0x00000000u   // Bootloader (0x10000000).
#define OTA_APP_OFFSET        0x00010000u   // Slot de aplicación (0x10010000).
#define OTA_STAGING_OFFSET    0x000F0000u   // Slot de staging (0x100F0000).
#define OTA_SLOT_SIZE         0x000E0000u   // 917504 B = 896 KB por slot.
#define OTA_METADATA_OFFSET   0x001E0000u   // Metadatos OTA (0x101E0000).

// La tabla de vectores de una imagen está 256 B después del inicio del slot
// (los primeros 256 B son el boot2, que en el slot de aplicación no se ejecuta).
#define OTA_SLOT_VECTOR       0x100u

// -----------------------------------------------------------------------------
// Metadatos OTA (una página de 256 B; se reserva un sector completo de 4 KB)
// -----------------------------------------------------------------------------
#define OTA_METADATA_MAGIC    0x4F544131u   // "OTA1".
#define OTA_METADATA_VERSION  1u

// Estados de commit.
#define OTA_COMMIT_IDLE       0u            // Sin actualización pendiente.
#define OTA_COMMIT_PENDING    1u            // Staging verificado; copiar al slot app.

// Estructura de metadatos (48 bytes; cabe sobradamente en una página de 256 B).
typedef struct __attribute__((packed)) {
    uint32_t magic;            // OTA_METADATA_MAGIC.
    uint32_t version;          // OTA_METADATA_VERSION.
    uint32_t commit_state;     // OTA_COMMIT_IDLE / OTA_COMMIT_PENDING.
    uint32_t staging_len;      // Bytes válidos de la imagen en staging (0 = vacío).
    uint32_t staging_crc32;    // CRC32 de la imagen en staging.
    uint32_t app_len;          // Bytes de la imagen comprometida (0 = desconocido).
    uint32_t app_crc32;        // CRC32 de la imagen comprometida.
    uint32_t boot_count;       // Reservado (0).
    uint32_t reserved[2];      // Reservado.
    uint32_t checksum;         // Suma simple de los bytes previos.
} ota_metadata_t;

// -----------------------------------------------------------------------------
// CRC32 (IEEE 802.3, polinomio reflejado 0xEDB88320)
// -----------------------------------------------------------------------------
// API incremental: para una sola pasada usar ota_crc32(0, data, len); para
// streaming, encadenar: crc = ota_crc32(crc, chunk, len).
uint32_t ota_crc32(uint32_t crc, const uint8_t* data, uint32_t len);

// -----------------------------------------------------------------------------
// Metadatos
// -----------------------------------------------------------------------------
// Lee los metadatos desde XIP (solo lectura; no hace flash_safe_execute).
void ota_meta_read(ota_metadata_t* meta);

// Valida magic + version + checksum de los metadatos leídos.
bool ota_meta_valid(const ota_metadata_t* meta);

// Recalcula el checksum del struct (preparar una escritura).
void ota_meta_finalize(ota_metadata_t* meta);

// -----------------------------------------------------------------------------
// Verificación de integridad de una imagen en Flash (lectura XIP)
// -----------------------------------------------------------------------------
// Devuelve true si los `len` bytes en `slot_offset` tienen el CRC `expected_crc`.
bool ota_image_verify(uint32_t slot_offset, uint32_t len, uint32_t expected_crc);

// -----------------------------------------------------------------------------
// Escritura "cruda" de metadatos (SIN flash_safe_execute)
// -----------------------------------------------------------------------------
// Borra el sector de metadatos y programa una página de 256 B. SOLO debe usarse
// en el bootloader (un solo núcleo, sin FreeRTOS) o dentro de flash_safe_execute.
void ota_meta_write_raw(const ota_metadata_t* meta);

#ifdef __cplusplus
}
#endif

#endif // PICO_REFLEX_OTA_H

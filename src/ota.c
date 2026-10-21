/**
 * @file   ota.c
 * @brief  Lógica común de OTA (CRC32, metadatos, verificación) compartida por la
 *         aplicación y el bootloader. Implementación en C (sin FreeRTOS).
 *
 * Este fichero NO usa flash_safe_execute: la lectura es por XIP y la única
 * escritura (ota_meta_write_raw) está pensada para el bootloader (mononúcleo) o
 * para ejecutarse dentro de flash_safe_execute desde la aplicación.
 */

#include "ota.h"

#include <string.h>

#include "hardware/flash.h"
#include "hardware/address_mapped.h"   // XIP_BASE

// -----------------------------------------------------------------------------
// CRC32 (IEEE 802.3, polinomio reflejado 0xEDB88320)
// -----------------------------------------------------------------------------
uint32_t ota_crc32(uint32_t crc, const uint8_t* data, uint32_t len) {
    crc ^= 0xFFFFFFFFu;               // Init / deshacer el final-xor previo.
    for (uint32_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return crc ^ 0xFFFFFFFFu;         // Final xor.
}

// -----------------------------------------------------------------------------
// Metadatos
// -----------------------------------------------------------------------------
void ota_meta_read(ota_metadata_t* meta) {
    const ota_metadata_t* src =
        (const ota_metadata_t*)(XIP_BASE + OTA_METADATA_OFFSET);
    memcpy(meta, src, sizeof(*meta));
}

bool ota_meta_valid(const ota_metadata_t* meta) {
    if (meta->magic != OTA_METADATA_MAGIC) return false;
    if (meta->version != OTA_METADATA_VERSION) return false;

    uint32_t sum = 0;
    const uint8_t* p = (const uint8_t*)meta;
    size_t n = offsetof(ota_metadata_t, checksum);
    for (size_t i = 0; i < n; i++) {
        sum += p[i];
    }
    return (sum == meta->checksum);
}

void ota_meta_finalize(ota_metadata_t* meta) {
    meta->checksum = 0;
    uint32_t sum = 0;
    const uint8_t* p = (const uint8_t*)meta;
    size_t n = offsetof(ota_metadata_t, checksum);
    for (size_t i = 0; i < n; i++) {
        sum += p[i];
    }
    meta->checksum = sum;
}

// -----------------------------------------------------------------------------
// Verificación de integridad de una imagen (lectura XIP)
// -----------------------------------------------------------------------------
bool ota_image_verify(uint32_t slot_offset, uint32_t len, uint32_t expected_crc) {
    if (len == 0u || len == 0xFFFFFFFFu) return false;
    const uint8_t* base = (const uint8_t*)(XIP_BASE + slot_offset);
    return (ota_crc32(0, base, len) == expected_crc);
}

// -----------------------------------------------------------------------------
// Escritura cruda de metadatos (borra sector + programa una página de 256 B)
// -----------------------------------------------------------------------------
void ota_meta_write_raw(const ota_metadata_t* meta) {
    // Buffer de página en RAM (requisito de flash_range_program: datos en RAM).
    static uint8_t page[FLASH_PAGE_SIZE];
    memset(page, 0xFF, sizeof(page));          // La Flash borrada se lee 0xFF.
    memcpy(page, meta, sizeof(*meta));

    flash_range_erase(OTA_METADATA_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(OTA_METADATA_OFFSET, page, FLASH_PAGE_SIZE);
}

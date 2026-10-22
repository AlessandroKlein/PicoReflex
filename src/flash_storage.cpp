/**
 * @file   flash_storage.cpp
 * @brief  Implementación del almacenamiento no volátil en Flash.
 *
 * Lectura:  se hace directamente por XIP (la Flash está mapeada en memoria en
 *           XIP_BASE + offset). Solo hay que leer, no hace falta lock.
 * Escritura: se borra el sector (4096 B) y se programa la página (256 B) dentro
 *           de flash_safe_execute(), que garantiza que el otro núcleo no está
 *           ejecutando código desde Flash y que las IRQ están deshabilitadas.
 *
 * IMPORTANTE (multinúcleo): flash_safe_execute() necesita que el OTRO núcleo
 * haya llamado flash_safe_execute_core_init(). En este proyecto el guardado lo
 * dispara el Core 1 (web), así que el Core 0 llama a flash_safe_execute_core_init()
 * en main() antes de lanzar el Core 1.
 */

#include "flash_storage.hpp"

#include <cstring>

#include "hardware/flash.h"
#include "pico/flash.h"      // flash_safe_execute()
#include "hardware/address_mapped.h" // XIP_BASE

#include "config.hpp"        // WIFI_SSID_DEFAULT / WIFI_PASSWORD_DEFAULT

namespace flash {

// -----------------------------------------------------------------------------
// Por defecto
// -----------------------------------------------------------------------------
void set_default(PersistentData* d) {
    std::memset(d, 0, sizeof(*d));
    d->magic = PERSISTENT_MAGIC;
    d->version = PERSISTENT_VERSION;
    d->best_time_us = UINT32_MAX;   // "Sin récord" se representa con el máximo.
    const char* def = "JUGADOR";
    std::strncpy(d->user_name, def, USER_NAME_MAX - 1u);
    for (int i = 0; i < RANKING_SIZE; i++) {
        d->ranking[i].time_us = UINT32_MAX;
        std::strncpy(d->ranking[i].name, "-", USER_NAME_MAX - 1u);
    }
    // Configuración de red/preferencias por defecto (v0.2.0+).
    std::strncpy(d->wifi_ssid, WIFI_SSID_DEFAULT, sizeof(d->wifi_ssid) - 1u);
    std::strncpy(d->wifi_password, WIFI_PASSWORD_DEFAULT, sizeof(d->wifi_password) - 1u);
    d->oled_enabled = 1;
    d->start_delay_s = 3;
    d->batt_calib = BatteryCalibration{};   // Curva por defecto (4200/3700/3000, sin comp.).
    d->display_controller = PICOREFLEX_DISPLAY_CONTROLLER;   // SSD1306 o SH1106.
    update_checksum(d);
}

// -----------------------------------------------------------------------------
// Checksum (suma simple de todos los bytes previos al campo checksum)
// -----------------------------------------------------------------------------
void update_checksum(PersistentData* d) {
    d->checksum = 0;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(d);
    size_t n = reinterpret_cast<size_t>(&d->checksum) - reinterpret_cast<size_t>(d);
    uint32_t sum = 0;
    for (size_t i = 0; i < n; i++) {
        sum += p[i];
    }
    d->checksum = sum;
}

// -----------------------------------------------------------------------------
// Clave por defecto del AP (derivada del RUID de la Flash)
// -----------------------------------------------------------------------------
namespace {

// Callback de flash_safe_execute: lee el identificador único de la Flash (RUID).
void do_read_ruid(void* param) {
    flash_get_unique_id(static_cast<uint8_t*>(param));
}

} // namespace

// Deriva una clave única por placa a partir del RUID (8 bytes = 16 nibbles).
// Se usan 15 nibbles (60 bits) para que quepan en ap_password[16] (15 + '\0').
// Se ejecuta dentro de flash_safe_execute porque flash_get_unique_id() suspende
// el XIP (no es seguro con IRQs/otro núcleo ejecutando desde Flash).
void ap_password_default(char* out, size_t n) {
    if (n == 0) return;
    static const char hex[] = "0123456789abcdef";
    uint8_t id[FLASH_UNIQUE_ID_SIZE_BYTES] = {0};
    flash_safe_execute(do_read_ruid, id, 1000);

    size_t chars = (n >= 16u) ? 15u : (n - 1u);
    for (size_t i = 0; i < chars; i++) {
        uint8_t nib = (i & 1u) ? (id[i >> 1] & 0x0Fu) : (id[i >> 1] >> 4u);
        out[i] = hex[nib];
    }
    out[chars] = '\0';
}

// -----------------------------------------------------------------------------
// Lectura
// -----------------------------------------------------------------------------
bool load(PersistentData* d) {
    const PersistentData* src =
        reinterpret_cast<const PersistentData*>(XIP_BASE + FLASH_TARGET_OFFSET);

    // Si la Flash está virgen (nunca escrita) lee todo 0xFF -> magic != nuestro magic.
    if (src->magic != PERSISTENT_MAGIC) {
        return false;
    }

    std::memcpy(d, src, sizeof(*d));

    // Si la versión del formato cambió (estructura distinta), se re-inicializa.
    if (d->version != PERSISTENT_VERSION) {
        return false;
    }

    // Verifica integridad recomputando el checksum.
    uint32_t saved = d->checksum;
    update_checksum(d);
    return (saved == d->checksum);
}

// -----------------------------------------------------------------------------
// Escritura
// -----------------------------------------------------------------------------
namespace {

// Parámetros que flash_safe_execute() pasa al callback.
struct WriteJob {
    uint32_t       flash_offs;
    const uint8_t* data;
    size_t         count;
};

// Callback ejecutado por flash_safe_execute() en zona segura (IRQ off, otro núcleo
// bloqueado). Aquí SÍ se puede llamar a flash_range_erase/program.
void do_write(void* param) {
    WriteJob* job = static_cast<WriteJob*>(param);
    flash_range_erase(job->flash_offs, FLASH_SECTOR_SIZE);
    flash_range_program(job->flash_offs, job->data, job->count);
}

} // namespace

bool save(const PersistentData& d) {
    // Copia los datos a un buffer alineado a sector y rellenado con 0xFF (así se
    // lee la Flash borrada). La estructura puede superar una página, por lo que el
    // tamaño a programar se redondea a un múltiplo de FLASH_PAGE_SIZE (256 B).
    static uint8_t page[FLASH_SECTOR_SIZE];
    std::memset(page, 0xFF, sizeof(page));
    std::memcpy(page, &d, sizeof(d));

    size_t count = (sizeof(d) + FLASH_PAGE_SIZE - 1u) & ~(size_t)(FLASH_PAGE_SIZE - 1u);
    WriteJob job = {FLASH_TARGET_OFFSET, page, count};

    // Ejecuta la escritura de forma segura entre núcleos.
    // 1000 ms es holgado para borrar+programar un sector/página.
    int rc = flash_safe_execute(do_write, &job, 1000);
    return (rc == PICO_OK);
}

// -----------------------------------------------------------------------------
// OTA: escrituras seguras en Flash (imagen candidata + metadatos)
// -----------------------------------------------------------------------------
// Reutiliza do_write (erase + program) dentro de flash_safe_execute. La imagen
// candidata se graba sector a sector (4096 B); los metadatos, en una página.
bool ota_write_sector(uint32_t offset, const uint8_t* data) {
    // `data` debe apuntar a 4096 bytes en RAM (el sector completo ya rellenado).
    WriteJob job = {offset, data, FLASH_SECTOR_SIZE};
    int rc = flash_safe_execute(do_write, &job, 5000);
    return (rc == PICO_OK);
}

bool ota_write_metadata(const ota_metadata_t* meta) {
    static uint8_t page[FLASH_PAGE_SIZE];
    std::memset(page, 0xFF, sizeof(page));
    std::memcpy(page, meta, sizeof(*meta));

    WriteJob job = {OTA_METADATA_OFFSET, page, FLASH_PAGE_SIZE};
    int rc = flash_safe_execute(do_write, &job, 5000);
    return (rc == PICO_OK);
}

} // namespace flash

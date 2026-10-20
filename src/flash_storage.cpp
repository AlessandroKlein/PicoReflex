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

namespace flash {

// -----------------------------------------------------------------------------
// Por defecto
// -----------------------------------------------------------------------------
void set_default(PersistentData* d) {
    std::memset(d, 0, sizeof(*d));
    d->magic = PERSISTENT_MAGIC;
    d->version = 1u;
    d->best_time_us = UINT32_MAX;   // "Sin récord" se representa con el máximo.
    const char* def = "JUGADOR";
    std::strncpy(d->user_name, def, USER_NAME_MAX - 1u);
    for (int i = 0; i < RANKING_SIZE; i++) {
        d->ranking[i].time_us = UINT32_MAX;
        std::strncpy(d->ranking[i].name, "-", USER_NAME_MAX - 1u);
    }
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
    // Copia los datos a un buffer de página alineado (256 bytes, cero-padded).
    static uint8_t page[FLASH_PAGE_SIZE];
    std::memset(page, 0xFF, sizeof(page));  // La Flash borrada se lee 0xFF.
    std::memcpy(page, &d, sizeof(d));

    WriteJob job = {FLASH_TARGET_OFFSET, page, FLASH_PAGE_SIZE};

    // Ejecuta la escritura de forma segura entre núcleos.
    // 1000 ms es holgado para borrar+programar un sector/página.
    int rc = flash_safe_execute(do_write, &job, 1000);
    return (rc == PICO_OK);
}

} // namespace flash

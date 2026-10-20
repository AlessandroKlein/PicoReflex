/**
 * @file   flash_storage.hpp
 * @brief  Almacenamiento no volátil (récords, nombre y última semilla) en Flash.
 *
 * La Pico W tiene 2 MB de Flash QSPI. Se reserva el ÚLTIMO sector (4096 bytes)
 * para guardar datos persistentes, lejos del firmware (que arranca en 0x10000000).
 *
 * Reglas del hardware_flash (ver hardware/flash.h):
 *  - flash_range_erase()  requiere offset alineado a 4096 y tamaño múltiplo de 4096.
 *  - flash_range_program() requiere offset alineado a 256 y tamaño múltiplo de 256.
 *  - Durante el borrado/programación NO puede ejecutarse código desde Flash en el
 *    OTRO núcleo. Por eso usamos flash_safe_execute() de pico_flash, que bloquea
 *    al otro núcleo (multicore_lockout) y deshabilita interrupciones.
 *
 * Referencias:
 *  - hardware_flash: https://www.raspberrypi.com/documentation/pico-sdk/hardware.html#hardware_flash
 *  - pico_flash (flash_safe_execute): https://www.raspberrypi.com/documentation/pico-sdk/runtime.html#pico_flash
 */

#ifndef PICO_REFLEX_FLASH_STORAGE_HPP
#define PICO_REFLEX_FLASH_STORAGE_HPP

#include "pico/stdlib.h"
#include "hardware/flash.h"

namespace flash {

// Nombre de usuario máximo (incluye el terminador '\0').
#define USER_NAME_MAX 16u
// Cantidad de récords en el ranking (top mejores tiempos).
#define RANKING_SIZE  5u

// -----------------------------------------------------------------------------
// Datos persistentes (debe caber en UNA página de 256 bytes)
// -----------------------------------------------------------------------------
struct PersistentData {
    uint32_t magic;              ///< Identificador de validez (PERSISTENT_MAGIC).
    uint32_t version;            ///< Versión del formato (1).

    char     user_name[USER_NAME_MAX]; ///< Nombre del jugador actual.

    uint32_t best_time_us;       ///< Mejor tiempo de reacción global (us).
    uint32_t last_seed;          ///< Última semilla usada.

    // Ranking de los mejores tiempos (el índice 0 es el mejor).
    struct RankEntry {
        uint32_t time_us;            ///< Tiempo de reacción en microsegundos.
        char     name[USER_NAME_MAX];///< Nombre asociado al récord.
    } ranking[RANKING_SIZE];

    uint32_t checksum;           ///< Suma simple de todos los bytes previos.
};

// Asegura en tiempo de compilación que quepa en una página de Flash.
static_assert(sizeof(PersistentData) <= FLASH_PAGE_SIZE,
              "PersistentData debe caber en 256 bytes (FLASH_PAGE_SIZE)");

// Valor mágico que identifica datos válidos ("REFL" en little-endian).
#define PERSISTENT_MAGIC 0x5245464Cu

// Offset del sector de datos (último sector de la Flash de 2 MB).
#define FLASH_TARGET_OFFSET (0x200000u - FLASH_SECTOR_SIZE)   // 0x1FF000

// Inicializa la estructura con valores por defecto.
void set_default(PersistentData* d);

// Carga los datos desde Flash. Devuelve true si son válidos (magic + checksum).
bool load(PersistentData* d);

// Guarda los datos en Flash (borra el sector y programa la página).
// Debe llamarse desde el núcleo que NO haya hecho flash_safe_execute_core_init().
// Devuelve true si la operación fue segura y exitosa.
bool save(const PersistentData& d);

// Calcula y actualiza el checksum de la estructura.
void update_checksum(PersistentData* d);

} // namespace flash

#endif // PICO_REFLEX_FLASH_STORAGE_HPP

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

#include "ota.h"   // ota_metadata_t (escritura de metadatos OTA).
#include "config.hpp"   // BatteryCalibration (persistida en Flash).

namespace flash {

// Nombre de usuario máximo (incluye el terminador '\0').
#define USER_NAME_MAX 16u
// Cantidad de récords en el ranking (top mejores tiempos).
#define RANKING_SIZE  5u
// Versión del formato de PersistentData (se incrementa al cambiar la estructura).
#define PERSISTENT_VERSION 5u

// -----------------------------------------------------------------------------
// Datos persistentes (deben caber en el sector reservado de 4 KB; el guardado
// redondea el tamaño a un múltiplo de FLASH_PAGE_SIZE = 256 B)
// -----------------------------------------------------------------------------
struct PersistentData {
    uint32_t magic;              ///< Identificador de validez (PERSISTENT_MAGIC).
    uint32_t version;            ///< Versión del formato (PERSISTENT_VERSION).

    char     user_name[USER_NAME_MAX]; ///< Nombre del jugador actual.

    uint32_t best_time_us;       ///< Mejor tiempo de reacción global (us).
    uint32_t last_seed;          ///< Última semilla usada.

    // Ranking de los mejores tiempos (el índice 0 es el mejor).
    struct RankEntry {
        uint32_t time_us;            ///< Tiempo de reacción en microsegundos.
        char     name[USER_NAME_MAX];///< Nombre asociado al récord.
    } ranking[RANKING_SIZE];

    // Configuración persistida (v0.2.0+): red y preferencias.
    char     wifi_ssid[33];      ///< SSID Wi-Fi (STA).
    char     wifi_password[65];  ///< Contraseña Wi-Fi (STA).
    char     ap_password[16];    ///< Clave del AP de respaldo (vacío = derivar del RUID).
    uint8_t  oled_enabled;       ///< 1 = OLED habilitado, 0 = apagado.
    uint32_t start_delay_s;      ///< Retardo de cuenta regresiva (segundos).

    // Calibración de la curva de descarga de batería (tabla por tramos + comp. de carga).
    BatteryCalibration batt_calib;

    uint8_t  display_controller; ///< 0 = SSD1306, 1 = SH1106 (controlador del OLED).

    uint32_t checksum;           ///< Suma simple de todos los bytes previos.
};

// Asegura en tiempo de compilación que quepa en el sector reservado (4 KB).
static_assert(sizeof(PersistentData) <= FLASH_SECTOR_SIZE,
              "PersistentData debe caber en el sector de Flash reservado");

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

// Deriva la clave por defecto del Access Point a partir del identificador único
// de la Flash (RUID, 8 bytes -> 15 caracteres hexadecimales + '\0'). Es única por
// placa y se usa cuando ap_password está vacío. `n` debe ser >= 16.
void ap_password_default(char* out, size_t n);

// -----------------------------------------------------------------------------
// OTA (actualización de firmware por aire) — escrituras seguras en Flash
// -----------------------------------------------------------------------------
// Escribe un sector completo (4096 B) en Flash: borra el sector destino y
// programa los 4096 bytes de `data` (debe apuntar a RAM). Usado para grabar la
// imagen candidata en el slot de staging, sector a sector.
// `offset` debe estar alineado a FLASH_SECTOR_SIZE.
bool ota_write_sector(uint32_t offset, const uint8_t* data);

// Escribe los metadatos OTA (una página de 256 B en el sector reservado).
// `meta` debe tener el checksum calculado (ota_meta_finalize).
bool ota_write_metadata(const ota_metadata_t* meta);

} // namespace flash

#endif // PICO_REFLEX_FLASH_STORAGE_HPP

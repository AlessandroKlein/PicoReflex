/* Región FLASH de la aplicación: relocalizada al slot de aplicación (A).
 *
 * La aplicación SIEMPRE se enlaza en 0x10010000 (offset 0x00010000), con 896 KB
 * de tamaño (0x000E0000). El bootloader (en 0x10000000) se encarga de copiar la
 * imagen nueva desde el staging (0x100F0000) hasta aquí antes de saltar a
 * 0x10010100 (tabla de vectores, tras el boot2 de 256 B).
 *
 * Este fichero se inyecta vía pico_add_linker_script_override_path() y sobreescribe
 * el pico_flash_region.ld generado por el SDK (que usa ORIGIN=0x10000000, 2 MB).
 */
FLASH(rx) : ORIGIN = 0x10010000, LENGTH = 0x000E0000

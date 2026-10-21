/**
 * @file   web_ui.hpp
 * @brief  Interfaz del "frontend": generación de las páginas HTML.
 *
 * Separa la presentación (este módulo) del backend HTTP (web_server.cpp), que
 * se encarga del ruteo, del parseo de parámetros y de la aplicación de la
 * configuración. `render()` SOLO lee el estado (config/estadísticas/red) y
 * produce HTML; nunca modifica nada.
 *
 * Rutas soportadas (ver web_server.cpp para el parseo de sus parámetros):
 *   "/"        -> Home: estadísticas + tabla de tiempos + controles.
 *   "/seed"    -> Configuración de la semilla.
 *   "/game"    -> Dificultad, modo, velocidad, duración de ronda, cuenta regresiva.
 *   "/display" -> Habilitar/deshabilitar el OLED.
 *   "/wifi"    -> Credenciales Wi-Fi (SSID/contraseña + mostrar/ocultar contraseña).
 */

#ifndef PICO_REFLEX_WEB_UI_HPP
#define PICO_REFLEX_WEB_UI_HPP

#include "pico/stdlib.h"

namespace web_ui {

// Escapa los caracteres especiales de HTML (< > & " ') para evitar XSS al
// insertar datos controlados por el usuario (nombre, SSID) en el HTML.
void html_escape(const char* src, char* dst, size_t n);

// Genera el HTML completo de la página para `path`. Devuelve false si `path`
// no es una ruta conocida.
bool render(const char* path, char* out, size_t out_len);

} // namespace web_ui

#endif // PICO_REFLEX_WEB_UI_HPP

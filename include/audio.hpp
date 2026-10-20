/**
 * @file   audio.hpp
 * @brief  Feedback sonoro del buzzer pasivo mediante PWM.
 *
 * El buzzer pasivo se controla con un slice PWM de la RP2040: la frecuencia del
 * tono la define el periodo del PWM (wrap) y el volumen/duty el nivel del canal.
 * Ver: https://www.raspberrypi.com/documentation/pico-sdk/hardware.html#hardware_pwm
 */

#ifndef PICO_REFLEX_AUDIO_HPP
#define PICO_REFLEX_AUDIO_HPP

#include "pico/stdlib.h"

namespace audio {

// Inicializa el pin del buzzer como salida PWM. Debe llamarse una vez.
void init();

// Reproduce un tono de frecuencia `freq_hz` durante `duration_ms`, bloqueando.
// Un buzzer pasivo solo "suena" mientras recibe una onda cuadrada; por eso el
// tono se genera con el duty al 50% y se apaga al terminar.
void play_tone(uint32_t freq_hz, uint32_t duration_ms);

// --- Melodías / efectos predefinidos -----------------------------------------
void play_hit();          ///< Tonito ascendente: respuesta correcta.
void play_miss();         ///< Tono grave sostenido: respuesta incorrecta.
void play_timeout();      ///< Doble pitido corto: se agotó el tiempo.
void play_countdown();    ///< Cuenta regresiva (3-2-1) antes de iniciar.
void play_game_over();    ///< Melodía descendente de fin de ronda.

} // namespace audio

#endif // PICO_REFLEX_AUDIO_HPP

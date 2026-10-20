// Mock de config.hpp para compilar web_ui en el host (x86).
// Reproduce EXACTAMENTE los tipos/estructuras que usa web_ui.cpp, sin
// dependencias del Pico SDK. Las funciones accesorias se implementan en main.cpp.
#ifndef PICO_REFLEX_CONFIG_HPP
#define PICO_REFLEX_CONFIG_HPP

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

enum class GameMode : uint8_t {
    FREE_PLAY = 0, TIMED = 1, SUDDEN_DEATH = 2, MODE_COUNT = 3
};

enum class Difficulty : uint8_t {
    EASY = 0, NORMAL = 1, HARD = 2, DIFF_COUNT = 3
};

struct GameConfig {
    uint32_t seed = 0;
    Difficulty difficulty = Difficulty::NORMAL;
    GameMode mode = GameMode::FREE_PLAY;
    uint32_t round_seconds = 30;
    uint32_t stimulus_min_ms = 500;
    uint32_t stimulus_max_ms = 1500;
    char user_name[16] = "JUGADOR";
    uint32_t start_delay_s = 3;
    bool oled_enabled = true;
    char wifi_ssid[33] = "TU_RED_WIFI";
    char wifi_password[65] = "TU_PASSWORD";
};

struct GameStats {
    uint32_t total_rounds = 0;
    uint32_t hits = 0;
    uint32_t misses = 0;
    uint32_t timeouts = 0;
    uint32_t best_time_us = UINT32_MAX;
    uint32_t worst_time_us = 0;
    uint64_t total_time_us = 0;
    uint32_t valid_reactions = 0;
    bool round_active = false;
    static constexpr uint32_t HISTORY_SIZE = 20;
    uint32_t history[HISTORY_SIZE] = {0};
    uint32_t history_count = 0;
};

struct SystemStatus {
    uint32_t battery_mv = 0;
    uint8_t battery_pct = 0;
    bool wifi_sta = false;
    bool wifi_ap = false;
    char ip[16] = "0.0.0.0";
};

GameConfig config_get();
GameStats stats_get();
SystemStatus system_status_get();
uint32_t difficulty_tolerance_ms(Difficulty d);
const char* game_mode_name(GameMode m);
const char* difficulty_name(Difficulty d);

#endif

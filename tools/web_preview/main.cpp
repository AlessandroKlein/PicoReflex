// Harness de prueba del "frontend" (web_ui) en el host (x86).
//
// Compila src/web_ui.cpp con este main y los mocks de tools/web_preview/mock,
// genera el HTML de cada ruta y lo guarda en web_preview_*.html para inspección.
//
// Uso:
//   g++ -std=c++17 -I tools/web_preview/mock -I include \
//       src/web_ui.cpp tools/web_preview/main.cpp -o web_preview.exe
//   ./web_preview.exe

#include "config.hpp"
#include "web_ui.hpp"

#include <cstdio>
#include <cstring>

// -----------------------------------------------------------------------------
// Mock de los accesores de estado (datos de ejemplo, incluyendo un caso XSS)
// -----------------------------------------------------------------------------
static GameConfig g_cfg;
static bool g_xss_mode = false;

GameConfig config_get() { return g_cfg; }

GameStats stats_get() {
    GameStats s;
    s.total_rounds = 50;
    s.hits = 42;
    s.misses = 5;
    s.timeouts = 3;
    s.best_time_us = 123000;
    s.worst_time_us = 812000;
    s.total_time_us = 25000000ULL;
    s.valid_reactions = 42;
    s.round_active = true;
    // Historial de ejemplo (más reciente primero).
    for (uint32_t i = 0; i < 12; i++) {
        s.history[i].time_ms    = 210 + i * 35;
        s.history[i].seed       = 12345u;
        s.history[i].difficulty = (i % 3 == 0) ? Difficulty::HARD
                                 : (i % 3 == 1) ? Difficulty::NORMAL
                                                : Difficulty::EASY;
    }
    s.history_count = 12;
    return s;
}

SystemStatus system_status_get() {
    SystemStatus st;
    st.battery_mv = 3890;
    st.battery_pct = 74;
    st.wifi_ap = true;
    st.wifi_sta = false;
    std::strcpy(st.ip, "192.168.4.1");
    return st;
}

uint32_t difficulty_tolerance_ms(Difficulty d) {
    switch (d) {
        case Difficulty::EASY:   return 1000u;
        case Difficulty::NORMAL: return 500u;
        case Difficulty::HARD:   return 250u;
        default:                 return 500u;
    }
}

const char* game_mode_name(GameMode m) {
    switch (m) {
        case GameMode::FREE_PLAY:    return "LIBRE";
        case GameMode::TIMED:        return "CONTRA";
        case GameMode::SUDDEN_DEATH: return "MUERTE";
        default:                     return "?";
    }
}

const char* difficulty_name(Difficulty d) {
    switch (d) {
        case Difficulty::EASY:   return "FACIL";
        case Difficulty::NORMAL: return "NORMAL";
        case Difficulty::HARD:   return "DIFICIL";
        default:                 return "?";
    }
}

// -----------------------------------------------------------------------------
int main() {
    const char* routes[] = {"/", "/seed", "/game", "/display", "/wifi", "/player"};
    const char* names[]  = {"home", "seed", "game", "display", "wifi", "player"};

    for (int i = 0; i < 6; i++) {
        char buf[16384];
        bool ok = web_ui::render(routes[i], buf, sizeof(buf));

        char fname[64];
        std::snprintf(fname, sizeof(fname), "web_preview_%s.html", names[i]);
        FILE* f = std::fopen(fname, "w");
        if (f) { std::fputs(buf, f); std::fclose(f); }

        std::printf("%-10s -> %-7s (%5d bytes) -> %s\n",
                    routes[i], ok ? "OK" : "UNKNOWN", (int)std::strlen(buf), fname);
    }

    // --- Prueba de XSS: nombre malicioso ---
    std::strcpy(g_cfg.user_name, "'><script>alert(1)</script>");
    std::strcpy(g_cfg.wifi_ssid, "red\"><img src=x onerror=alert(1)>");
    char xss[16384];
    web_ui::render("/", xss, sizeof(xss));
    bool injected = (std::strstr(xss, "<script>") != nullptr) ||
                    (std::strstr(xss, "onerror=") != nullptr);
    FILE* f = std::fopen("web_preview_xss.html", "w");
    if (f) { std::fputs(xss, f); std::fclose(f); }
    std::printf("\n[XSS] inyeccion sin escapar: %s (ver web_preview_xss.html)\n",
                injected ? "DETECTADA (bug)" : "NO (escapado correcto)");

    return 0;
}

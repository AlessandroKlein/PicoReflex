// blink — pico-examples/pico_w/blink STOCK (SDK 2.3.0).
// Sin FreeRTOS, sin lwIP, sin PicoReflex. Solo USB+LED+serial:
//   stdio_init_all() -> cyw43_arch_init() -> parpadeo del LED onboard (CYW43_WL_GPIO_LED_PIN).
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

int main(void) {
    stdio_init_all();
    if (cyw43_arch_init()) {
        printf("Wi-Fi init failed");
        return -1;
    }
    while (true) {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
        sleep_ms(250);
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
        sleep_ms(250);
    }
}

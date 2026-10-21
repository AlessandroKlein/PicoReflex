# Referencias — PicoReflex

Lista completa de referencias oficiales usadas y verificadas para el desarrollo
del proyecto. Cada decisión de `DECISIONES.md` y cada módulo del código remite a
una de estas fuentes.

## 1. Documentación oficial Raspberry Pi Pico SDK

| Tema | URL |
| :--- | :--- |
| Índice general del SDK | <https://www.raspberrypi.com/documentation/pico-sdk/> |
| Hardware (GPIO/ADC/PWM/I2C/DMA) | <https://www.raspberrypi.com/documentation/pico-sdk/hardware.html> |
| `hardware_gpio` | <https://www.raspberrypi.com/documentation/pico-sdk/hardware.html#hardware_gpio> |
| `hardware_adc` | <https://www.raspberrypi.com/documentation/pico-sdk/hardware.html#hardware_adc> |
| `hardware_pwm` | <https://www.raspberrypi.com/documentation/pico-sdk/hardware.html#hardware_pwm> |
| `hardware_i2c` | <https://www.raspberrypi.com/documentation/pico-sdk/hardware.html#hardware_i2c> |
| `hardware_dma` | <https://www.raspberrypi.com/documentation/pico-sdk/hardware.html#hardware_dma> |
| Runtime (`pico_multicore`, `pico_flash`, `queue`) | <https://www.raspberrypi.com/documentation/pico-sdk/runtime.html> |
| `pico_multicore` | <https://www.raspberrypi.com/documentation/pico-sdk/runtime.html#pico_multicore> |
| `pico_flash` | <https://www.raspberrypi.com/documentation/pico-sdk/runtime.html#pico_flash> |
| Networking (`cyw43_arch`, lwIP) | <https://www.raspberrypi.com/documentation/pico-sdk/networking.html> |
| Third-party (lwIP, FreeRTOS-Kernel) | <https://www.raspberrypi.com/documentation/pico-sdk/third_party.html> |
| Datasheet Pico W | <https://datasheets.raspberrypi.com/picow/pico-w-datasheet.pdf> |

## 2. Ejemplos oficiales (raspberrypi/pico-examples)

Repositorio: <https://github.com/raspberrypi/pico-examples>

### 2.1 Access Point (corroborado — coincide con PicoReflex)

El ejemplo `pico_w/wifi/access_point` levanta un AP y usa:
- `dhcpserver` (servidor DHCP) + `dnsserver` (DNS captive portal) + `httpd` (lwIP).

PicoReflex implementa lo mismo: `src/dhcpserver.c` (copiado de MicroPython/pico-examples)
+ `src/captive_portal.cpp` (DNS) + servidor HTTP con API raw de lwIP.

| Archivo | URL |
| :--- | :--- |
| `picow_access_point.c` | <https://raw.githubusercontent.com/raspberrypi/pico-examples/refs/heads/master/pico_w/wifi/access_point/picow_access_point.c> |
| `dhcpserver.c` | <https://raw.githubusercontent.com/raspberrypi/pico-examples/refs/heads/master/pico_w/wifi/access_point/dhcpserver/dhcpserver.c> |
| `dhcpserver.h` | <https://raw.githubusercontent.com/raspberrypi/pico-examples/refs/heads/master/pico_w/wifi/access_point/dhcpserver/dhcpserver.h> |
| `dnsserver.c` | <https://raw.githubusercontent.com/raspberrypi/pico-examples/refs/heads/master/pico_w/wifi/access_point/dnsserver/dnsserver.c> |

### 2.2 Wi-Fi provisioning (corroborado — flujo STA↔AP)

`pico_w/wifi/access_point_wifi_provisioning` hace el flujo completo que PicoReflex
replica: intenta **STA** con credenciales guardadas en Flash y, si falla, levanta
**AP** para configurarlas; al guardar, persiste en Flash y reconecta.

- <https://raw.githubusercontent.com/raspberrypi/pico-examples/refs/heads/master/pico_w/wifi/access_point_wifi_provisioning/AP_provisioning.c>

### 2.3 FreeRTOS (corroborado — patrón usado en v0.5.0)

`pico_w/wifi/freertos` demuestra Wi-Fi bajo FreeRTOS con dos variantes de enlace:
- `threadsafe_background` (NO_SYS=1) y `sys_freertos` (NO_SYS=0).

**Patrón clave** (que PicoReflex sigue): `cyw43_arch_init()` se llama **dentro de
una tarea** (`main_task`), creada antes del scheduler:

```c
void main_task(void *params) {
    if (cyw43_arch_init()) { /* error */ }
    cyw43_arch_enable_sta_mode();
    ...
    while (true) vTaskDelay(100);
}
void vLaunch(void) {
    xTaskCreate(main_task, "TestMainThread", configMINIMAL_STACK_SIZE, NULL, TEST_TASK_PRIORITY, &task);
    vTaskStartScheduler();
}
```

| Archivo | URL |
| :--- | :--- |
| Directorio FreeRTOS | <https://github.com/raspberrypi/pico-examples/tree/master/pico_w/wifi/freertos> |
| iperf | <https://raw.githubusercontent.com/raspberrypi/pico-examples/refs/heads/master/pico_w/wifi/freertos/iperf/picow_freertos_iperf.c> |
| httpd | <https://raw.githubusercontent.com/raspberrypi/pico-examples/refs/heads/master/pico_w/wifi/freertos/httpd/pico_freertos_httpd.c> |

### 2.4 Lista de ejemplos `pico_w/wifi` (referencia)

- `blink` — LED onboard del CYW43 (mínimo).
- `access_point` — AP + DHCP + DNS + httpd.
- `access_point_wifi_provisioning` — AP para configurar credenciales y conectar STA.
- `freertos` — Wi-Fi bajo FreeRTOS (httpd, iperf, http_client, ntp, ping).
- `http_client` — cliente HTTPS.
- `httpd` — servidor HTTP lwIP (SSI/CGI).
- `iperf` — benchmark TCP/UDP.
- `mqtt` — cliente MQTT.
- `ntp_client` / `ntp_system_time` — NTP.
- `ota_update` — actualización over-the-air.
- `tcp_client` / `tcp_server` — sockets TCP.
- `tls_client` — TLS con mbedTLS.
- `udp_beacon` — beacon UDP.
- `wifi_scan` — escaneo de redes.

## 3. Bibliotecas de terceros

| Biblioteca | Repositorio |
| :--- | :--- |
| FreeRTOS-Kernel (V11.1.0) | <https://github.com/FreeRTOS/FreeRTOS-Kernel> |
| lwIP | (incluido en el SDK) `lib/lwip` |
| cyw43-driver | (incluido en el SDK) `lib/cyw43-driver` |

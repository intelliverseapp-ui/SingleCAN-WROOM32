#include "singlecan_leds.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

//
// SingleCAN LED subsystem
// ESP32-WROOM32 DevKit V1 has ONE onboard LED on GPIO2
//

static const char *TAG = "SingleCAN_LEDS";

#define SINGLECAN_LED_GPIO   2   // Onboard LED

// ------------------------------------------------------------
// Initialization
// ------------------------------------------------------------
void singlecan_leds_init(void)
{
    ESP_LOGI(TAG, "Initializing SingleCAN LED subsystem (GPIO2)...");

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << SINGLECAN_LED_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    gpio_config(&io_conf);

    gpio_set_level(SINGLECAN_LED_GPIO, 0);

    ESP_LOGI(TAG, "SingleCAN LED ready");
}

// ------------------------------------------------------------
// Basic LED control
// ------------------------------------------------------------
void led_set_red(void)
{
    gpio_set_level(SINGLECAN_LED_GPIO, 1);
}

void led_set_green(void)
{
    // No green LED available — blink to simulate "green"
    for (int i = 0; i < 2; i++) {
        gpio_set_level(SINGLECAN_LED_GPIO, 1);
        vTaskDelay(pdMS_TO_TICKS(100));
        gpio_set_level(SINGLECAN_LED_GPIO, 0);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void led_set_off(void)
{
    gpio_set_level(SINGLECAN_LED_GPIO, 0);
}

// ------------------------------------------------------------
// Automotive Status API (mapped to simple LED behavior)
// ------------------------------------------------------------

// CAN idle = slow blink
void singlecan_leds_can_idle(void)
{
    gpio_set_level(SINGLECAN_LED_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(200));
    gpio_set_level(SINGLECAN_LED_GPIO, 0);
}

// CAN RX = short blink
void singlecan_leds_can_rx_active(void)
{
    gpio_set_level(SINGLECAN_LED_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(SINGLECAN_LED_GPIO, 0);
}

// CAN TX = medium blink
void singlecan_leds_can_tx_active(void)
{
    gpio_set_level(SINGLECAN_LED_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(SINGLECAN_LED_GPIO, 0);
}

// Wi-Fi AP down = LED off
void singlecan_leds_wifi_ap_down(void)
{
    led_set_off();
}

// Wi-Fi AP up = two blinks
void singlecan_leds_wifi_ap_up(void)
{
    for (int i = 0; i < 2; i++) {
        gpio_set_level(SINGLECAN_LED_GPIO, 1);
        vTaskDelay(pdMS_TO_TICKS(150));
        gpio_set_level(SINGLECAN_LED_GPIO, 0);
        vTaskDelay(pdMS_TO_TICKS(150));
    }
}

// TCP server down = long blink
void singlecan_leds_tcp_server_down(void)
{
    gpio_set_level(SINGLECAN_LED_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(300));
    gpio_set_level(SINGLECAN_LED_GPIO, 0);
}

// TCP server up = triple blink
void singlecan_leds_tcp_server_up(void)
{
    for (int i = 0; i < 3; i++) {
        gpio_set_level(SINGLECAN_LED_GPIO, 1);
        vTaskDelay(pdMS_TO_TICKS(100));
        gpio_set_level(SINGLECAN_LED_GPIO, 0);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// Error = solid ON
void singlecan_leds_error(void)
{
    gpio_set_level(SINGLECAN_LED_GPIO, 1);
}

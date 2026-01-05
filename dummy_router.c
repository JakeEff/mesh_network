#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_log.h"

#define ROUTER_SSID "VirtualMeshRouter"
#define ROUTER_PASS "meshpass"
#define ROUTER_CHANNEL 6

static const char *TAG = "VIRTUAL_ROUTER";

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Create default Wi-Fi AP network interface
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t ap_config = {
        .ap = {
            .ssid = ROUTER_SSID,
            .ssid_len = strlen(ROUTER_SSID),
            .channel = ROUTER_CHANNEL,
            .password = ROUTER_PASS,
            .max_connection = 10,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK,
        },
    };

    // Allow open network if password length < 8
    if (strlen(ROUTER_PASS) < 8) {
        ap_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Virtual Router started");
    ESP_LOGI(TAG, "SSID: %s | Password: %s | Channel: %d",
             ROUTER_SSID, ROUTER_PASS, ROUTER_CHANNEL);

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

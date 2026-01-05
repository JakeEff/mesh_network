#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_mesh.h"
#include "esp_mac.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h"

#define MESH_ID     {0x11, 0x22, 0x33, 0x44, 0x55, 0x66}
#define MESH_CHANNEL 6
#define INPUT_GPIO   GPIO_NUM_25   

static const char *TAG = "MESH_APP";

static void mesh_event_handler(void *arg, esp_event_base_t base, int32_t id, void *event_data)
{
    switch (id) {
        case MESH_EVENT_STARTED:
            ESP_LOGI(TAG, "MESH started");
            break;
        case MESH_EVENT_PARENT_CONNECTED:
            ESP_LOGI(TAG, "Parent connected");
            break;
        case MESH_EVENT_CHILD_CONNECTED:
            ESP_LOGI(TAG, "Child connected");
            break;
        case MESH_EVENT_CHILD_DISCONNECTED:
            ESP_LOGI(TAG, "Child disconnected");
            break;
        default:
            break;
    }
}

static void wifi_mesh_init(void)
{
    //wifi init
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    //mesh init
    ESP_ERROR_CHECK(esp_mesh_init());
    esp_event_handler_register(MESH_EVENT, ESP_EVENT_ANY_ID, &mesh_event_handler, NULL);

    mesh_cfg_t mesh_config = MESH_INIT_CONFIG_DEFAULT();
    uint8_t mesh_id[6] = MESH_ID;
    memcpy(mesh_config.mesh_id.addr, mesh_id, 6);
    mesh_config.channel = MESH_CHANNEL;

    //router settings
    const char *router_ssid = "VirtualMeshRouter";
    const char *router_pass = "meshpass";
    strncpy((char *)mesh_config.router.ssid, router_ssid, sizeof(mesh_config.router.ssid));
    mesh_config.router.ssid_len = strlen(router_ssid);
    strncpy((char *)mesh_config.router.password, router_pass, sizeof(mesh_config.router.password));

    mesh_config.mesh_ap.max_connection = 6;
    strncpy((char *)mesh_config.mesh_ap.password, "meshpass", sizeof(mesh_config.mesh_ap.password));

    ESP_ERROR_CHECK(esp_mesh_set_self_organized(true, true));

    ESP_ERROR_CHECK(esp_mesh_set_config(&mesh_config));
    ESP_ERROR_CHECK(esp_mesh_start());

    ESP_LOGI(TAG, "Mesh initialization complete (Router-less mode)");
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    wifi_mesh_init();

    //gpio setup
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << INPUT_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE
    };
    gpio_config(&io_conf);

    ESP_LOGI(TAG, "This node is the LEAF. Waiting for GPIO25 HIGH...");

    //data setup
    const char *msg = "Hello from the Leaf Node!";
    mesh_data_t data;
    data.data = (uint8_t*)msg;
    data.size = strlen(msg) + 1;
    data.proto = MESH_PROTO_BIN;
    data.tos = MESH_TOS_P2P;

    //wait for mesh
    vTaskDelay(pdMS_TO_TICKS(8000));

    while (1) {
        int level = gpio_get_level(INPUT_GPIO);

        if (level == 1) {
            //pin is high, send message
            esp_err_t err = esp_mesh_send(NULL, &data, 0, NULL, 0);

            if (err == ESP_OK) {
                ESP_LOGI(TAG, "GPIO25 HIGH → Message sent: %s", msg);
            } else {
                ESP_LOGW(TAG, "Send FAILED: %s", esp_err_to_name(err));
            }

            vTaskDelay(pdMS_TO_TICKS(1000));  //debounce
        }

        vTaskDelay(pdMS_TO_TICKS(50));  // small poll delay
    }
}







// #include <string.h>
// #include "freertos/FreeRTOS.h"
// #include "freertos/task.h"
// #include "esp_wifi.h"
// #include "esp_event.h"
// #include "esp_mesh.h"
// #include "esp_mac.h"
// #include "esp_log.h"
// #include "nvs_flash.h"


// #define MESH_ID     {0x11, 0x22, 0x33, 0x44, 0x55, 0x66}
// #define MESH_CHANNEL 6

// static const char *TAG = "MESH_APP";

// static void mesh_event_handler(void *arg, esp_event_base_t base, int32_t id, void *event_data)
// {
//     switch (id) {
//         case MESH_EVENT_STARTED:
//             ESP_LOGI(TAG, "MESH started");
//             break;
//         case MESH_EVENT_PARENT_CONNECTED:
//             ESP_LOGI(TAG, "Parent connected");
//             break;
//         case MESH_EVENT_CHILD_CONNECTED:
//             ESP_LOGI(TAG, "Child connected");
//             break;
//         case MESH_EVENT_CHILD_DISCONNECTED:
//             ESP_LOGI(TAG, "Child disconnected");
//             break;
//         default:
//             break;
//     }
// }

// static void wifi_mesh_init(void)
// {
//       // --- Standard Wi-Fi + event init ---
//     ESP_ERROR_CHECK(esp_netif_init());
//     ESP_ERROR_CHECK(esp_event_loop_create_default());
//     wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
//     ESP_ERROR_CHECK(esp_wifi_init(&cfg));
//     ESP_ERROR_CHECK(esp_wifi_start());

//     // --- Mesh initialization ---
//     ESP_ERROR_CHECK(esp_mesh_init());
//     esp_event_handler_register(MESH_EVENT, ESP_EVENT_ANY_ID, &mesh_event_handler, NULL);

//      mesh_cfg_t mesh_config = MESH_INIT_CONFIG_DEFAULT();
//     uint8_t mesh_id[6] = MESH_ID;
//     memcpy(mesh_config.mesh_id.addr, mesh_id, 6);
//     mesh_config.channel = MESH_CHANNEL;

//     const char *router_ssid = "VirtualMeshRouter";
//     const char *router_pass = "meshpass";
//     strncpy((char *)mesh_config.router.ssid, router_ssid, sizeof(mesh_config.router.ssid));
//     mesh_config.router.ssid_len = strlen(router_ssid);
//     strncpy((char *)mesh_config.router.password, router_pass, sizeof(mesh_config.router.password));
//     mesh_config.channel = 6;  // must match the AP channel

//     // --- Mesh soft-AP settings so children can connect ---
//     mesh_config.mesh_ap.max_connection = 6;
//     strncpy((char *)mesh_config.mesh_ap.password, "meshpass", sizeof(mesh_config.mesh_ap.password));

//     // --- Enable router-less self-organization ---
//     ESP_ERROR_CHECK(esp_mesh_set_self_organized(true, true));

//     // --- Apply configuration and start mesh ---
//     ESP_ERROR_CHECK(esp_mesh_set_config(&mesh_config));
//     ESP_ERROR_CHECK(esp_mesh_start());

//     ESP_LOGI(TAG, "Mesh initialization complete (Router-less mode)");

// }

// //LEAF
// void app_main(void)
// {
//     ESP_ERROR_CHECK(nvs_flash_init());
//     wifi_mesh_init();

//     //ESP_ERROR_CHECK(esp_mesh_set_type(MESH_NODE));
//     ESP_LOGI(TAG, "This node is the LEAF");

//     const char *msg = "Hello from the Leaf Node!";
//     mesh_data_t data;
//     data.data = (uint8_t*)msg;
//     data.size = strlen(msg) + 1;
//     data.proto = MESH_PROTO_BIN;
//     data.tos = MESH_TOS_P2P;

//     // Wait for the leaf to connect to a parent
//     vTaskDelay(10000 / portTICK_PERIOD_MS);

//     while (1) {
//         esp_err_t err = esp_mesh_send(NULL, &data, 0, NULL, 0);
//         if (err == ESP_OK) {
//             ESP_LOGI(TAG, "Message sent to parent: %s", msg);
//         } else {
//             ESP_LOGW(TAG, "Send failed: %s", esp_err_to_name(err));
//         }
//         vTaskDelay(5000 / portTICK_PERIOD_MS);
//     }
// }
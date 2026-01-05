#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_mesh.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "driver/i2c_master.h"
#include "ssd1306.h"

#define MESH_ID        {0x11, 0x22, 0x33, 0x44, 0x55, 0x66}
#define MESH_CHANNEL   6

#define OLED_SDA_GPIO  21
#define OLED_SCL_GPIO  22

static const char *TAG = "MESH_APP";

static ssd1306_handle_t g_oled = NULL;
static i2c_master_bus_handle_t g_i2c_bus = NULL;

static void oled_init(void)
{
    //setup
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = OLED_SDA_GPIO,
        .scl_io_num = OLED_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true, 
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &g_i2c_bus));

    //ssd1306 library setup  > GPT
    ssd1306_config_t cfg = I2C_SSD1306_128x64_CONFIG_DEFAULT;
    ESP_ERROR_CHECK(ssd1306_init(g_i2c_bus, &cfg, &g_oled));
    ssd1306_clear_display(g_oled, false);
    ssd1306_display_text(g_oled, 0, "Mesh receiver", false);
}


static void oled_show_packet(const mesh_addr_t *from, const char *msg)
{
    char mac_line1[17];
    char mac_line2[17];

    snprintf(mac_line1, sizeof(mac_line1),
             "%02X:%02X:%02X:%02X",
             from->addr[0], from->addr[1],
             from->addr[2], from->addr[3]);

    snprintf(mac_line2, sizeof(mac_line2),
             "%02X:%02X",
             from->addr[4], from->addr[5]);

    ssd1306_clear_display(g_oled, false);

    //display mac
    ssd1306_display_text(g_oled, 0, mac_line1, false);
    ssd1306_display_text(g_oled, 1, mac_line2, false);

    //seperate
    ssd1306_display_text(g_oled, 2, "----------------", false);

    //display message
    char line3[17] = {0};
    char line4[17] = {0};

    strncpy(line3, msg, 16);
    if (strlen(msg) > 16) {
        strncpy(line4, msg + 16, 16);
    }

    ssd1306_display_text(g_oled, 3, line3, false);
    if (line4[0]) {
        ssd1306_display_text(g_oled, 4, line4, false);
    }
}


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

    const char *router_ssid = "VirtualMeshRouter";
    const char *router_pass = "meshpass";
    strncpy((char *)mesh_config.router.ssid, router_ssid, sizeof(mesh_config.router.ssid));
    mesh_config.router.ssid_len = strlen(router_ssid);
    strncpy((char *)mesh_config.router.password, router_pass, sizeof(mesh_config.router.password));
    mesh_config.channel = 6;  //router channel

    //how many children can each node have
    mesh_config.mesh_ap.max_connection = 6;
    //set password for this nodes children
    strncpy((char *)mesh_config.mesh_ap.password, "meshpass", sizeof(mesh_config.mesh_ap.password));

    //self organisation of network. 
    ESP_ERROR_CHECK(esp_mesh_set_self_organized(true, true));

    //apply config
    ESP_ERROR_CHECK(esp_mesh_set_config(&mesh_config));
    ESP_ERROR_CHECK(esp_mesh_start());

    ESP_LOGI(TAG, "Mesh initialization complete (Router-less mode)");

}


static void mac_to_str(const uint8_t mac[6], char *out, size_t out_len)
{
    snprintf(out, out_len, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

// BRANCH / ROOT
void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    wifi_mesh_init();

    //force receiver to become root. 
    ESP_ERROR_CHECK(esp_mesh_set_type(MESH_ROOT));
    ESP_LOGI(TAG, "This node is the ROOT (branch)");

    oled_init();

    mesh_addr_t from;
    mesh_data_t data = {0};
    data.data = malloc(128);
    data.size = 128;
    data.proto = MESH_PROTO_BIN;
    data.tos = MESH_TOS_P2P;

    // while (1) {
    //     int flag = 0;
    //     esp_err_t err = esp_mesh_recv(&from, &data, portMAX_DELAY, &flag, NULL, 0);
    //     if (err == ESP_OK) {
    //         const char *rx = (const char *)data.data;
    //         ESP_LOGI(TAG, "Received: %s", rx);

    //         if (g_oled) {
    //             oled_show_packet(&from, rx);
    //         }
    //     }
    // }

    while (1) {
    int flag = 0;

    data.size = 128;  // IMPORTANT: reset capacity before each recv
    esp_err_t err = esp_mesh_recv(&from, &data, portMAX_DELAY, &flag, NULL, 0);

    if (err == ESP_OK) {
        // Make payload safe to print as a string
        size_t n = (data.size < 127) ? data.size : 127;
        ((char *)data.data)[n] = '\0';

        char mac_str[18];
        mac_to_str(from.addr, mac_str, sizeof(mac_str));

        ESP_LOGI(TAG, "RX from %s: %s", mac_str, (char *)data.data);

        if (g_oled) {
            oled_show_packet(&from, (char *)data.data);
        }
    } else {
        ESP_LOGW(TAG, "esp_mesh_recv failed: %s", esp_err_to_name(err));
    }
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

// static void mac_to_str(const uint8_t mac[6], char *out, size_t out_len)
// {
//     snprintf(out, out_len, "%02X:%02X:%02X:%02X:%02X:%02X",
//              mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
// }


// void app_main(void)
// {
//     ESP_ERROR_CHECK(nvs_flash_init());
//     wifi_mesh_init();

//     ESP_ERROR_CHECK(esp_mesh_set_type(MESH_ROOT));
//     ESP_LOGI(TAG, "This node is the ROOT (branch)");

//     mesh_addr_t from;
//     mesh_data_t data;

//     uint8_t *rx_buf = malloc(128);
//     if (!rx_buf) {
//         ESP_LOGE(TAG, "malloc failed");
//         return;
//     }

//     data.data = rx_buf;
//     data.proto = MESH_PROTO_BIN;
//     data.tos   = MESH_TOS_P2P;

//     while (1) {
//         int flag = 0;
//         data.size = 128;  // capacity before recv

//         esp_err_t err = esp_mesh_recv(&from, &data, portMAX_DELAY, &flag, NULL, 0);

//         if (err == ESP_OK) {
//             char mac_str[18];
//             mac_to_str(from.addr, mac_str, sizeof(mac_str));

//             // null terminate safely
//             size_t n = (data.size < 127) ? data.size : 127;
//             rx_buf[n] = '\0';

//             ESP_LOGI(TAG, "Received from %s: %s", mac_str, (char *)rx_buf);
//         } else {
//             ESP_LOGW(TAG, "esp_mesh_recv failed: %s", esp_err_to_name(err));
//         }
//     }
// }



// //BRANCH
// void app_main(void)
// {
//     ESP_ERROR_CHECK(nvs_flash_init());
//     wifi_mesh_init();

//     ESP_ERROR_CHECK(esp_mesh_set_type(MESH_ROOT));
//     ESP_LOGI(TAG, "This node is the ROOT (branch)");

//     mesh_addr_t from;
//     mesh_data_t data;
//     data.data = malloc(128);
//     data.size = 128;
//     data.proto = MESH_PROTO_BIN;
//     data.tos = MESH_TOS_P2P;

//     while (1) {
//         int flag = 0;
//         esp_err_t err = esp_mesh_recv(&from, &data, portMAX_DELAY, &flag, NULL, 0);

//         if (err == ESP_OK) {
//             ESP_LOGI(TAG, "Received from leaf: %s", (char*)data.data);
//         }
//     }
// }

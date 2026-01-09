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

typedef struct __attribute__((packed)) {
    uint8_t  src_mac[6];    //MAC address of sender for after root
    int16_t  tilt_measurement;  //tilt measurement
    uint16_t moisture_measurement;  //moisture measurement
} sensor_payload_t;

static void oled_init(void)
{
    //I2C library setup from GPT 
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = OLED_SDA_GPIO,
        .scl_io_num = OLED_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true, 
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &g_i2c_bus));

    //ssd1306 library setup from GPT
    ssd1306_config_t cfg = I2C_SSD1306_128x64_CONFIG_DEFAULT;
    ESP_ERROR_CHECK(ssd1306_init(g_i2c_bus, &cfg, &g_oled));
    ssd1306_clear_display(g_oled, false);
    ssd1306_display_text(g_oled, 0, "Mesh receiver", false);
}


static void oled_show_sensor(const sensor_payload_t *p)
{
    char line0[17], line1[17], line2[17], line3[17];

    //MAC across both lines
    snprintf(line0, sizeof(line0), "%02X:%02X:%02X:%02X",
             p->src_mac[0], p->src_mac[1], p->src_mac[2], p->src_mac[3]);

    snprintf(line1, sizeof(line1), "%02X:%02X",
             p->src_mac[4], p->src_mac[5]);

    snprintf(line2, sizeof(line2), "Tilt: %d", (int)p->tilt_measurement);
    snprintf(line3, sizeof(line3), "Moist: %u", (unsigned)p->moisture_measurement);

    ssd1306_clear_display(g_oled, false);
    ssd1306_display_text(g_oled, 0, line0, false);
    ssd1306_display_text(g_oled, 1, line1, false);
    ssd1306_display_text(g_oled, 2, "----------------", false);
    ssd1306_display_text(g_oled, 3, line2, false);
    ssd1306_display_text(g_oled, 4, line3, false);
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

    while (1) {
        int flag = 0;

        data.size = 128;  //if this isn't here it won't work. 
        esp_err_t err = esp_mesh_recv(&from, &data, portMAX_DELAY, &flag, NULL, 0);

        if (err == ESP_OK) {

            if (data.size == sizeof(sensor_payload_t)) {

                sensor_payload_t p;
                memcpy(&p, data.data, sizeof(p));  //copy to new place for processing

                //display on serial monitoro debug
                char src_mac_str[18];
                mac_to_str(p.src_mac, src_mac_str, sizeof(src_mac_str));

                ESP_LOGI(TAG, "RX sensor payload from %s: tilt=%d moist=%u",
                        src_mac_str, (int)p.tilt_measurement, (unsigned)p.moisture_measurement);

                //display message on oled
                if (g_oled) {
                    oled_show_sensor(&p);
                }

            } else {
                //if i get something other than payload struct
                ESP_LOGW(TAG, "RX unknown payload size=%d (expected %d)",
                        (int)data.size, (int)sizeof(sensor_payload_t));
            }

        } else {
            ESP_LOGW(TAG, "esp_mesh_recv failed: %s", esp_err_to_name(err));
        }
    }

}

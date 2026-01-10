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

#include "lwip/sockets.h"
#include "lwip/err.h"
#include "lwip/sys.h"

#define RECEIVER_IP "192.168.4.2" 
#define RECEIVER_PORT 5005

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

static void forward_to_udp(const sensor_payload_t *payload) {
    struct sockaddr_in dest_addr;
    dest_addr.sin_addr.s_addr = inet_addr(RECEIVER_IP);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(RECEIVER_PORT);

    // Create socket
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        return;
    }

    // Send the struct directly
    int err = sendto(sock, payload, sizeof(sensor_payload_t), 0, 
                     (struct sockaddr *)&dest_addr, sizeof(dest_addr));

    if (err < 0) {
        ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
    } else {
        ESP_LOGI(TAG, "UDP packet sent to %s", RECEIVER_IP);
    }

    close(sock);
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

static bool s_has_ip = false;

static void mesh_event_handler(void *arg, esp_event_base_t base, int32_t id, void *event_data)
{
    if (base == MESH_EVENT) {
        switch (id) {
            case MESH_EVENT_STARTED:
                ESP_LOGI(TAG, "MESH started");
                break;
            case MESH_EVENT_PARENT_CONNECTED:
                ESP_LOGI(TAG, "Connected to Router (Parent)");
                break;
            case MESH_EVENT_CHILD_CONNECTED:
                ESP_LOGI(TAG, "Child node connected to this Root");
                break;
            // Removed MESH_EVENT_ROOT_GOT_IP as it's not in IDF v5.x
            default:
                break;
        }
    } 
    // This is the block that actually matters for your UDP socket
    else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
        ESP_LOGI(TAG, "TCP/IP Layer: GOT IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_has_ip = true;
    }
    else if (base == IP_EVENT && id == IP_EVENT_STA_LOST_IP) {
        ESP_LOGW(TAG, "TCP/IP Layer: LOST IP");
        s_has_ip = false;
    }
}

static void wifi_mesh_init(void)
{
    // 1. Core Network Init
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // 2. Create the Station Interface (Necessary for Root to get IP)
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);

    // 3. Wi-Fi Driver Init
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Register handlers for both the Mesh and the IP stacks
    ESP_ERROR_CHECK(esp_event_handler_register(MESH_EVENT, ESP_EVENT_ANY_ID, &mesh_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &mesh_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_LOST_IP, &mesh_event_handler, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_FLASH));
    ESP_ERROR_CHECK(esp_wifi_start());

    // 4. Mesh Stack Init
    ESP_ERROR_CHECK(esp_mesh_init());
    
    mesh_cfg_t mesh_config = MESH_INIT_CONFIG_DEFAULT();
    
    // Mesh Identity
    uint8_t mesh_id[6] = MESH_ID;
    memcpy(mesh_config.mesh_id.addr, mesh_id, 6);
    
    // Credentials for the "Virtual Router" Access Point
    const char *router_ssid = "VirtualMeshRouter";
    const char *router_pass = "meshpass";
    
    mesh_config.channel = 6;
    mesh_config.router.ssid_len = strlen(router_ssid);
    memcpy(mesh_config.router.ssid, router_ssid, mesh_config.router.ssid_len);
    memcpy(mesh_config.router.password, router_pass, strlen(router_pass));

    // Internal Mesh AP (for children)
    mesh_config.mesh_ap.max_connection = 6;
    strncpy((char *)mesh_config.mesh_ap.password, "meshpass", sizeof(mesh_config.mesh_ap.password));

    ESP_ERROR_CHECK(esp_mesh_set_self_organized(true, true));
    ESP_ERROR_CHECK(esp_mesh_set_config(&mesh_config));
    
    ESP_ERROR_CHECK(esp_mesh_start());
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

                forward_to_udp(&p);

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
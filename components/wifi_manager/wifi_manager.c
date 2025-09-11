#include "wifi_manager.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "string.h"

static const char *TAG = "wifi_manager";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;
static bool s_wifi_connected = false;
static TaskHandle_t s_wifi_keepalive_task = NULL;

#define MAX_RETRY 5
#define KEEPALIVE_INTERVAL_MS 2000   // Send keepalive every 2 seconds

// WiFi keep-alive task to maintain connection
static void wifi_keepalive_task(void *pvParameters)
{
    ESP_LOGI(TAG, "🔄 WiFi keep-alive task started");
    
    while (1) {
        if (s_wifi_connected) {
            // Send a simple ping to the gateway to keep connection alive
            esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
            if (netif) {
                esp_netif_ip_info_t ip_info;
                if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
                    // Create a simple UDP packet to keep connection active
                    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
                    if (sock >= 0) {
                        struct sockaddr_in addr;
                        memset(&addr, 0, sizeof(addr));
                        addr.sin_family = AF_INET;
                        addr.sin_port = htons(53);  // DNS port
                        addr.sin_addr.s_addr = ip_info.gw.addr;  // Gateway IP
                        
                        char keepalive_data[] = "keepalive";
                        sendto(sock, keepalive_data, strlen(keepalive_data), 0, 
                               (struct sockaddr*)&addr, sizeof(addr));
                        close(sock);
                        
                        ESP_LOGD(TAG, "📡 Keep-alive packet sent to gateway");
                    }
                }
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(KEEPALIVE_INTERVAL_MS));
    }
}

static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "📡 WiFi STA started, attempting connection...");
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t* disconnected = (wifi_event_sta_disconnected_t*) event_data;
        ESP_LOGW(TAG, "📡 WiFi disconnected (attempt %d/%d) - Reason: %d", 
                s_retry_num + 1, MAX_RETRY, disconnected->reason);
        
        // Log common disconnect reasons
        switch (disconnected->reason) {
            case WIFI_REASON_AUTH_EXPIRE:
                ESP_LOGW(TAG, "   → Authentication expired");
                break;
            case WIFI_REASON_AUTH_LEAVE:
                ESP_LOGW(TAG, "   → Authentication leave");
                break;
            case WIFI_REASON_ASSOC_EXPIRE:
                ESP_LOGW(TAG, "   → Association expired");
                break;
            case WIFI_REASON_ASSOC_TOOMANY:
                ESP_LOGW(TAG, "   → Too many associations");
                break;
            case WIFI_REASON_NOT_AUTHED:
                ESP_LOGW(TAG, "   → Not authenticated");
                break;
            case WIFI_REASON_NOT_ASSOCED:
                ESP_LOGW(TAG, "   → Not associated");
                break;
            case WIFI_REASON_ASSOC_LEAVE:
                ESP_LOGW(TAG, "   → Association leave");
                break;
            case WIFI_REASON_ASSOC_NOT_AUTHED:
                ESP_LOGW(TAG, "   → Association not authenticated");
                break;
            case WIFI_REASON_DISASSOC_PWRCAP_BAD:
                ESP_LOGW(TAG, "   → Disassoc due to power capability");
                break;
            case WIFI_REASON_DISASSOC_SUPCHAN_BAD:
                ESP_LOGW(TAG, "   → Disassoc due to supported channel");
                break;
            case WIFI_REASON_IE_INVALID:
                ESP_LOGW(TAG, "   → Invalid IE");
                break;
            case WIFI_REASON_MIC_FAILURE:
                ESP_LOGW(TAG, "   → MIC failure");
                break;
            case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
                ESP_LOGW(TAG, "   → 4-way handshake timeout");
                break;
            case WIFI_REASON_GROUP_KEY_UPDATE_TIMEOUT:
                ESP_LOGW(TAG, "   → Group key update timeout");
                break;
            case WIFI_REASON_IE_IN_4WAY_DIFFERS:
                ESP_LOGW(TAG, "   → IE in 4-way differs");
                break;
            case WIFI_REASON_GROUP_CIPHER_INVALID:
                ESP_LOGW(TAG, "   → Group cipher invalid");
                break;
            case WIFI_REASON_PAIRWISE_CIPHER_INVALID:
                ESP_LOGW(TAG, "   → Pairwise cipher invalid");
                break;
            case WIFI_REASON_AKMP_INVALID:
                ESP_LOGW(TAG, "   → AKMP invalid");
                break;
            case WIFI_REASON_UNSUPP_RSN_IE_VERSION:
                ESP_LOGW(TAG, "   → Unsupported RSN IE version");
                break;
            case WIFI_REASON_INVALID_RSN_IE_CAP:
                ESP_LOGW(TAG, "   → Invalid RSN IE cap");
                break;
            case WIFI_REASON_802_1X_AUTH_FAILED:
                ESP_LOGW(TAG, "   → 802.1X auth failed");
                break;
            case WIFI_REASON_CIPHER_SUITE_REJECTED:
                ESP_LOGW(TAG, "   → Cipher suite rejected");
                break;
            case WIFI_REASON_BEACON_TIMEOUT:
                ESP_LOGW(TAG, "   → Beacon timeout");
                break;
            case WIFI_REASON_NO_AP_FOUND:
                ESP_LOGW(TAG, "   → No AP found");
                break;
            case WIFI_REASON_AUTH_FAIL:
                ESP_LOGW(TAG, "   → Authentication failed");
                break;
            case WIFI_REASON_ASSOC_FAIL:
                ESP_LOGW(TAG, "   → Association failed");
                break;
            case WIFI_REASON_HANDSHAKE_TIMEOUT:
                ESP_LOGW(TAG, "   → Handshake timeout");
                break;
            default:
                ESP_LOGW(TAG, "   → Unknown reason: %d", disconnected->reason);
                break;
        }
        
        if (s_retry_num < MAX_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "🔄 Retrying connection to AP...");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            ESP_LOGE(TAG, "❌ Failed to connect to AP after %d attempts", MAX_RETRY);
        }
        s_wifi_connected = false;
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "🎉 Got IP address: " IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "🌐 Gateway: " IPSTR, IP2STR(&event->ip_info.gw));
        ESP_LOGI(TAG, "🔧 Netmask: " IPSTR, IP2STR(&event->ip_info.netmask));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        s_wifi_connected = true;
        
        // Start keep-alive task to maintain connection
        if (s_wifi_keepalive_task == NULL) {
            BaseType_t ret = xTaskCreate(wifi_keepalive_task, "wifi_keepalive", 4096, NULL, 5, &s_wifi_keepalive_task);
            if (ret != pdPASS) {
                ESP_LOGW(TAG, "Failed to create WiFi keep-alive task");
            } else {
                ESP_LOGI(TAG, "✅ WiFi keep-alive task created");
            }
        }
    }
}

esp_err_t wifi_manager_init(void)
{
    ESP_LOGI(TAG, "🔧 Starting WiFi manager initialization...");
    
    s_wifi_event_group = xEventGroupCreate();
    if (!s_wifi_event_group) {
        ESP_LOGE(TAG, "Failed to create event group");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "✅ Event group created");

    esp_err_t ret = esp_netif_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize netif: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "✅ Netif initialized");

    ret = esp_event_loop_create_default();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create event loop: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "✅ Event loop created");
    
    esp_netif_create_default_wifi_sta();
    ESP_LOGI(TAG, "✅ Default WiFi STA created");

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    return ESP_OK;
}

esp_err_t wifi_manager_connect(const char* ssid, const char* password)
{
    ESP_LOGI(TAG, "🔗 Starting WiFi connection process...");
    ESP_LOGI(TAG, "📡 SSID: %s", ssid);
    ESP_LOGI(TAG, "🔑 Password length: %d", strlen(password));
    
    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable = false,  // Disable PMF for router compatibility
                .required = false
            },
            .listen_interval = 1,  // Shorter listen interval for better stability
            .scan_method = WIFI_FAST_SCAN,  // Faster scanning
            .sort_method = WIFI_CONNECT_AP_BY_SIGNAL,  // Connect to strongest signal
            .threshold.rssi = -80,  // Lower RSSI threshold
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    
    strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    
    ESP_LOGI(TAG, "⚙️ Setting WiFi configuration...");
    esp_err_t ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to set WiFi config: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "✅ WiFi config set successfully");
    
    ESP_LOGI(TAG, "🚀 Starting WiFi...");
    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to start WiFi: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "✅ WiFi started successfully");

    // Configure WiFi power management for stability
    ESP_LOGI(TAG, "⚙️ Configuring WiFi power management...");
    wifi_ps_type_t ps_type = WIFI_PS_NONE;  // Disable power saving for stability
    esp_err_t pm_ret = esp_wifi_set_ps(ps_type);
    if (pm_ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set WiFi power management: %s", esp_err_to_name(pm_ret));
    } else {
        ESP_LOGI(TAG, "✅ WiFi power management configured (PS_NONE for stability)");
    }
    
    // Configure WiFi for gaming router compatibility
    ESP_LOGI(TAG, "⚙️ Configuring WiFi for gaming router compatibility...");
    wifi_bandwidth_t bw = WIFI_BW_HT20;  // Use 20MHz bandwidth for stability
    esp_err_t bw_ret = esp_wifi_set_bandwidth(WIFI_IF_STA, bw);
    if (bw_ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set WiFi bandwidth: %s", esp_err_to_name(bw_ret));
    } else {
        ESP_LOGI(TAG, "✅ WiFi bandwidth set to 20MHz for stability");
    }

    ESP_LOGI(TAG, "wifi_init_sta finished.");
    
    // Scan for available networks to help debug
    ESP_LOGI(TAG, "🔍 Scanning for available networks...");
    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = true,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 100,
        .scan_time.active.max = 300,
    };
    
    esp_err_t scan_ret = esp_wifi_scan_start(&scan_config, true);
    if (scan_ret == ESP_OK) {
        uint16_t ap_count = 0;
        esp_wifi_scan_get_ap_num(&ap_count);
        ESP_LOGI(TAG, "📡 Found %d access points", ap_count);
        
        if (ap_count > 0) {
            wifi_ap_record_t ap_info[ap_count];
            esp_wifi_scan_get_ap_records(&ap_count, ap_info);
            
            for (int i = 0; i < ap_count; i++) {
                ESP_LOGI(TAG, "   %d: SSID='%s' RSSI=%d Auth=%d", 
                        i+1, ap_info[i].ssid, ap_info[i].rssi, ap_info[i].authmode);
            }
        }
    } else {
        ESP_LOGW(TAG, "⚠️ WiFi scan failed: %s", esp_err_to_name(scan_ret));
    }

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s", ssid, password);
        return ESP_OK;
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s", ssid, password);
        return ESP_FAIL;
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
        return ESP_FAIL;
    }
}

esp_err_t wifi_manager_disconnect(void)
{
    s_wifi_connected = false;
    return esp_wifi_disconnect();
}

bool wifi_manager_is_connected(void)
{
    return s_wifi_connected;
}

esp_err_t wifi_manager_get_ip(char* ip_str, size_t max_len)
{
    if (!s_wifi_connected || !ip_str || max_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!netif) {
        return ESP_FAIL;
    }

    esp_netif_ip_info_t ip_info;
    esp_err_t ret = esp_netif_get_ip_info(netif, &ip_info);
    if (ret != ESP_OK) {
        return ret;
    }

    snprintf(ip_str, max_len, IPSTR, IP2STR(&ip_info.ip));
    return ESP_OK;
}

esp_err_t wifi_manager_check_connection(void)
{
    if (!s_wifi_connected) {
        ESP_LOGW(TAG, "⚠️ WiFi not connected");
        return ESP_FAIL;
    }
    
    // Try to ping the gateway to verify connection
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif) {
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
            ESP_LOGI(TAG, "📡 WiFi connection verified - IP: " IPSTR, IP2STR(&ip_info.ip));
            return ESP_OK;
        }
    }
    
    ESP_LOGW(TAG, "⚠️ WiFi connection verification failed");
    return ESP_FAIL;
}

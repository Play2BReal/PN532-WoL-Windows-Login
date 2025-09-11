#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <ctype.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "sdkconfig.h"
#include "pn532_driver_i2c.h"
#include "pn532.h"
#include "driver/rmt_tx.h"
#include "led_strip.h"
#include "driver/gpio.h"

// New includes for Windows login functionality
#include "wifi_manager.h"
#include "wol_client.h"
#include "hid_keyboard.h"
#include "nvs_flash.h"


// I2C mode configuration for PN532 (from sdkconfig)
#define SCL_PIN    CONFIG_PN532_SCL_PIN
#define SDA_PIN    CONFIG_PN532_SDA_PIN
#define RESET_PIN  CONFIG_PN532_RESET_PIN
#define IRQ_PIN    CONFIG_PN532_IRQ_PIN

static const char *TAG = "windows_login_nfc";

// LED Type Selection - Choose between normal LED and Neopixel (from sdkconfig)
#define USE_NEOPIXEL CONFIG_USE_NEOPIXEL

// LED Configuration (from sdkconfig)
#define LED_PIN CONFIG_LED_PIN
#define LED_NUM CONFIG_LED_NUM
#define LED_RMT_RES_HZ CONFIG_LED_RMT_RES_HZ

// Authentication URL
#define AUTH_URL "put your URL here or other ntag data text here for the reader!"

// Windows Login Configuration
#define WIFI_SSID "WiFiSSID"           // Change this to your WiFi SSID
#define WIFI_PASSWORD "WiFiPassword"   // Change this to your WiFi password
#define PC_MAC_ADDRESS "xx:xx:xx:xx:xx:xx"   // Change this to your PC's MAC address
#define PC_IP_ADDRESS "x.x.x.x"        // Change this to your PC's IP address
#define WINDOWS_PASSWORD "Pass"     // Change this to your Windows password

// Set to 1 to force sending WoL packets on each tap regardless of PC state (for testing with Wireshark)
#define WOL_ALWAYS_SEND_FOR_TEST 0

// LED state
#if USE_NEOPIXEL
static led_strip_handle_t led_strip = NULL;
#else
static bool normal_led_initialized = false;
#endif

// Function declarations
void hsv_to_rgb(int h, int s, int v, int *r, int *g, int *b);
void led_boot_indication(void);
void led_pc_connect(void);
void led_auth_success(void);
void led_auth_fail(void);
void led_read_fail(void);
esp_err_t perform_windows_login(void);

// Helper: case-insensitive substring check
static bool strcasestr_simple(const char *haystack, const char *needle) {
    if (!haystack || !needle) return false;
    size_t nlen = strlen(needle);
    if (nlen == 0) return true;
    for (const char *p = haystack; *p; ++p) {
        size_t i = 0;
        while (i < nlen && p[i] && tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i])) {
            i++;
        }
        if (i == nlen) return true;
    }
    return false;
}

// Function to extract URL from NDEF data (simplified approach)
bool extract_url_from_ndef(uint8_t* data, int data_len, char* url, int url_max_len) {
    if (!data || data_len <= 0 || !url || url_max_len <= 1) return false;

    // Look for the pattern: 03 13 d1 01 0f 55 04/05 [URL text] fe
    for (int i = 0; i < data_len - 8; i++) {
        // Check for NDEF TLV header: 03 13 d1 01 0f 55
        if (data[i] == 0x03 && data[i+1] == 0x13 && data[i+2] == 0xd1 && 
            data[i+3] == 0x01 && data[i+4] == 0x0f && data[i+5] == 0x55) {
            
            // uint8_t uri_code = data[i+6]; // Not used in this implementation
            int url_start = i + 7;
            int url_len = 0;
            
            // Extract URL text until we hit 0xfe or end of data
            for (int j = url_start; j < data_len && url_len < url_max_len - 1; j++) {
                if (data[j] == 0xfe) break; // NDEF terminator
                url[url_len++] = data[j];
            }
            url[url_len] = '\0';
            
            // The URL is already complete in the NDEF data, no need to add protocol prefix
            
            return true;
        }
    }
    return false;
}

// Function to check if URL matches authentication URL (case-insensitive)
bool authenticate_url(const char* extracted_url) {
    return strcasestr_simple(extracted_url, AUTH_URL);
}

// LED status indication function (legacy - use specific functions instead)
void led_status_indication(const char* color, int duration_ms) {
    // This function is kept for compatibility but should use specific LED functions
    ESP_LOGW(TAG, "Using legacy led_status_indication - consider using specific LED functions");
}

// LED effect function - creates a "rainbow" pattern with NeoPixel colors
void rainbow_effect(int duration_ms) {
#if USE_NEOPIXEL
    if (!led_strip) {
        ESP_LOGE(TAG, "NeoPixel not initialized! Cannot start effect.");
        return;
    }
    
    int start_time = esp_timer_get_time() / 1000; // Convert to milliseconds
    int cycle_count = 0;
    
    while ((esp_timer_get_time() / 1000) - start_time < duration_ms) {
        // Create rainbow colors by cycling through hue values
        int hue = (cycle_count * 10) % 360; // Cycle through 0-360 degrees
        
        // Convert HSV to RGB
        int r, g, b;
        hsv_to_rgb(hue, 100, 100, &r, &g, &b);
        
        // Set NeoPixel color
        led_strip_set_pixel(led_strip, 0, r, g, b);
        led_strip_refresh(led_strip);
        
        vTaskDelay(100 / portTICK_PERIOD_MS); // 100ms delay between color changes
        cycle_count++;
    }
    
    // Turn off LED
    led_strip_clear(led_strip);
#else
    ESP_LOGW(TAG, "Rainbow effect only available with NeoPixel LEDs.");
#endif
}

// Boot indication: Single blue blink
void led_boot_indication(void) {
#if USE_NEOPIXEL
    if (!led_strip) {
        ESP_LOGW(TAG, "NeoPixel not initialized! Cannot show boot indication.");
        return;
    }
    
    // Single blue blink
    led_strip_set_pixel(led_strip, 0, 0, 0, 255);  // Blue
    led_strip_refresh(led_strip);
    vTaskDelay(500 / portTICK_PERIOD_MS);
    
    led_strip_clear(led_strip);
    led_strip_refresh(led_strip);
#else
    if (!normal_led_initialized) {
        ESP_LOGW(TAG, "Normal LED not initialized! Cannot show boot indication.");
        return;
    }
    
    // 5 blinks for boot
    for (int i = 0; i < 5; i++) {
        gpio_set_level(LED_PIN, 0);  // Turn on (active-low)
        vTaskDelay(200 / portTICK_PERIOD_MS);
        gpio_set_level(LED_PIN, 1);  // Turn off (active-low)
        vTaskDelay(200 / portTICK_PERIOD_MS);
    }
#endif
}

// PC connection: Rainbow cycle
void led_pc_connect(void) {
#if USE_NEOPIXEL
    if (!led_strip) {
        ESP_LOGW(TAG, "NeoPixel not initialized! Cannot show PC connect.");
        return;
    }
    
    // Rainbow cycle for PC connection celebration
    int rainbow_colors[][3] = {
        {255, 0, 0},     // Red
        {255, 127, 0},   // Orange
        {255, 255, 0},   // Yellow
        {127, 255, 0},   // Yellow-Green
        {0, 255, 0},     // Green
        {0, 255, 127},   // Green-Cyan
        {0, 255, 255},   // Cyan
        {0, 127, 255},   // Light Blue
        {0, 0, 255},     // Blue
        {127, 0, 255},   // Blue-Purple
        {255, 0, 255},   // Magenta
        {255, 0, 127}    // Red-Pink
    };
    
    for (int i = 0; i < 12; i++) {
        led_strip_set_pixel(led_strip, 0, rainbow_colors[i][0], rainbow_colors[i][1], rainbow_colors[i][2]);
        led_strip_refresh(led_strip);
        vTaskDelay(50 / portTICK_PERIOD_MS);  // Faster cycle for quick rainbow
    }
    
    led_strip_clear(led_strip);
    led_strip_refresh(led_strip);
#else
    if (!normal_led_initialized) {
        ESP_LOGW(TAG, "Normal LED not initialized! Cannot show PC connect.");
        return;
    }
    
    // 2 blinks for PC connect
    for (int i = 0; i < 2; i++) {
        gpio_set_level(LED_PIN, 0);  // Turn on (active-low)
        vTaskDelay(300 / portTICK_PERIOD_MS);
        gpio_set_level(LED_PIN, 1);  // Turn off (active-low)
        vTaskDelay(200 / portTICK_PERIOD_MS);
    }
#endif
}

// Authentication success: Green blink 3 times
void led_auth_success(void) {
#if USE_NEOPIXEL
    if (!led_strip) {
        ESP_LOGW(TAG, "NeoPixel not initialized! Cannot show auth success.");
        return;
    }
    
    // Green blink 3 times
    for (int i = 0; i < 3; i++) {
        led_strip_set_pixel(led_strip, 0, 0, 255, 0);  // Green
        led_strip_refresh(led_strip);
        vTaskDelay(200 / portTICK_PERIOD_MS);
        
        led_strip_clear(led_strip);
        led_strip_refresh(led_strip);
        vTaskDelay(200 / portTICK_PERIOD_MS);
    }
    
    led_strip_clear(led_strip);
    led_strip_refresh(led_strip);
#else
    if (!normal_led_initialized) {
        ESP_LOGW(TAG, "Normal LED not initialized! Cannot show auth success.");
        return;
    }
    
    // 3 blinks for auth success
    for (int i = 0; i < 3; i++) {
        gpio_set_level(LED_PIN, 0);  // Turn on (active-low)
        vTaskDelay(300 / portTICK_PERIOD_MS);
        gpio_set_level(LED_PIN, 1);  // Turn off (active-low)
        vTaskDelay(200 / portTICK_PERIOD_MS);
    }
#endif
}

// Authentication failure: Red blink 3 times (wrong card/NDEF)
void led_auth_fail(void) {
#if USE_NEOPIXEL
    if (!led_strip) {
        ESP_LOGW(TAG, "NeoPixel not initialized! Cannot show auth fail.");
        return;
    }
    
    // Red blink 3 times for wrong card/NDEF
    for (int i = 0; i < 3; i++) {
        led_strip_set_pixel(led_strip, 0, 255, 0, 0);  // Red
        led_strip_refresh(led_strip);
        vTaskDelay(300 / portTICK_PERIOD_MS);
        
        led_strip_clear(led_strip);
        led_strip_refresh(led_strip);
        vTaskDelay(200 / portTICK_PERIOD_MS);
    }
#else
    if (!normal_led_initialized) {
        ESP_LOGW(TAG, "Normal LED not initialized! Cannot show auth fail.");
        return;
    }
    
    // 1 blink for any failure (wrong card/NDEF/read fail - can't distinguish with single LED)
    gpio_set_level(LED_PIN, 0);  // Turn on (active-low)
    vTaskDelay(300 / portTICK_PERIOD_MS);
    gpio_set_level(LED_PIN, 1);  // Turn off (active-low)
#endif
}

// Read failure: Single red blink (misread/cut-off)
void led_read_fail(void) {
#if USE_NEOPIXEL
    if (!led_strip) {
        ESP_LOGW(TAG, "NeoPixel not initialized! Cannot show read fail.");
        return;
    }
    
    // Single red blink for read fail (misread/cut-off)
    led_strip_set_pixel(led_strip, 0, 255, 0, 0);  // Red
    led_strip_refresh(led_strip);
    vTaskDelay(300 / portTICK_PERIOD_MS);
    
    led_strip_clear(led_strip);
    led_strip_refresh(led_strip);
#else
    if (!normal_led_initialized) {
        ESP_LOGW(TAG, "Normal LED not initialized! Cannot show read fail.");
        return;
    }
    
    // 1 blink for any failure (wrong card/NDEF/read fail - can't distinguish with single LED)
    gpio_set_level(LED_PIN, 0);  // Turn on (active-low)
    vTaskDelay(300 / portTICK_PERIOD_MS);
    gpio_set_level(LED_PIN, 1);  // Turn off (active-low)
#endif
}

// Simple HSV to RGB conversion
void hsv_to_rgb(int h, int s, int v, int *r, int *g, int *b) {
    int c = (v * s) / 100;
    int x = c * (60 - abs((h % 360) - 180)) / 60;
    int m = v - c;
    
    int r1, g1, b1;
    if (h < 60) {
        r1 = c; g1 = x; b1 = 0;
    } else if (h < 120) {
        r1 = x; g1 = c; b1 = 0;
    } else if (h < 180) {
        r1 = 0; g1 = c; b1 = x;
    } else if (h < 240) {
        r1 = 0; g1 = x; b1 = c;
    } else if (h < 300) {
        r1 = x; g1 = 0; b1 = c;
    } else {
        r1 = c; g1 = 0; b1 = x;
    }
    
    *r = (r1 + m) * 255 / 100;
    *g = (g1 + m) * 255 / 100;
    *b = (b1 + m) * 255 / 100;
}

// Initialize LED (NeoPixel or normal LED)
void init_led() {
#if USE_NEOPIXEL
    // LED strip configuration
    led_strip_config_t strip_config = {
        .strip_gpio_num = LED_PIN,
        .max_leds = LED_NUM,
        .led_model = LED_MODEL_WS2812,
        .flags.invert_out = false,
    };
    
    led_strip_rmt_config_t rmt_config_strip = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = LED_RMT_RES_HZ,
        .flags.with_dma = false,
    };
    
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config_strip, &led_strip));
    if (!led_strip) {
        ESP_LOGE(TAG, "Failed to initialize NeoPixel LED strip");
        return;
    }
    
    ESP_LOGI(TAG, "NeoPixel LED strip initialized on GPIO %d", LED_PIN);
#else
    // Normal LED configuration
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << LED_PIN),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
    };
    
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure GPIO %d for normal LED", LED_PIN);
        return;
    }
    
    normal_led_initialized = true;
    ESP_LOGI(TAG, "Normal LED initialized on GPIO %d", LED_PIN);
#endif
}

// Windows Login Function
esp_err_t perform_windows_login(void)
{
    ESP_LOGI(TAG, "🔐 Starting Windows login process...");
    
    // Check WiFi connection first
    ESP_LOGI(TAG, "🔍 Checking WiFi connection...");
    esp_err_t wifi_check = wifi_manager_check_connection();
    if (wifi_check != ESP_OK) {
        ESP_LOGE(TAG, "❌ WiFi connection lost! Cannot proceed with login");
        return ESP_FAIL;
    }
    
    // Optional: send WoL regardless of state for testing
#if WOL_ALWAYS_SEND_FOR_TEST
    ESP_LOGW(TAG, "WOL test mode enabled: sending WoL packet");
    wol_send_magic_packet_all(PC_MAC_ADDRESS, PC_IP_ADDRESS);
#endif

    // Check if PC is already on
    ESP_LOGI(TAG, "🔍 Checking if PC is already on...");
    ESP_LOGI(TAG, "📍 PC IP Address: %s", PC_IP_ADDRESS);
    ESP_LOGI(TAG, "📍 PC MAC Address: %s", PC_MAC_ADDRESS);
    // Keep detection quick to avoid long waits on tap
    bool pc_is_on = wol_check_host_reachable(PC_IP_ADDRESS, 600);
    
    if (pc_is_on) {
        ESP_LOGI(TAG, "✅ PC is already on! Proceeding with login...");
        led_pc_connect();  // Purple blink for PC connection
        
        // PC is already on - quick login with no delay
        ESP_LOGI(TAG, "⚡ Quick login - PC is already running");

        // Wake focus before typing
        hid_keyboard_press_enter();
        
        // Brief delay before typing password
        ESP_LOGI(TAG, "⏳ Brief delay before typing password...");
        vTaskDelay(500 / portTICK_PERIOD_MS);

        // Type the Windows password
        ESP_LOGI(TAG, "Typing Windows password...");
        esp_err_t ret = hid_keyboard_type_string(WINDOWS_PASSWORD, 50);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to type password");
            return ret;
        }
        
        // Press Enter to submit
        ESP_LOGI(TAG, "Pressing Enter to submit password...");
        hid_keyboard_press_enter();
        
        ESP_LOGI(TAG, "🎉 Windows login completed!");
        
    } else {
        ESP_LOGI(TAG, "💤 PC is off. Sending Wake-on-LAN packet...");
        
        // Alternate WoL and probe 3 ports for 30 seconds total
        esp_err_t ret = ESP_FAIL;
        int start_time = esp_timer_get_time() / 1000; // Convert to milliseconds
        int attempt = 0;
        
        while ((esp_timer_get_time() / 1000) - start_time < 30000 && !pc_is_on) {
            attempt++;
            ESP_LOGI(TAG, "🔔 WoL attempt %d", attempt);
            ret = wol_send_magic_packet_all(PC_MAC_ADDRESS, PC_IP_ADDRESS);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "WoL send failed: %s", esp_err_to_name(ret));
            }
            
            ESP_LOGI(TAG, "🔍 Probing 3 ports...");
            for (int probe = 1; probe <= 3; probe++) {
                ESP_LOGI(TAG, "Probe %d/3", probe);
                if (wol_check_host_reachable(PC_IP_ADDRESS, 1000)) {
                    pc_is_on = true;
                    break;
                }
            }
            
            if (pc_is_on) break;
            
            // Quick check if we're still within 30s before next WoL
            if ((esp_timer_get_time() / 1000) - start_time >= 30000) {
                break;
            }
        }
        
        // Check if PC is now on
        ESP_LOGI(TAG, "Checking if PC is now on...");
        if (!pc_is_on) {
            pc_is_on = wol_check_host_reachable(PC_IP_ADDRESS, 3000);
        }
        
        if (pc_is_on) {
            ESP_LOGI(TAG, "✅ PC is now on! Proceeding with login...");
            led_pc_connect();  // Purple blink for PC connection
            
            // Wait for Windows to boot and lock screen to be ready
            ESP_LOGI(TAG, "⏳ Waiting for Windows to boot and lock screen to be ready...");
            vTaskDelay(7000 / portTICK_PERIOD_MS);

            // Wake focus before typing
            hid_keyboard_press_enter();
            
            // Brief delay before typing password
            ESP_LOGI(TAG, "⏳ Brief delay before typing password...");
            vTaskDelay(500 / portTICK_PERIOD_MS);

            // Type the Windows password
            ESP_LOGI(TAG, "Typing Windows password...");
            ret = hid_keyboard_type_string(WINDOWS_PASSWORD, 50);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to type password");
                return ret;
            }
            
            // Press Enter to submit
            ESP_LOGI(TAG, "Pressing Enter to submit password...");
            hid_keyboard_press_enter();
            
            ESP_LOGI(TAG, "🎉 Windows login completed!");
            
        } else {
            ESP_LOGW(TAG, "❌ PC did not respond after Wake-on-LAN. It may not support WoL or be configured properly.");
            return ESP_FAIL;
        }
    }
    
    return ESP_OK;
}

void app_main()
{
    pn532_io_t pn532_io;
    esp_err_t err;

    printf("Windows Login NFC Reader Starting...\n");
    ESP_LOGI(TAG, "🚀 Starting Windows Login NFC Reader");
    ESP_LOGI(TAG, "📋 Configuration:");
    ESP_LOGI(TAG, "   WiFi SSID: %s", WIFI_SSID);
    ESP_LOGI(TAG, "   PC IP: %s", PC_IP_ADDRESS);
    ESP_LOGI(TAG, "   PC MAC: %s", PC_MAC_ADDRESS);
    ESP_LOGI(TAG, "   Password: %s", WINDOWS_PASSWORD);

    // Initialize LED
    ESP_LOGI(TAG, "💡 Initializing LED...");
    init_led();
    ESP_LOGI(TAG, "✅ LED initialized");
    
    // Initialize NVS (required for WiFi)
    ESP_LOGI(TAG, "🔧 Initializing NVS...");
    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_LOGI(TAG, "✅ NVS initialized");

    // Initialize WiFi
    ESP_LOGI(TAG, "🔧 Initializing WiFi...");
    err = wifi_manager_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to initialize WiFi manager");
        ESP_LOGE(TAG, "Error code: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "✅ WiFi manager initialized successfully");
    
    // Connect to WiFi
    ESP_LOGI(TAG, "🔗 Connecting to WiFi: %s", WIFI_SSID);
    ESP_LOGI(TAG, "🔑 Using password: %s", WIFI_PASSWORD);
    err = wifi_manager_connect(WIFI_SSID, WIFI_PASSWORD);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to connect to WiFi");
        ESP_LOGE(TAG, "Check your WiFi credentials and network availability");
        return;
    }
    
    ESP_LOGI(TAG, "✅ Connected to WiFi!");
    char ip_str[16];
    wifi_manager_get_ip(ip_str, sizeof(ip_str));
    ESP_LOGI(TAG, "IP address: %s", ip_str);
    
    // Initialize HID keyboard
    ESP_LOGI(TAG, "Initializing HID keyboard...");
    err = hid_keyboard_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize HID keyboard");
        return;
    }
    
    ESP_LOGI(TAG, "✅ HID keyboard initialized!");
    
    // Show boot indication
    led_boot_indication();
    
    ESP_LOGI(TAG, "🔄 Ready! Waiting for NFC card authentication...");
    ESP_LOGI(TAG, "💡 Tap your authorized NFC card to trigger Windows login");
    
    // Start WiFi health check task
    ESP_LOGI(TAG, "🔧 Starting WiFi health monitoring...");

#if 1
    // Enable DEBUG logging
    esp_log_level_set("PN532", ESP_LOG_DEBUG);
    esp_log_level_set("pn532_driver", ESP_LOG_DEBUG);
    esp_log_level_set("pn532_driver_i2c", ESP_LOG_DEBUG);
    esp_log_level_set("i2c.master", ESP_LOG_DEBUG);
    esp_log_level_set("ntag_read", ESP_LOG_DEBUG);
    esp_log_level_set("wol_client", ESP_LOG_DEBUG);  // Enable WoL client debug logging
#endif

    vTaskDelay(1000 / portTICK_PERIOD_MS);

    ESP_LOGI(TAG, "init PN532 in I2C mode");
    ESP_ERROR_CHECK(pn532_new_driver_i2c(SDA_PIN, SCL_PIN, RESET_PIN, IRQ_PIN, 0, &pn532_io));

    do {
        err = pn532_init(&pn532_io);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "failed to initialize PN532");
            pn532_release(&pn532_io);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
    } while(err != ESP_OK);

    ESP_LOGI(TAG, "get firmware version");
    uint32_t version_data = 0;
    do {
        err = pn532_get_firmware_version(&pn532_io, &version_data);
        if (ESP_OK != err) {
            ESP_LOGI(TAG, "Didn't find PN53x board");
            pn532_reset(&pn532_io);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
    } while (ESP_OK != err);

    // Log firmware infos
    ESP_LOGI(TAG, "Found chip PN5%x", (unsigned int)(version_data >> 24) & 0xFF);
    ESP_LOGI(TAG, "Firmware ver. %d.%d", (int)(version_data >> 16) & 0xFF, (int)(version_data >> 8) & 0xFF);

    ESP_LOGI(TAG, "Waiting for an ISO14443A Card ...");
    while (1)
    {
        uint8_t uid[] = {0, 0, 0, 0, 0, 0, 0}; // Buffer to store the returned UID
        uint8_t uid_length;                     // Length of the UID (4 or 7 bytes depending on ISO14443A card type)

        // Wait for an ISO14443A type cards (Mifare, etc.).  When one is found
        // 'uid' will be populated with the UID, and uid_length will indicate
        // if the uid is 4 bytes (Mifare Classic) or 7 bytes (Mifare Ultralight)
        err = pn532_read_passive_target_id(&pn532_io, PN532_BRTY_ISO14443A_106KBPS, uid, &uid_length, 0);

        if (ESP_OK == err)
        {
            // Display some basic information about the card
            ESP_LOGI(TAG, "Found an ISO14443A card");
            ESP_LOGI(TAG, "UID Length: %d bytes", uid_length);
            ESP_LOGI(TAG, "UID Value:");
            ESP_LOG_BUFFER_HEX_LEVEL(TAG, uid, uid_length, ESP_LOG_INFO);

            err = pn532_in_list_passive_target(&pn532_io);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "❌ Failed to inList passive target - misread or card too far");
                led_read_fail();
                continue;
            }

            NTAG2XX_MODEL ntag_model = NTAG2XX_UNKNOWN;
            err = ntag2xx_get_model(&pn532_io, &ntag_model);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "❌ Failed to get NTAG model - misread or card too far");
                led_read_fail();
                continue;
            }

            int page_max;
            switch (ntag_model) {
                case NTAG2XX_NTAG213:
                    page_max = 45;
                    ESP_LOGI(TAG, "found NTAG213 target (or maybe NTAG203)");
                    break;

                case NTAG2XX_NTAG215:
                    page_max = 135;
                    ESP_LOGI(TAG, "found NTAG215 target");
                    break;

                case NTAG2XX_NTAG216:
                    page_max = 231;
                    ESP_LOGI(TAG, "found NTAG216 target");
                    break;

                default:
                    ESP_LOGI(TAG, "Found unknown NTAG target!");
                    continue;
            }

            // Read pages to get NDEF data for authentication
            uint8_t ndef_data[256];
            int ndef_len = 0;
            bool auth_success = false;
            
            // Read first 16 pages (256 bytes) to capture NDEF data across boundaries
            for(int page=0; page < 16 && page < page_max; page+=4) {
                uint8_t buf[16];
                err = ntag2xx_read_page(&pn532_io, page, buf, 16);
                if (err == ESP_OK) {
                    ESP_LOG_BUFFER_HEXDUMP(TAG, buf, 16, ESP_LOG_INFO);
                    
                    // Collect NDEF data for authentication
                    if (ndef_len <= (int)sizeof(ndef_data) - 16) {
                        memcpy(ndef_data + ndef_len, buf, 16);
                        ndef_len += 16;
                    }
                }
                else {
                    ESP_LOGI(TAG, "Failed to read page %d", page);
                    break;
                }
            }
            
            // Check if we failed to read any data (misread)
            if (ndef_len == 0) {
                ESP_LOGW(TAG, "❌ Failed to read card data - misread or card too far");
                led_read_fail();
                continue; // Try again
            }
            
            // Try to authenticate the card
            if (ndef_len > 0) {
                char extracted_url[256];
                if (extract_url_from_ndef(ndef_data, ndef_len, extracted_url, sizeof(extracted_url))) {
                    ESP_LOGI(TAG, "Extracted URL: %s", extracted_url);
                    
                    if (authenticate_url(extracted_url)) {
                        ESP_LOGI(TAG, "✅ AUTHENTICATION SUCCESS! Card authorized.");
                        auth_success = true;
                        
                        // Show authentication success LED
                        led_auth_success();
                        
                        // Perform Windows login instead of just LED effects
                        ESP_LOGI(TAG, "🚀 Triggering Windows login process...");
                        esp_err_t login_result = perform_windows_login();
                        
                        if (login_result == ESP_OK) {
                            ESP_LOGI(TAG, "🎉 Windows login process completed successfully!");
                        } else {
                            ESP_LOGE(TAG, "❌ Windows login process failed!");
                        }
                    } else {
                        ESP_LOGI(TAG, "❌ Authentication failed. URL does not match: %s", AUTH_URL);
                        
                        // Show authentication failure LED
                        led_auth_fail();
                    }
                } else {
                    ESP_LOGI(TAG, "❌ No valid NDEF URL found on card");
                    
                    // Show authentication failure LED
                    led_auth_fail();
                }
            }
            
            // Continue reading remaining pages for display
            for(int page=16; page < page_max; page+=4) {
                uint8_t buf[16];
                err = ntag2xx_read_page(&pn532_io, page, buf, 16);
                if (err == ESP_OK) {
                    ESP_LOG_BUFFER_HEXDUMP(TAG, buf, 16, ESP_LOG_INFO);
                }
                else {
                    ESP_LOGI(TAG, "Failed to read page %d", page);
                    break;
                }
            }
            
            if (auth_success) {
                ESP_LOGI(TAG, "🎉 Authorized card processed successfully!");
            }
            
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        } else {
            // NFC read failed - show single red blink for misread/cut-off
            ESP_LOGD(TAG, "NFC read failed or no card detected");
            led_read_fail();
        }
    }
}
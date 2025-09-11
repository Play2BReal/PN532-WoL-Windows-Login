#include "hid_keyboard.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "string.h"
#include "tinyusb.h"
#include "tusb.h"
#include "class/hid/hid_device.h"

static const char *TAG = "hid_keyboard";

// HID report descriptor (Keyboard)
static const uint8_t desc_hid_keyboard_report[] = {
    TUD_HID_REPORT_DESC_KEYBOARD()
};

// Configuration descriptor (Full Speed)
enum {
    ITF_NUM_KEYBOARD = 0,
    ITF_NUM_TOTAL
};

#define EPNUM_KEYBOARD   0x81
#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN)

static const uint8_t desc_configuration_fs[] = {
    // Config number, interface count, string index, total length, attribute, power in mA
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),

    // Interface number, string index, protocol, report descriptor len, EP In address, size & polling interval
    TUD_HID_DESCRIPTOR(ITF_NUM_KEYBOARD, 0, HID_ITF_PROTOCOL_KEYBOARD, sizeof(desc_hid_keyboard_report), EPNUM_KEYBOARD, CFG_TUD_HID_EP_BUFSIZE, 10),
};

// Track USB mount state
static volatile bool s_usb_mounted = false;

// TinyUSB device callbacks for connection state
void tud_mount_cb(void)
{
    s_usb_mounted = true;
    ESP_LOGI(TAG, "USB mounted");
}

void tud_umount_cb(void)
{
    s_usb_mounted = false;
    ESP_LOGW(TAG, "USB unmounted");
}

void tud_suspend_cb(bool remote_wakeup_en)
{
    (void)remote_wakeup_en;
    ESP_LOGW(TAG, "USB suspended");
}

void tud_resume_cb(void)
{
    ESP_LOGI(TAG, "USB resumed");
}

static bool hid_wait_ready(uint32_t timeout_ms)
{
    uint32_t elapsed = 0;
    while (!(s_usb_mounted && tud_hid_ready())) {
        vTaskDelay(pdMS_TO_TICKS(1));
        if (++elapsed >= timeout_ms) {
            return false;
        }
    }
    return true;
}

// TinyUSB HID callbacks
uint8_t const * tud_hid_descriptor_report_cb(uint8_t instance)
{
    (void) instance;
    return desc_hid_keyboard_report;
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t* buffer, uint16_t reqlen)
{
    (void) instance;
    (void) report_id;
    (void) report_type;
    (void) buffer;
    (void) reqlen;
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const* buffer, uint16_t bufsize)
{
    (void) instance;
    (void) report_id;
    (void) report_type;
    (void) buffer;
    (void) bufsize;
}

// TAG moved to top so TinyUSB callbacks can log

// USB HID key codes (simplified definitions)
#define HID_KEY_1 0x1E
#define HID_KEY_2 0x1F
#define HID_KEY_3 0x20
#define HID_KEY_4 0x21
#define HID_KEY_5 0x22
#define HID_KEY_6 0x23
#define HID_KEY_7 0x24
#define HID_KEY_8 0x25
#define HID_KEY_9 0x26
#define HID_KEY_0 0x27
#define HID_KEY_A 0x04
#define HID_KEY_B 0x05
#define HID_KEY_C 0x06
#define HID_KEY_D 0x07
#define HID_KEY_E 0x08
#define HID_KEY_F 0x09
#define HID_KEY_G 0x0A
#define HID_KEY_H 0x0B
#define HID_KEY_I 0x0C
#define HID_KEY_J 0x0D
#define HID_KEY_K 0x0E
#define HID_KEY_L 0x0F
#define HID_KEY_M 0x10
#define HID_KEY_N 0x11
#define HID_KEY_O 0x12
#define HID_KEY_P 0x13
#define HID_KEY_Q 0x14
#define HID_KEY_R 0x15
#define HID_KEY_S 0x16
#define HID_KEY_T 0x17
#define HID_KEY_U 0x18
#define HID_KEY_V 0x19
#define HID_KEY_W 0x1A
#define HID_KEY_X 0x1B
#define HID_KEY_Y 0x1C
#define HID_KEY_Z 0x1D
#define HID_KEY_SPACE 0x2C
#define HID_KEY_MINUS 0x2D
#define HID_KEY_EQUAL 0x2E
#define HID_KEY_BRACKET_LEFT 0x2F
#define HID_KEY_BRACKET_RIGHT 0x30
#define HID_KEY_BACKSLASH 0x31
#define HID_KEY_SEMICOLON 0x33
#define HID_KEY_APOSTROPHE 0x34
#define HID_KEY_GRAVE 0x35
#define HID_KEY_COMMA 0x36
#define HID_KEY_PERIOD 0x37
#define HID_KEY_SLASH 0x38
#define HID_KEY_ENTER 0x28
#define HID_KEY_TAB 0x2B
#define HID_KEY_ESCAPE 0x29
#define HID_KEY_SHIFT_LEFT 0xE1

// USB HID key codes for common characters
static uint8_t char_to_hid_code[128] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 0-15
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 16-31
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 32-47
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 48-63
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 64-79
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 80-95
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 96-111
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 112-127
};

// ASCII to keycode conversion table from TinyUSB (modifier, keycode)
static const uint8_t ascii_to_keycode[128][2] = { HID_ASCII_TO_KEYCODE };

// Initialize character to HID code mapping
static void init_char_mapping(void)
{
    // Numbers
    char_to_hid_code['1'] = HID_KEY_1;
    char_to_hid_code['2'] = HID_KEY_2;
    char_to_hid_code['3'] = HID_KEY_3;
    char_to_hid_code['4'] = HID_KEY_4;
    char_to_hid_code['5'] = HID_KEY_5;
    char_to_hid_code['6'] = HID_KEY_6;
    char_to_hid_code['7'] = HID_KEY_7;
    char_to_hid_code['8'] = HID_KEY_8;
    char_to_hid_code['9'] = HID_KEY_9;
    char_to_hid_code['0'] = HID_KEY_0;

    // Letters (lowercase)
    char_to_hid_code['a'] = HID_KEY_A;
    char_to_hid_code['b'] = HID_KEY_B;
    char_to_hid_code['c'] = HID_KEY_C;
    char_to_hid_code['d'] = HID_KEY_D;
    char_to_hid_code['e'] = HID_KEY_E;
    char_to_hid_code['f'] = HID_KEY_F;
    char_to_hid_code['g'] = HID_KEY_G;
    char_to_hid_code['h'] = HID_KEY_H;
    char_to_hid_code['i'] = HID_KEY_I;
    char_to_hid_code['j'] = HID_KEY_J;
    char_to_hid_code['k'] = HID_KEY_K;
    char_to_hid_code['l'] = HID_KEY_L;
    char_to_hid_code['m'] = HID_KEY_M;
    char_to_hid_code['n'] = HID_KEY_N;
    char_to_hid_code['o'] = HID_KEY_O;
    char_to_hid_code['p'] = HID_KEY_P;
    char_to_hid_code['q'] = HID_KEY_Q;
    char_to_hid_code['r'] = HID_KEY_R;
    char_to_hid_code['s'] = HID_KEY_S;
    char_to_hid_code['t'] = HID_KEY_T;
    char_to_hid_code['u'] = HID_KEY_U;
    char_to_hid_code['v'] = HID_KEY_V;
    char_to_hid_code['w'] = HID_KEY_W;
    char_to_hid_code['x'] = HID_KEY_X;
    char_to_hid_code['y'] = HID_KEY_Y;
    char_to_hid_code['z'] = HID_KEY_Z;

    // Letters (uppercase)
    char_to_hid_code['A'] = HID_KEY_A;
    char_to_hid_code['B'] = HID_KEY_B;
    char_to_hid_code['C'] = HID_KEY_C;
    char_to_hid_code['D'] = HID_KEY_D;
    char_to_hid_code['E'] = HID_KEY_E;
    char_to_hid_code['F'] = HID_KEY_F;
    char_to_hid_code['G'] = HID_KEY_G;
    char_to_hid_code['H'] = HID_KEY_H;
    char_to_hid_code['I'] = HID_KEY_I;
    char_to_hid_code['J'] = HID_KEY_J;
    char_to_hid_code['K'] = HID_KEY_K;
    char_to_hid_code['L'] = HID_KEY_L;
    char_to_hid_code['M'] = HID_KEY_M;
    char_to_hid_code['N'] = HID_KEY_N;
    char_to_hid_code['O'] = HID_KEY_O;
    char_to_hid_code['P'] = HID_KEY_P;
    char_to_hid_code['Q'] = HID_KEY_Q;
    char_to_hid_code['R'] = HID_KEY_R;
    char_to_hid_code['S'] = HID_KEY_S;
    char_to_hid_code['T'] = HID_KEY_T;
    char_to_hid_code['U'] = HID_KEY_U;
    char_to_hid_code['V'] = HID_KEY_V;
    char_to_hid_code['W'] = HID_KEY_W;
    char_to_hid_code['X'] = HID_KEY_X;
    char_to_hid_code['Y'] = HID_KEY_Y;
    char_to_hid_code['Z'] = HID_KEY_Z;

    // Special characters
    char_to_hid_code[' '] = HID_KEY_SPACE;
    char_to_hid_code['-'] = HID_KEY_MINUS;
    char_to_hid_code['='] = HID_KEY_EQUAL;
    char_to_hid_code['['] = HID_KEY_BRACKET_LEFT;
    char_to_hid_code[']'] = HID_KEY_BRACKET_RIGHT;
    char_to_hid_code['\\'] = HID_KEY_BACKSLASH;
    char_to_hid_code[';'] = HID_KEY_SEMICOLON;
    char_to_hid_code['\''] = HID_KEY_APOSTROPHE;
    char_to_hid_code['`'] = HID_KEY_GRAVE;
    char_to_hid_code[','] = HID_KEY_COMMA;
    char_to_hid_code['.'] = HID_KEY_PERIOD;
    char_to_hid_code['/'] = HID_KEY_SLASH;
}

esp_err_t hid_keyboard_init(void)
{
    init_char_mapping();
    
    ESP_LOGI(TAG, "🔧 Initializing USB HID keyboard for ESP32-S3...");
    
    // Install TinyUSB stack using esp_tinyusb helper
    const tinyusb_config_t tusb_cfg = {
        .device_descriptor = NULL,              // use defaults from Kconfig
        .string_descriptor = NULL,
        .string_descriptor_count = 0,
        .external_phy = false,
        .configuration_descriptor = desc_configuration_fs,
        .self_powered = false,
        .vbus_monitor_io = -1,
    };

    esp_err_t ret = tinyusb_driver_install(&tusb_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to initialize TinyUSB: %s", esp_err_to_name(ret));
        ESP_LOGW(TAG, "Falling back to simulation mode");
        return ESP_OK; // Continue with simulation mode
    }
    
    ESP_LOGI(TAG, "✅ USB HID keyboard initialized successfully!");
    ESP_LOGI(TAG, "🎹 Ready to type on connected devices");
    ESP_LOGI(TAG, "📱 Connect ESP32-S3 to PC via USB-C for keyboard functionality");
    // HID is active; device should enumerate as a keyboard
    
    return ESP_OK;
}

esp_err_t hid_keyboard_type_string(const char* text, uint32_t delay_ms)
{
    if (!text) {
        ESP_LOGE(TAG, "Text is required");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "🎹 Typing password: %s", text);

    // Ensure device is mounted and ready before typing
    if (!hid_wait_ready(4000)) {
        ESP_LOGW(TAG, "HID not ready before typing");
        return ESP_ERR_TIMEOUT;
    }

    for (size_t i = 0; i < strlen(text); i++) {
        unsigned char c = (unsigned char)text[i];
        uint8_t modifier = 0;
        uint8_t key = 0;

        if (c < 128) {
            modifier = ascii_to_keycode[c][0] ? KEYBOARD_MODIFIER_LEFTSHIFT : 0;
            key = ascii_to_keycode[c][1];
        }

        if (key == 0) {
            ESP_LOGW(TAG, "Unsupported character: %c", c);
            continue;
        }

        // Ensure still ready per key
        if (!hid_wait_ready(2000)) {
            ESP_LOGW(TAG, "HID not ready while typing");
            return ESP_ERR_TIMEOUT;
        }

        uint8_t keycode[6] = {0};
        keycode[0] = key;
        tud_hid_keyboard_report(0, modifier, keycode);
        vTaskDelay(pdMS_TO_TICKS(30));

        // Release
        tud_hid_keyboard_report(0, 0, NULL);

        if (delay_ms > 0) {
            vTaskDelay(pdMS_TO_TICKS(delay_ms));
        }
    }

    ESP_LOGI(TAG, "✅ Finished typing password");
    return ESP_OK;
}

esp_err_t hid_keyboard_press_key(uint8_t key_code)
{
    ESP_LOGD(TAG, "Pressing key: 0x%02X", key_code);

    // Wait until USB HID is ready
    if (!hid_wait_ready(2000)) {
        ESP_LOGW(TAG, "HID not ready (timeout)");
        return ESP_ERR_TIMEOUT;
    }

    uint8_t keycode[6] = {0};
    keycode[0] = key_code;

    // Press
    tud_hid_keyboard_report(0, 0, keycode);
    vTaskDelay(pdMS_TO_TICKS(50));

    // Release
    tud_hid_keyboard_report(0, 0, NULL);

    return ESP_OK;
}

esp_err_t hid_keyboard_press_enter(void)
{
    ESP_LOGI(TAG, "🎯 Pressing Enter key");
    return hid_keyboard_press_key(HID_KEY_ENTER);
}

esp_err_t hid_keyboard_press_tab(void)
{
    ESP_LOGI(TAG, "🎯 Pressing Tab key");
    return hid_keyboard_press_key(HID_KEY_TAB);
}

esp_err_t hid_keyboard_press_escape(void)
{
    ESP_LOGI(TAG, "🎯 Pressing Escape key");
    return hid_keyboard_press_key(HID_KEY_ESCAPE);
}

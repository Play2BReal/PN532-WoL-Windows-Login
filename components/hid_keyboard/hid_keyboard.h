#ifndef HID_KEYBOARD_H
#define HID_KEYBOARD_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize HID keyboard
 * @return ESP_OK on success
 */
esp_err_t hid_keyboard_init(void);

/**
 * @brief Type a string as keyboard input
 * @param text Text to type
 * @param delay_ms Delay between keystrokes in milliseconds
 * @return ESP_OK on success
 */
esp_err_t hid_keyboard_type_string(const char* text, uint32_t delay_ms);

/**
 * @brief Press and release a key
 * @param key_code USB HID key code
 * @return ESP_OK on success
 */
esp_err_t hid_keyboard_press_key(uint8_t key_code);

/**
 * @brief Press Enter key
 * @return ESP_OK on success
 */
esp_err_t hid_keyboard_press_enter(void);

/**
 * @brief Press Tab key
 * @return ESP_OK on success
 */
esp_err_t hid_keyboard_press_tab(void);

/**
 * @brief Press Escape key
 * @return ESP_OK on success
 */
esp_err_t hid_keyboard_press_escape(void);

#ifdef __cplusplus
}
#endif

#endif // HID_KEYBOARD_H












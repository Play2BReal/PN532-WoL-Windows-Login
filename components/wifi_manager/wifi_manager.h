#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize WiFi manager
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_init(void);

/**
 * @brief Connect to WiFi network
 * @param ssid WiFi SSID
 * @param password WiFi password
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_connect(const char* ssid, const char* password);

/**
 * @brief Disconnect from WiFi
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_disconnect(void);

/**
 * @brief Check if WiFi is connected
 * @return true if connected, false otherwise
 */
bool wifi_manager_is_connected(void);
esp_err_t wifi_manager_check_connection(void);

/**
 * @brief Get current IP address
 * @param ip_str Buffer to store IP address string
 * @param max_len Maximum length of buffer
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_get_ip(char* ip_str, size_t max_len);

#ifdef __cplusplus
}
#endif

#endif // WIFI_MANAGER_H

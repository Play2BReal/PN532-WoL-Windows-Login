#ifndef WOL_CLIENT_H
#define WOL_CLIENT_H

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Send Wake-on-LAN magic packet
 * @param mac_address Target MAC address (format: "AA:BB:CC:DD:EE:FF")
 * @param ip_address Target IP address (optional, for directed WoL)
 * @param port UDP port (default: 9)
 * @return ESP_OK on success
 */
esp_err_t wol_send_magic_packet(const char* mac_address, const char* ip_address, uint16_t port);

// Send WoL magic packet to multiple common targets (ports 9 and 7,
// both directed to PC IP and subnet broadcast). Succeeds if any send succeeds.
esp_err_t wol_send_magic_packet_all(const char* mac_address, const char* ip_address);

/**
 * @brief Check if a host is reachable (ping)
 * @param ip_address Target IP address
 * @param timeout_ms Timeout in milliseconds
 * @return true if reachable, false otherwise
 */
bool wol_check_host_reachable(const char* ip_address, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif // WOL_CLIENT_H

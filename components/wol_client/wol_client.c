#include "wol_client.h"
#include "esp_log.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include <fcntl.h>
#include <errno.h>
#include "esp_netif.h"
#include "lwip/dns.h"
#include "string.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "wol_client";

esp_err_t wol_send_magic_packet(const char* mac_address, const char* ip_address, uint16_t port)
{
    if (!mac_address) {
        ESP_LOGE(TAG, "MAC address is required");
        return ESP_ERR_INVALID_ARG;
    }

    // Parse MAC address
    uint8_t mac[6];
    if (sscanf(mac_address, "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx",
               &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]) != 6) {
        ESP_LOGE(TAG, "Invalid MAC address format: %s", mac_address);
        return ESP_ERR_INVALID_ARG;
    }

    // Create magic packet (6 bytes of 0xFF + 16 repetitions of MAC address)
    uint8_t magic_packet[102];
    memset(magic_packet, 0xFF, 6);
    for (int i = 0; i < 16; i++) {
        memcpy(magic_packet + 6 + (i * 6), mac, 6);
    }

    // Create UDP socket
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Failed to create socket");
        return ESP_FAIL;
    }

    // Set socket options
    int broadcast = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast)) < 0) {
        ESP_LOGE(TAG, "Failed to set broadcast option");
        close(sock);
        return ESP_FAIL;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    if (ip_address && strlen(ip_address) > 0) {
        // Directed WoL to specific IP
        if (inet_aton(ip_address, &addr.sin_addr) == 0) {
            ESP_LOGE(TAG, "Invalid IP address: %s", ip_address);
            close(sock);
            return ESP_ERR_INVALID_ARG;
        }
        ESP_LOGI(TAG, "Sending directed WoL to %s:%d", ip_address, port);
    } else {
        // Broadcast WoL
        addr.sin_addr.s_addr = INADDR_BROADCAST;
        ESP_LOGI(TAG, "Sending broadcast WoL to port %d", port);
    }

    // Send magic packet
    ssize_t sent = sendto(sock, magic_packet, sizeof(magic_packet), 0,
                         (struct sockaddr*)&addr, sizeof(addr));
    
    if (sent < 0) {
        ESP_LOGE(TAG, "Failed to send magic packet: %s", strerror(errno));
        close(sock);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Magic packet sent successfully (%d bytes)", sent);
    close(sock);
    return ESP_OK;
}

esp_err_t wol_send_magic_packet_all(const char* mac_address, const char* ip_address)
{
    if (!mac_address) {
        return ESP_ERR_INVALID_ARG;
    }

    // Try directed unicast to PC IP on common ports
    esp_err_t ok = ESP_FAIL;
    if (ip_address && strlen(ip_address) > 0) {
        ESP_LOGI(TAG, "WoL: directed %s:9", ip_address);
        if (wol_send_magic_packet(mac_address, ip_address, 9) == ESP_OK) ok = ESP_OK;
        ESP_LOGI(TAG, "WoL: directed %s:7", ip_address);
        if (wol_send_magic_packet(mac_address, ip_address, 7) == ESP_OK) ok = ESP_OK;
    }

    // Compute subnet broadcast based on current STA IP/netmask
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif) {
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
            uint32_t bcast_addr = (ip_info.ip.addr & ip_info.netmask.addr) | (~ip_info.netmask.addr);
            struct in_addr bcast_in;
            bcast_in.s_addr = bcast_addr;
            char bcast_str[16];
            const char *res = inet_ntoa_r(bcast_in, bcast_str, sizeof(bcast_str));
            if (res != NULL) {
                ESP_LOGI(TAG, "WoL: subnet broadcast %s:9", bcast_str);
                if (wol_send_magic_packet(mac_address, bcast_str, 9) == ESP_OK) ok = ESP_OK;
                ESP_LOGI(TAG, "WoL: subnet broadcast %s:7", bcast_str);
                if (wol_send_magic_packet(mac_address, bcast_str, 7) == ESP_OK) ok = ESP_OK;
            }
        }
    }

    // Also try limited broadcast in case AP prefers it
    const char *bcast_any = "255.255.255.255";
    ESP_LOGI(TAG, "WoL: limited broadcast %s:9", bcast_any);
    if (wol_send_magic_packet(mac_address, bcast_any, 9) == ESP_OK) ok = ESP_OK;
    ESP_LOGI(TAG, "WoL: limited broadcast %s:7", bcast_any);
    if (wol_send_magic_packet(mac_address, bcast_any, 7) == ESP_OK) ok = ESP_OK;

    return ok;
}

bool wol_check_host_reachable(const char* ip_address, uint32_t timeout_ms)
{
    if (!ip_address) {
        ESP_LOGE(TAG, "IP address is required");
        return false;
    }

    // Try TCP connection to a small prioritized set of common Windows ports
    // 3389 (RDP), 135 (RPC), 445 (SMB). Keep list short to avoid long waits.
    int ports[] = {3389, 135, 445};
    int num_ports = sizeof(ports) / sizeof(ports[0]);
    int per_port_timeout_ms = (int)timeout_ms / (num_ports > 0 ? num_ports : 1);
    if (per_port_timeout_ms < 150) per_port_timeout_ms = 150; // minimum per port

    for (int i = 0; i < num_ports; i++) {
        int tcp_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (tcp_sock < 0) {
            ESP_LOGE(TAG, "Failed to create TCP socket");
            continue;
        }

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(ports[i]);

        if (inet_aton(ip_address, &addr.sin_addr) == 0) {
            ESP_LOGE(TAG, "Invalid IP address: %s", ip_address);
            close(tcp_sock);
            continue;
        }

        // Make socket non-blocking so connect won't stall
        int flags = fcntl(tcp_sock, F_GETFL, 0);
        if (flags >= 0) fcntl(tcp_sock, F_SETFL, flags | O_NONBLOCK);

        ESP_LOGD(TAG, "Trying to connect to %s:%d", ip_address, ports[i]);
        int result = connect(tcp_sock, (struct sockaddr*)&addr, sizeof(addr));
        int saved_errno = errno;

        if (result == 0) {
            ESP_LOGI(TAG, "✅ Host %s is reachable on port %d", ip_address, ports[i]);
            close(tcp_sock);
            return true;
        }

        if (saved_errno != EINPROGRESS && saved_errno != EALREADY && saved_errno != EWOULDBLOCK) {
            // Immediate error
            if (saved_errno == ECONNREFUSED) {
                ESP_LOGI(TAG, "✅ Host %s is up (connection refused on port %d)", ip_address, ports[i]);
                close(tcp_sock);
                return true;
            }
            ESP_LOGD(TAG, "❌ Immediate connect error to %s:%d (errno: %d)", ip_address, ports[i], saved_errno);
            close(tcp_sock);
            continue;
        }

        // Wait until socket is writable or timeout
        fd_set writefds;
        FD_ZERO(&writefds);
        FD_SET(tcp_sock, &writefds);
        struct timeval tv;
        tv.tv_sec = per_port_timeout_ms / 1000;
        tv.tv_usec = (per_port_timeout_ms % 1000) * 1000;
        int sel = select(tcp_sock + 1, NULL, &writefds, NULL, &tv);
        if (sel > 0 && FD_ISSET(tcp_sock, &writefds)) {
            int so_error = 0;
            socklen_t len = sizeof(so_error);
            if (getsockopt(tcp_sock, SOL_SOCKET, SO_ERROR, &so_error, &len) == 0) {
                if (so_error == 0) {
                    ESP_LOGI(TAG, "✅ Host %s is reachable on port %d", ip_address, ports[i]);
                    close(tcp_sock);
                    return true;
                }
                if (so_error == ECONNREFUSED) {
                    ESP_LOGI(TAG, "✅ Host %s is up (connection refused on port %d)", ip_address, ports[i]);
                    close(tcp_sock);
                    return true;
                }
                ESP_LOGD(TAG, "❌ Connect SO_ERROR=%d to %s:%d", so_error, ip_address, ports[i]);
            }
        } else if (sel == 0) {
            ESP_LOGD(TAG, "⏳ Connect timeout to %s:%d (treating as down)", ip_address, ports[i]);
        } else {
            ESP_LOGD(TAG, "❌ select() error while connecting to %s:%d", ip_address, ports[i]);
        }

        close(tcp_sock);
    }

    ESP_LOGI(TAG, "Host %s is not reachable on any common ports", ip_address);
    return false;
}

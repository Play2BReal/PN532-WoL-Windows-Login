idf_component_register(SRCS "wol_client.c"
                    INCLUDE_DIRS "."
                    REQUIRES lwip esp_netif esp_wifi)

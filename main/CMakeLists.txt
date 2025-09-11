idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES wifi_manager hid_keyboard wol_client esp_timer nvs_flash esp_tinyusb)




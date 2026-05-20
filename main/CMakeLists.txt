idf_component_register(SRCS "main.c" "audio_utils.c"
                    INCLUDE_DIRS "."
                    REQUIRES driver esp_http_client nvs_flash esp_wifi esp_event esp_netif lwip cjson esp-tls mbedtls)
idf_component_register(SRCS "audio_stream.c" "captive_dns.c" "main.c" "ws_server.c"
                    INCLUDE_DIRS "."
                    REQUIRES "esp_common"
                             "esp_driver_i2s"
                             "esp_event"
                             "esp_http_server"
                             "esp_netif"
                             "esp_system"
                             "esp_wifi"
                             "freertos"
                             "log"
                             "nvs_flash"
                    PRIV_REQUIRES spiffs)

spiffs_create_partition_image(spiffs spiffs_image FLASH_IN_PROJECT)

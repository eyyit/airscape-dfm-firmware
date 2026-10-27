idf_component_register(SRCS "app_main.c" "sensor.c" "runtime.c" "network.c" "metrics.c" "servers.c" "ota.c" "syslog.c"
    INCLUDE_DIRS "." "${CMAKE_BINARY_DIR}/generated"
    REQUIRES dfm_core esp_driver_i2c esp_driver_gpio esp_wifi esp_netif esp_event
        esp_timer esp_http_server nvs_flash app_update json
        esp_system esp_app_format mbedtls
    EMBED_FILES "../web/index.html" "../web/console.js")

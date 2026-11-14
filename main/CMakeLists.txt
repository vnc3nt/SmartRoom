idf_component_register(SRCS "smart.cpp" "ir_nec_encoder.c" "led_strip_encoder.c"
                       PRIV_REQUIRES esp_driver_rmt esp_matter nvs_flash esp_event esp_wifi
                       INCLUDE_DIRS ".")
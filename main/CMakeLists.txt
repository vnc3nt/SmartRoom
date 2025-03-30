idf_component_register(SRCS "ir_nec_transceiver_main.c" "ir_nec_encoder.c"
                       PRIV_REQUIRES esp_driver_rmt esp_matter
                       INCLUDE_DIRS ".")


# ir_nec_transceiver_main
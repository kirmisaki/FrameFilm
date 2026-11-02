# Minimal ST25R3916 NFC library (ESP-IDF 5.5)
idf_component_register(
    SRCS
        "src/st25r3916_hal.c"
        "src/st25r3916_pcd.c"
        "src/st25r3916_picc.c"
        "src/st25r3916_ntag.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    REQUIRES esp_driver_spi esp_driver_gpio
    PRIV_REQUIRES esp_timer esp_hw_support soc hal
)

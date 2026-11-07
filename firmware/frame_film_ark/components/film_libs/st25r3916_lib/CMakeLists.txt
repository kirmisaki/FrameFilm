# Minimal ST25R3916 NFC library (ESP-IDF 5.5)
# 本组件位于 EXTRA_COMPONENT_DIRS，IDF 会把它排在依赖方（film_hal）之前，
# 普通静态链接会丢符号；NFC 变体用整库链接规避顺序问题（n4r2 不引用，不参与链接）。
set(whole_archive_arg)
if(CONFIG_ARK_HAS_NFC)
    set(whole_archive_arg WHOLE_ARCHIVE)
endif()

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
    ${whole_archive_arg}
)

set(srcs "src/hal_init.c"
         "src/hal_button.c"
         "src/hal_led.c"
         "src/hal_sd.c"
         "src/hal_flash.c"
         "src/hal_epd.c"
         "src/hal_audio.c"
         "src/hal_usb.c"
         )

set(public_include "inc")

set(requires film_sys led_strip driver fatfs spiffs button esp_driver_i2c esp_driver_i2s esp_codec_dev usb_device_uac)

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS ${public_include}
                       REQUIRES ${requires}
                      )

# UAC composite 模式（CONFIG_USB_DEVICE_UAC_AS_PART=y）下，usb_device_uac 组件不再向
# tinyusb 注入配置与头文件目录，需由工程补上：配置复用组件的 tusb_config.h，另用编译宏打开 HID/CDC
idf_component_get_property(tusb_lib espressif__tinyusb COMPONENT_LIB)
idf_component_get_property(uac_dir espressif__usb_device_uac COMPONENT_DIR)
idf_component_get_property(usb_lib usb COMPONENT_LIB)

target_include_directories(${tusb_lib} PUBLIC
    "${uac_dir}/tusb"       # tusb_config.h
    "${uac_dir}/tusb_uac")  # uac_config.h / uac_descriptors.h / tusb_config_uac.h
target_compile_definitions(${tusb_lib} PUBLIC
    CFG_TUD_HID=1 CFG_TUD_HID_EP_BUFSIZE=8
    CFG_TUD_CDC=1 CFG_TUD_CDC_RX_BUFSIZE=512 CFG_TUD_CDC_TX_BUFSIZE=512)
target_link_libraries(${tusb_lib} PUBLIC ${usb_lib})
# hal_usb.c 内含 USB 描述符，需直接使用 tinyusb（含其配置与 CFG_TUSB_MCU）
target_link_libraries(${COMPONENT_LIB} PUBLIC ${tusb_lib})

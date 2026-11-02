// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Isolate board identity/configuration from all classic S3 model macros.
#define __SYS_CFG_H__
#define FRAMEFILM_C3 1
#define FRAMEFILM_STD 0
#define FRAMEFILM_PRO 0
#define FRAMEFILM_MAX 0
#define SYS_DEVICE_NAME "FRAMEFILMC3"
#define SYS_MANUFACTURER_NAME "FRAMEFILMC3"
#define SYS_MODEL_NUMBER "C3-JD79665AA"
#define SYS_SERIAL_NUMBER "C3000001"
#define SYS_HAREWARE_VERSION "C3-devboard"
#define SYS_FIRMWARE_VERSION "0.1.0"
#define SYS_SYSTEM_ID "FrameFilmC3"
#define SYS_BLE_DEFAULT_KEY "FRAMEFILM_KEY"
#define SYS_M_NVS_NAMESPACE "FRAMEFILM_C3"
#define SYS_M_NVS_KEY_NAME "FILMKEY"
#define BACE_PATH "/spiffs"
#define MONITOR_AUTO_SLEEP_TIMEOUT_SEC_LOW 60
// Guards also prevent classic hal_api.h's local includes selecting S3 pins.
#include "hal_epd_c3.h"
#include "hal_led_c3.h"

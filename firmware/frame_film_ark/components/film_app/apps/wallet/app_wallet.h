#ifndef __APP_WALLET_H__
#define __APP_WALLET_H__

#include "app_interface.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 卡包 app（仅 n16r8，SYS_NFC_ENABLE=1 时注册） */
extern const app_entry_t g_app_wallet_entry;

/* 入睡前：以当前卡进入模拟，供 monitor 保持供电并武装 IRQ。app_task 上下文。 */
void app_wallet_sleep_prepare(void);

/* 离开卡包/入睡停 app 前调用：取消扫描、停止模拟。app_task 上下文。 */
void app_wallet_deactivate(void);

/* BLE 卡包命令入口（app_task 上下文，由 app_manager 路由）。 */
void app_wallet_on_ble_cmd(uint8_t ch, const uint8_t *data, uint8_t len);

#ifdef __cplusplus
}
#endif

#endif /* __APP_WALLET_H__ */

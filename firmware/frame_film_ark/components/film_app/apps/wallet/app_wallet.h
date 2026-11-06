#ifndef __APP_WALLET_H__
#define __APP_WALLET_H__

#include "app_interface.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 卡包 app（仅 n16r8，SYS_NFC_ENABLE=1 时注册） */
extern const app_entry_t g_app_wallet_entry;

/* ---- NFC 总开关（持久化在 app8 槽位，默认开）----
 * 关闭 = 停止模拟且不再自动起；开启且卡库有卡 = 以当前选中的卡常驻模拟。
 * 取值/写入均在 app_task 上下文（与其它参数写入同一串行约定）。 */
uint8_t app_wallet_nfc_enabled(void);

/* 改开关并立即生效（落盘 + 起停模拟）。app_task 上下文。 */
void app_wallet_set_nfc_enabled(uint8_t enable);

/* 正在模拟卡片（开关开 + 有当前卡 + 模拟任务在跑）。app_task 上下文。 */
uint8_t app_wallet_emu_active(void);

/* 按"开关 + 卡库"把模拟同步到当前卡：开且有卡 → 模拟，否则停止。
 * 切 app / 开关变化 / 卡库变更后调用，保证离开卡包也一直模拟。app_task 上下文。 */
void app_wallet_emu_sync(void);

/* 离开卡包但保持模拟：取消读卡、回到模拟当前卡。app_task 上下文。 */
void app_wallet_leave(void);

/* 入睡前：以当前卡进入模拟，供 monitor 保持供电并武装 IRQ。app_task 上下文。 */
void app_wallet_sleep_prepare(void);

/* 入睡停 app 前调用：取消扫描、停止模拟。app_task 上下文。 */
void app_wallet_deactivate(void);

/* BLE 卡包命令入口（app_task 上下文，由 app_manager 路由）。 */
void app_wallet_on_ble_cmd(uint8_t ch, const uint8_t *data, uint8_t len);

#ifdef __cplusplus
}
#endif

#endif /* __APP_WALLET_H__ */

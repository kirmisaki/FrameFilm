#ifndef __APP_BOOT_CFG_H__
#define __APP_BOOT_CFG_H__

/*********************************************************************
 * INCLUDES
 */
#include <stdint.h>

#include "app_interface.h"

/*********************************************************************
 * CPPMIX
 */
#ifdef __cplusplus
extern "C" {
#endif

/*********************************************************************
 * MACROS
 */
/* 布局版本：v2 增加语言；读取时兼容 v1 两字节开机配置 */
#define APP_BOOT_CFG_VER        (2)
#define APP_LANGUAGE_ZH_CN     (0)
#define APP_LANGUAGE_EN        (1)

/* BOOT PAGE：上电是否先画开机画面（首帧顺带完成整屏清场） */
#define APP_BOOT_PAGE_SHOW      (0)     // 显示（默认，与加此参数前的行为一致）
#define APP_BOOT_PAGE_SKIP      (1)     // 跳过，直接进 START APP

/* START APP 候选。**下标即存盘值**，设置页的候选表顺序必须与此一致。 */
typedef enum {
    APP_START_MENU = 0,     // 主菜单（默认）
    APP_START_LAST,         // 上次运行的 app（无记录回落图片）
    APP_START_IMAGE,
    APP_START_TEMPLATE,
    APP_START_CLOCK,
    APP_START_ANIMATION,
    APP_START_SETTINGS,
    APP_START_PASS,
    APP_START_WALLET,
    APP_START_NUM,
} app_start_t;

/*********************************************************************
* TYPEDEFS
*/
/**
 * @brief 开机行为配置（app 层参数）
 *
 * 存在 service_param 的保留槽位 `app7`（见 SERVICE_PARAM_APP_ID_BOOT_CFG）：
 * 借用 app 状态那套 blob 持久化（magic/size/version 校验），app 层不写 NVS 代码。
 *
 * 为什么不做成 service_param 的字段：这两个参数只描述"上电时 app 层怎么走"，
 * 与 service 层（WiFi/BLE/休眠）无关；放 app 层也避免改 ServiceParam_Def_t
 * 布局带来的整体重置（那会连带清掉 WiFi 配网与时区）。
 */
typedef struct {
    uint8_t boot_page;      // APP_BOOT_PAGE_SHOW / SKIP
    uint8_t start_app;      // app_start_t
    uint8_t language;       // APP_LANGUAGE_ZH_CN / EN
} app_boot_cfg_t;

/*********************************************************************
 * GLOBAL FUNCTIONS
 */
/**
 * @brief 载入配置（无数据/校验失败回落默认值）。在 film_app_init 早期调用一次
 */
void app_boot_cfg_init(void);

/**
 * @brief 取当前配置（只读，永不为 NULL）
 */
const app_boot_cfg_t *app_boot_cfg_get(void);

/**
 * @brief 改配置并落盘（校验失败忽略；值未变化则不写，省 NVS 擦写）
 *
 * 调用上下文：app 任务（与其它参数写入同一串行约定）。
 */
void app_boot_cfg_set(uint8_t boot_page, uint8_t start_app);
void app_language_set(uint8_t language);
uint8_t app_language_get(void);

/**
 * @brief 解析 START APP 得到真正要进入的 app
 *
 * 逐个排除不可用目标：主菜单未注册（模式裁剪/UI 未启用）→ 图片；
 * 指定 app 未注册 → 图片；`APP_START_LAST` 取上次运行的 app，越界/未注册 → 图片。
 *
 * @return 一定是一个已注册的 app
 */
app_id_t app_boot_cfg_resolve_target(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_BOOT_CFG_H__ */

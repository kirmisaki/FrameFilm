#ifndef __APP_MANAGER_H__
#define __APP_MANAGER_H__

/*********************************************************************
 * INCLUDES
 */
#include "app_interface.h"

/*********************************************************************
 * CPPMIX
 */
#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 按键切换交互模式（生效值）
 *
 * 由 sys_cfg.h 的 SYS_APP_SWITCH_MODE 配置，运行期解析：
 * FULL 在屏幕不具备封面菜单能力时自动降级为 SIMPLE。
 */
typedef enum {
    APP_SWITCH_MODE_NONE = 0,   // 关闭按键切换（BLE 远程切换仍有效）
    APP_SWITCH_MODE_SIMPLE,     // 简易：图片 <-> 最近推送的 app
    APP_SWITCH_MODE_FULL,       // 全功能：长按进封面菜单
} app_switch_mode_t;

/**
 * @brief 初始化 app 调度器
 *
 * 创建 app 任务与事件队列，并统一注册输入回调转发到 app_manager。
 * 由 film_app_init() 调用。
 */
void app_manager_init(void);

/**
 * @brief 注册一个 app 到调度器
 *
 * @param app app 接口实例
 */
void app_manager_register(const app_entry_t *app);

/**
 * @brief 请求切换到指定 app
 *
 * 通过事件队列异步触发，实际 on_exit/on_enter 在 app 任务上下文执行。
 * 若队列尚未就绪（初始化早期），则同步执行切换。
 *
 * @param id 目标 app 的 ID
 */
void app_manager_switch(app_id_t id);

/**
 * @brief 输入事件转发入口（由 app_init 注册的 hal_input 回调调用）
 *
 * 将 hal_input 按键回调统一转发为 APP_EVT_INPUT 事件排队，供 app 任务消费。
 *
 * @param key 按键类型
 */
void app_manager_on_input(input_press_type_t key);

/**
 * @brief 获取当前运行的 app ID
 *
 * @return app_id_t 当前 app ID
 */
app_id_t app_manager_get_current(void);

/**
 * @brief 获取生效的按键切换模式
 *
 * 供上层查询/日志使用（FULL 可能已被降级为 SIMPLE）。
 *
 * @return app_switch_mode_t 生效模式
 */
app_switch_mode_t app_manager_get_switch_mode(void);

/**
 * @brief 指定 app 是否已注册
 *
 * 供启动恢复时校验“上次运行的 app 是否在当前模式下仍可用”（被裁剪的 app 需回落）。
 *
 * @param id 目标 app ID
 * @return 1 已注册；0 未注册
 */
int app_manager_is_registered(app_id_t id);

/**
 * @brief 向首个 app 投递一次 APP_EVT_BOOT
 *
 * 由 app_init 在恢复上次 app 后调用，驱动“开机自动”行为（自动切图 / 自动拉取）。
 */
void app_manager_notify_boot(void);

/**
 * @brief 保存当前 app 的状态到 NVS
 *
 * app 未声明状态（state/state_size 为空）时是 no-op。
 *
 * @return 0 成功；负值失败
 */
int app_state_save(void);

/**
 * @brief 从 NVS 载入当前 app 的状态
 *
 * 失败（无数据 / 校验不过 / 版本不符）时套用 app 声明的 state_default。
 *
 * @return 0 成功；负值已回落默认值
 */
int app_state_load(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_MANAGER_H__ */

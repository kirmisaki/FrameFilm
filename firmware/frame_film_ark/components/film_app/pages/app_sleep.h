#ifndef __APP_SLEEP_H__
#define __APP_SLEEP_H__

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

/*********************************************************************
 * GLOBAL FUNCTIONS
 */

/**
 * @brief 取得休眠卡页面契约（由 app_manager_sleep_show() 展示）
 *
 * 全屏、不带 app_shell 外壳：休眠意味着"UI 体系已经关掉"，留着状态栏与提示行
 * 会让人以为设备还醒着。版式见 tools/ui-mockup §08。
 */
const app_ui_ops_t *app_sleep_ops(void);

/**
 * @brief 登记定时唤醒信息（展示休眠卡之前由 app 任务侧填好）
 *
 * 页面跑在 ui_task，读不到 g_service_param，所以这两个值必须由 app 任务侧读出后
 * 送进来 —— 与开机页的 app_boot_set_telemetry() 是同一套路。
 * 页脚据此显示 `AUTO WAKE hh:mm`（开了）或 `AUTO WAKE OFF`（没开）。
 *
 * @param auto_on 定时唤醒是否生效（sleep_auto 且 sleep_time > 0）
 * @param minutes 唤醒间隔（分钟），auto_on 为 0 时忽略
 */
void app_sleep_set_info(uint8_t auto_on, uint16_t minutes);

/**
 * @brief 出示休眠卡（可选）→ 等卡片画完 + 唤醒条件解除（**不发低功耗请求**）
 *
 * 从 app_sleep_run() 拆出来，供 app_manager 的休眠屏障在**闲置自动休眠 / 低电**
 * 路径上复用：那条路径由 monitor 主动发起，不需要（也不该）再回调一次 request_sleep。
 *
 * 必须从 app 任务调用（内部会阻塞等待，且要读唤醒脚电平）。
 *
 * @param show_card         是否出示休眠卡；0 表示"就着当前画面睡"
 * @param from_armed_timer  页脚唤醒信息取自哪个来源：
 *                          1 = 已配下去的定时唤醒源（自动 / 低电路径，低电时不设 → OFF）
 *                          0 = 按休眠参数推断（手动路径，monitor 稍后才配唤醒源）
 */
void app_sleep_present(uint8_t show_card, uint8_t from_armed_timer);

/**
 * @brief 执行一次"手动休眠"：出示休眠卡（可选）→ 等卡片画完 + 唤醒条件解除 → 请求进低功耗
 *
 * 必须从 app 任务调用（内部会阻塞等待，且要读唤醒脚电平）。
 * 触发点有两处：主菜单长按（画休眠卡）、app 内长按（按当前 app 的
 * `app_entry_t.sleep_face` 决定画卡还是留画面，见 app_manager_sleep_from_app）。
 * **不理会休眠模式开关** —— 用户明确按下的动作就该执行。
 *
 * @param show_card 是否出示休眠卡；0 表示"就着当前画面睡"
 *
 * 提交休眠请求后返回；monitor 等待网络恢复和唤醒脚释放再进入 deep sleep。
 * 调用方须保持 app 的休眠门闸，不能在等待期间恢复绘制。
 */
void app_sleep_run(uint8_t show_card);

#ifdef __cplusplus
}
#endif

#endif /* __APP_SLEEP_H__ */

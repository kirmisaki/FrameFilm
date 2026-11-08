#ifndef __HAL_PWR_H__
#define __HAL_PWR_H__


/*********************************************************************
 * INCLUDES
 */
#include <stdbool.h>
#include <stdint.h>


/*********************************************************************
 * CPPMIX
 */
#ifdef __cplusplus
extern "C" {
#endif

/*********************************************************************
 * MACROS
 */


/*********************************************************************
* TYPEDEFS
*/
typedef enum {
    HAL_PWR_WAKE_NONE = 0,
    HAL_PWR_WAKE_KEY,       // EXT0：GPIO5 按键
    HAL_PWR_WAKE_TIMER,     // 定时器
    HAL_PWR_WAKE_NFC,       // EXT1：GPIO7 IRQ
    HAL_PWR_WAKE_OTHER,
} hal_pwr_wake_cause_t;


/*********************************************************************
 * CONSTANTS
 */


/*********************************************************************
 * LOCAL VARIABLES
 */


/*********************************************************************
 * GLOBAL VARIABLES
 */


/*********************************************************************
 * LOCAL FUNCTIONS
 */


/*********************************************************************
 * GLOBAL FUNCTIONS
 */
/**
 * @brief 初始化电源管理模块
 * 
 * 该函数用于初始化电源管理模块，配置唤醒源和相关参数。
 * 通常在系统启动时调用此函数。
 */
extern void hal_pwr_init(void);

/**
 * @brief 配好本次入睡的唤醒源（ext0 按键 + 可选的定时器）
 *
 * **必须在卸载外设之前调用**：hal_pwr_enter_sleep() 里 ext0 配置失败会直接返回，
 * 那时若定时器已停、外设已 deinit，设备就卡在"卸了却睡不着"的状态。
 *
 * @return true 配置成功（可以放心往下卸外设）；false 配置失败（本次不要入睡）
 */
extern bool hal_pwr_wakeup_prepare(void);

/**
 * @brief 进入深度睡眠模式
 * 
 * 该函数使设备进入深度睡眠模式以节省电量。
 * 在深度睡眠模式下，大部分外设将被关闭，仅保留RTC和唤醒源。
 * 设备可通过配置的唤醒源（如按键）唤醒。
 *
 * 唤醒源由 hal_pwr_wakeup_prepare() 提前配好，本函数只切断外设供电并开始睡眠。
 */
extern void hal_pwr_enter_sleep(void);

/**
 * @brief 设置定时器唤醒时间
 * 
 * @param minutes 唤醒时间（单位：分钟），0表示不使用定时器唤醒
 */
extern void hal_pwr_set_timer_wakeup(uint32_t minutes);

/**
 * @brief 查询已设置的定时器唤醒间隔
 *
 * 睡眠卡页脚要显示"会不会自动醒"，必须以实际配下去的唤醒源为准：
 * 低电入睡不设定时唤醒，若照参数推断会显示一个不会发生的时刻。
 *
 * @return 唤醒间隔（分钟）；0 表示未启用定时唤醒
 */
extern uint32_t hal_pwr_get_timer_wakeup(void);

/**
 * @brief 检查唤醒原因
 * 
 * 该函数用于检查设备是否从深度睡眠中唤醒，并返回唤醒原因。
 * 
 * @return bool true-从深度睡眠唤醒，false-正常启动
 */
extern bool hal_pwr_check_wakeup(void);

/**
 * @brief 本次启动的唤醒原因（区分按键 / 定时 / NFC IRQ）
 */
extern hal_pwr_wake_cause_t hal_pwr_get_wakeup_cause(void);

/**
 * @brief 唤醒条件此刻是否已经成立
 *
 * ext0 是**电平**触发：若在调用 hal_pwr_enter_sleep() 的那一刻唤醒脚就已处于
 * 触发电平，条件当场成立、设备会立刻醒回来。典型场景是"长按唤醒键入睡" ——
 * 长按是在**按住期间**上报的，此刻手指还在键上，而那个脚正是唤醒脚。
 *
 * 所以入睡前必须查一次，等它归为 false 再断电。这里只回答"这一路 GPIO 的电平"，
 * 不关心是哪个按键：三机型的唤醒脚恰好都落在"确认"键上。
 *
 * @return true 已成立（此刻入睡会立刻醒）；false 未成立（可以入睡）
 */
extern bool hal_pwr_wake_condition_met(void);


#ifdef __cplusplus
}
#endif

#endif /* __HAL_PWR_H__ */

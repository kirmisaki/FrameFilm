#ifndef __HAL_BUTTON_H__
#define __HAL_BUTTON_H__


/*********************************************************************
 * INCLUDES
 */


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
    HAL_BUTTON_PRESS_NONE = 0,
    HAL_BUTTON_PRESS_SHORT,  // 按键短按
    HAL_BUTTON_PRESS_LONG ,  // 按键长按
    HAL_BUTTON_PRESS_UP,     // 上/右按键
    HAL_BUTTON_PRESS_DOWN,   // 下/左按键
    HAL_BUTTON_PRESS_PRESSED,
    HAL_BUTTON_PRESS_MAX,
} hal_button_press_type_t;

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
 * TYPEDEFS
 */
typedef void (*button_callback_t)(void);

/*********************************************************************
 * GLOBAL FUNCTIONS
 */
/**
 * @brief 初始化按键硬件模块
 * 该函数用于对按键进行初始化操作，确保按键能够正常工作。
 */
extern void hal_button_init(void);

/**
 * @brief 注册按键回调函数
 * @param type 事件类型
 * @param cb 回调函数指针
 * @return 0 成功，其他值 失败
 */
extern int hal_button_register_cb(hal_button_press_type_t type, button_callback_t cb);

/**
 * @brief 注销按键回调函数
 * @param type 事件类型
 * @param cb 回调函数指针
 * @return 0 成功，其他值 失败
 */
extern int hal_button_unregister_cb(hal_button_press_type_t type, button_callback_t cb);

/**
 * @brief 反初始化按键硬件模块
 * 释放按键资源，将按键引脚设为高阻态
 */
extern void hal_button_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* __HAL_BUTTON_H__ */

#ifndef __HAL_INPUT_H__
#define __HAL_INPUT_H__


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
    INPUT_PRESS_NONE = 0,
    INPUT_PRESS_SHORT,  // 编码器短按
    INPUT_PRESS_LONG ,  // 编码器长按
    INPUT_PRESS_UP,     // 编码器+
    INPUT_PRESS_DOWN,   // 编码器-
    INPUT_PRESS_PRESSED,
    INPUT_PRESS_MAX,
} input_press_type_t;

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
typedef void (*input_callback_t)(void);

/*********************************************************************
 * GLOBAL FUNCTIONS
 */
/**
 * @brief 初始化编码器硬件模块
 * 该函数用于对编码器进行初始化操作，确保编码器能够正常工作。
 */
extern void hal_input_init(void);

/**
 * @brief 注册编码器回调函数
 * @param type 事件类型
 * @param cb 回调函数指针
 * @return 0 成功，其他值 失败
 */
extern int hal_input_register_cb(input_press_type_t type, input_callback_t cb);

/**
 * @brief 注销编码器回调函数
 * @param type 事件类型
 * @param cb 回调函数指针
 * @return 0 成功，其他值 失败
 */
extern int hal_input_unregister_cb(input_press_type_t type, input_callback_t cb);

/**
 * @brief 反初始化编码器硬件模块
 * 释放 rotary_input 资源，将编码器引脚设为高阻态
 */
extern void hal_input_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* __HAL_INPUT_H__ */

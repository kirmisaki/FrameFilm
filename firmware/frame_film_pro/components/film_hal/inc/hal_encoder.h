#ifndef __HAL_ENCODER_H__
#define __HAL_ENCODER_H__


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
    ENCODER_PRESS_NONE = 0,
    ENCODER_PRESS_SHORT,  // 编码器短按
    ENCODER_PRESS_LONG ,  // 编码器长按
    ENCODER_PRESS_UP,     // 编码器+
    ENCODER_PRESS_DOWN,   // 编码器-
    ENCODER_PRESS_PRESSED,
    ENCODER_PRESS_MAX,
} encoder_press_type_t;

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
typedef void (*encoder_callback_t)(void);

/*********************************************************************
 * GLOBAL FUNCTIONS
 */
/**
 * @brief 初始化编码器硬件模块
 * 该函数用于对编码器进行初始化操作，确保编码器能够正常工作。
 */
extern void hal_encoder_init(void);

/**
 * @brief 注册编码器回调函数
 * @param type 事件类型
 * @param cb 回调函数指针
 * @return 0 成功，其他值 失败
 */
extern int hal_encoder_register_cb(encoder_press_type_t type, encoder_callback_t cb);

/**
 * @brief 注销编码器回调函数
 * @param type 事件类型
 * @param cb 回调函数指针
 * @return 0 成功，其他值 失败
 */
extern int hal_encoder_unregister_cb(encoder_press_type_t type, encoder_callback_t cb);

/**
 * @brief 反初始化编码器硬件模块
 * 释放 rotary_encoder 资源，将编码器引脚设为高阻态
 */
extern void hal_encoder_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* __HAL_ENCODER_H__ */

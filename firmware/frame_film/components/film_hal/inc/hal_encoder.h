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
    ENCODER_PRESS_SHORT,
    ENCODER_PRESS_LONG ,
    ENCODER_PRESS_DOUBLE,
    ENCODER_PRESS_UP,
    ENCODER_PRESS_DOWN,
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
 * @brief 获取编码器的按键状态
 * 该函数用于读取编码器的按键是否被按下，并返回相应的状态值。
 * @return int 按键状态，具体含义根据实现而定。
 */
extern encoder_press_type_t hal_encoder_get_press(void);

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

#ifdef __cplusplus
}
#endif

#endif /* __HAL_ENCODER_H__ */

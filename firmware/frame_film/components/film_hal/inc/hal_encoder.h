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
 * @brief 设置编码器的提示音启用状态
 * 该函数用于控制编码器的提示音是否启用。
 * @param enable 一个无符号8位整数，当值非零时表示启用提示音，值为零时表示禁用提示音。
 */
extern void hal_encoder_set_tone(uint8_t enable);


#ifdef __cplusplus
}
#endif

#endif /* __HAL_ENCODER_H__ */

#ifndef __SERVICE_FILM_H__
#define __SERVICE_FILM_H__


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
#define FILM_TAG                    "film"

#define FILM_PLAY_MODE_MANUAL       (0)  // 手动播放模式
#define FILM_PLAY_MODE_AUTO         (1)  // 自动播放模式

/*********************************************************************
* TYPEDEFS
*/
typedef enum {
    MSG_FILM_DISPLAY,          // 显示指定图片
    MSG_FILM_NEXT,             // 显示下一张图片
    MSG_FILM_PREV,             // 显示上一张图片
    MSG_FILM_INIT,             // 初始化播放
    MSG_FILM_AUTO_PLAY,        // 自动播放
    MSG_FILM_CLEAR,            // EPD维护显示为白色用于长期保存
} film_msg_type_t;

typedef struct {
    film_msg_type_t ID;
    uint32_t file_id;  // 用于MSG_FILM_DISPLAY
} film_msg_t;

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
 * @brief 初始化照片播放服务
 *
 * 此函数用于初始化照片播放服务，创建任务和消息队列，
 * 初始化状态变量，检查服务参数。
 */
extern void service_film_init(void);

/**
 * @brief 显示指定图片
 *
 * 此函数用于显示指定ID的图片。
 *
 * @param file_id 文件ID
 */
extern void service_film_display(uint32_t file_id);

/**
 * @brief 显示下一张图片
 *
 * 此函数用于显示下一张图片，实现循环切换。
 */
extern void service_film_next(void);

/**
 * @brief 显示上一张图片
 *
 * 此函数用于显示上一张图片，实现循环切换。
 */
extern void service_film_prev(void);

/**
 * @brief 清除显示
 *
 * 此函数用于清除当前显示的图片，维护EPD显示为白色。
 */
extern void service_film_clear(void);

/**
 * @brief 设置播放模式
 *
 * 此函数用于设置播放模式。
 *
 * @param mode 播放模式（0：手动，1：自动）
 */
extern void service_film_set_play_mode(uint8_t mode);

/**
 * @brief 获取当前显示的文件ID
 *
 * 此函数用于获取当前显示的文件ID。
 *
 * @return uint32_t 当前文件ID
 */
extern uint32_t service_film_get_current_id(void);

/**
 * @brief 获取加载完成状态
 *
 * 此函数用于获取加载完成状态。
 *
 * @return uint8_t 加载完成状态（0：未完成，1：完成）
 */
extern uint8_t service_film_get_load_complete(void);


#ifdef __cplusplus
}
#endif

#endif /* __SERVICE_FILM_H__ */

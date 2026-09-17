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

/*********************************************************************
* TYPEDEFS
*/
typedef enum {
    MSG_FILM_DISPLAY,          // 显示指定文件（整张/首帧）
    MSG_FILM_RENDER,           // 按帧索引渲染（动图用）
} film_msg_type_t;

typedef struct {
    film_msg_type_t ID;
    uint32_t file_id;          // 用于 MSG_FILM_DISPLAY / MSG_FILM_RENDER
    uint32_t frame_idx;        // 用于 MSG_FILM_RENDER
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
 * @brief 初始化图片渲染服务
 *
 * 此处函数用于初始化图片渲染服务，创建任务和消息队列。
 */
extern void service_film_init(void);

/**
 * @brief 显示指定文件（整张/首帧）
 *
 * 加载指定ID的文件并渲染（v1/v2 单帧，多帧取首帧）。
 *
 * @param file_id 文件ID
 */
extern void service_film_display(uint32_t file_id);

/**
 * @brief 按帧索引渲染（动图）
 *
 * 校验文件头后按 frame_idx 定位帧数据并分派到对应驱动，
 * 内部自动完成该帧的完整刷新（含刷相）。
 *
 * @param file_id   文件ID
 * @param frame_idx 帧索引（0 起）
 */
extern void service_film_render_frame(uint32_t file_id, uint32_t frame_idx);

/**
 * @brief 获取文件帧数
 *
 * 读取文件头 偏移 0x0A（小端）。0/1 视为单帧，>1 为多帧动图。
 *
 * @param file_id 文件ID
 * @return uint32_t 帧数（至少 1）
 */
extern uint32_t service_film_get_frame_count(uint32_t file_id);


#ifdef __cplusplus
}
#endif

#endif /* __SERVICE_FILM_H__ */

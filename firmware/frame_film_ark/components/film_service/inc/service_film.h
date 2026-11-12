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
    MSG_FILM_MONO_END,         // 结束黑白快刷会话（面板断电，见 hal_epd_mono_session_end）
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
 * @brief 是否已有显示/按帧渲染请求排队或正在上屏
 *
 * 供"按帧推屏"的 app（动图）做流控：面板刷新慢于推送节奏时，要等上一帧
 * 落屏再推下一帧，否则请求会堆积（退出后仍在刷屏，队列满还会阻塞调用任务）。
 * 图片 app 也用它判断"上一张还在刷新"，刷新期间丢弃翻页按键。
 *
 * @return int 1:忙（应等待 / 应丢弃新的换图按键）, 0:空闲
 */
extern int service_film_is_busy(void);

/**
 * @brief 丢弃尚未处理的显示/渲染请求，并等当前正在上屏的那一帧画完
 *
 * DIRECT 层 app 退出时调用（见 app_entry_t.on_exit）：不丢弃的话，队列里
 * 排队的请求会在切页之后继续刷屏、把新页面覆盖掉；更要紧的是正在刷新的那一帧
 * 若不等待完成就切到 UI 层，两个任务会同时操作面板 SPI/BUSY，表现为卡死。
 * 函数同时会结束黑白快刷会话（把播放期间保持上电的面板断电），
 * 并等这一切完成才返回，上限 15s。
 */
extern void service_film_cancel_pending(void);

/**
 * @brief 获取文件帧数
 *
 * 读取文件头 偏移 0x0A（小端）。0/1 视为单帧，>1 为多帧动图。
 * 只读文件头，不会触发整份加载（可在 app 任务调用）。
 *
 * @param file_id 文件ID
 * @return uint32_t 帧数（≥1）；文件读不出来/格式未知/长度不足时返回 0
 */
extern uint32_t service_film_get_frame_count(uint32_t file_id);


#ifdef __cplusplus
}
#endif

#endif /* __SERVICE_FILM_H__ */

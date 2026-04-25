#ifndef __SERVICE_FILE_H__
#define __SERVICE_FILE_H__


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
#define FILE_TAG                    "file"
#define FILM_DIR                    "/sdcard/film"
#define FILM_FILE_EXT               ".film"

#define FILE_LOAD_STATE_NONE        (0)
#define FILE_LOAD_STATE_LOADING     (1)
#define FILE_LOAD_STATE_DONE        (2)

/*********************************************************************
* TYPEDEFS
*/
typedef struct {
    char filename[256];  // 文件名
    uint32_t file_size;  // 文件大小
} file_item_t;

typedef enum {
    MSG_FILE_LIST_REFRESH,    // 刷新文件列表
    MSG_FILE_LOAD,            // 加载文件
    MSG_FILE_LOAD_NEXT,       // 加载下一个文件
    MSG_SD_MOUNTED,           // SD卡挂载
    MSG_SD_UNMOUNTED,         // SD卡卸载
} file_msg_type_t;

typedef struct {
    file_msg_type_t ID;
    uint32_t file_id;  // 用于MSG_FILE_LOAD
} file_msg_t;

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
 * @brief 初始化文件服务
 *
 * 此函数用于初始化文件服务，创建任务和消息队列，
 * 初始化状态变量，检查SD卡状态。
 */
extern void service_file_init(void);

/**
 * @brief 刷新文件列表
 *
 * 此函数用于刷新文件列表，扫描SD卡中的.film文件。
 */
extern void service_file_refresh_list(void);

/**
 * @brief 加载指定文件
 *
 * 此函数用于加载指定ID的文件到PSRAM。
 *
 * @param file_id 文件ID
 * @return int 0:成功, -1:失败
 */
extern int service_file_load(uint32_t file_id);

/**
 * @brief 加载下一个文件
 *
 * 此函数用于加载文件列表中的下一个文件，实现循环加载。
 */
extern void service_file_load_next(void);

/**
 * @brief 获取文件数量
 *
 * 此函数用于获取文件列表中的文件数量。
 *
 * @return uint32_t 文件数量
 */
extern uint32_t service_file_get_count(void);

/**
 * @brief 获取文件名
 *
 * 此函数用于获取指定ID的文件名。
 *
 * @param file_id 文件ID
 * @return const char* 文件名
 */
extern const char* service_file_get_name(uint32_t file_id);

/**
 * @brief 获取PSRAM缓冲区指针
 *
 * 此函数用于获取加载文件的PSRAM缓冲区指针。
 *
 * @return uint8_t* PSRAM缓冲区指针
 */
extern uint8_t* service_file_get_buffer(void);

/**
 * @brief 获取缓冲区大小
 *
 * 此函数用于获取PSRAM缓冲区的大小。
 *
 * @return uint32_t 缓冲区大小
 */
extern uint32_t service_file_get_buffer_size(void);

/**
 * @brief 获取当前加载的文件ID
 *
 * 此函数用于获取当前加载的文件ID。
 *
 * @return uint32_t 当前文件ID
 */
extern uint32_t service_file_get_current_id(void);

/**
 * @brief 获取文件加载状态
 *
 * 此函数用于获取当前文件加载状态。
 *
 * @return uint8_t 文件加载状态
 */
extern uint8_t service_file_get_load_complete(void);


#ifdef __cplusplus
}
#endif

#endif /* __SERVICE_FILE_H__ */

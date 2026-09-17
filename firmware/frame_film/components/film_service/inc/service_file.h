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
#define ANIM_DIR                    "/sdcard/animation"
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
    MSG_FILE_SAVE_START,      // 开始保存文件（当前工作目录 + 文件名）
    MSG_FILE_SAVE_START_TO,   // 开始保存文件（显式相对路径，不参与列表/事件）
    MSG_FILE_SAVE_DATA,       // 保存文件数据
    MSG_FILE_SAVE_STOP,       // 停止保存文件
} file_msg_type_t;

typedef struct {
    file_msg_type_t ID;
    uint32_t file_id;
    uint32_t file_size;
    uint32_t data_len;
    uint8_t *pdata;
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
 * @brief 设置当前文件目录
 *
 * 切换目录（如 /sdcard/film 或 /sdcard/animation）后清空文件列表并触发刷新。
 * 用于图片/动图不同目录的复用。
 *
 * @param dir 目标目录字符串（必须以 '\0' 结尾）
 */
extern void service_file_set_dir(const char *dir);

/**
 * @brief 查询当前目录的文件列表是否已刷新完成
 *
 * service_file_set_dir() 为异步刷新，切换目录后需等待列表就绪再读取数量/内容。
 *
 * @return 1 已就绪，0 刷新中
 */
extern uint8_t service_file_is_list_ready(void);

/**
 * @brief 设置目录并同步等待列表就绪
 *
 * 供 app 层切换数据源时使用：内部调用 service_file_set_dir() 后轮询等待
 * 列表刷新完成（超时上限 FILE_DIR_READY_TIMEOUT_MS），返回刷新后的文件数量。
 * 目录未变化时不触发刷新，直接返回当前数量。
 *
 * @param dir 目标目录（NULL 时按当前目录处理）
 * @return uint32_t 列表就绪后的文件数量
 */
extern uint32_t service_file_set_dir_sync(const char *dir);

/**
 * @brief 获取当前文件目录
 *
 * @return const char* 当前目录字符串指针（内部缓冲，只读）
 */
extern const char *service_file_get_dir(void);

/**
 * @brief 刷新文件列表
 *
 * 此函数用于刷新文件列表，扫描SD卡中的.film文件。
 */
extern void service_file_refresh_list(void);

/**
 * @brief 保存文件数据
 *
 * 此函数用于通过BLE接收文件数据并保存到SD卡。
 *
 * @param pfilename 文件名
 * @param file_size 文件大小
 * @param pdata 数据指针
 * @param data_len 数据长度
 * @return int 0:成功, -1:失败
 */
extern int service_file_save_data(const char *pfilename, uint32_t file_size, uint8_t *pdata, uint32_t data_len);

/**
 * @brief 开始保存文件
 *
 * 此函数用于开始BLE文件传输，初始化文件保存。
 *
 * @param pfilename 文件名
 * @param file_size 文件大小
 * @return int 0:成功, -1:失败
 */
extern int service_file_save_start(const char *pfilename, uint32_t file_size);

/**
 * @brief 开始保存文件到显式相对路径
 *
 * 与 service_file_save_start() 的区别：路径相对 /sdcard 指定（如
 * "app/image/cover.film"），保存过程中自动逐级创建父目录，保存完成后
 * 不参与图片/动图列表刷新、不做 relocate、不上浮 SYS_EVT_FILE_SAVED。
 * 用于写入 app 封面等与文件列表无关的资源。
 *
 * @param rel_path  相对 /sdcard 的路径，不允许以 '/' 开头或包含 ".."
 * @param file_size 文件大小
 * @return int 0:成功, -1:失败
 */
extern int service_file_save_start_to(const char *rel_path, uint32_t file_size);

/**
 * @brief 停止保存文件
 *
 * 此函数用于完成BLE文件传输，关闭文件句柄。
 */
extern void service_file_save_stop(uint8_t auto_load);

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
 * @brief 安全获取文件名
 *
 * 此函数在锁内校验文件列表有效性后，将指定ID的文件名拷贝到调用方缓冲区，
 * 避免跨任务并发刷新文件列表时返回悬空指针。
 *
 * @param file_id  文件ID
 * @param out      输出缓冲区
 * @param out_size 输出缓冲区大小
 * @return int 0:成功, -1:失败（文件ID无效或列表刷新中）
 */
extern int service_file_get_filename_safe(uint32_t file_id, char *out, uint32_t out_size);

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

/**
 * @brief 获取文件大小
 *
 * 此函数用于获取指定ID的文件大小。
 *
 * @param file_id 文件ID
 * @return uint32_t 文件大小
 */
extern uint32_t service_file_get_size(uint32_t file_id);

/**
 * @brief 删除文件
 *
 * 此函数用于删除指定ID的文件。
 *
 * @param file_id 文件ID
 * @return int 0:成功, -1:失败
 */
extern int service_file_delete(uint32_t file_id);


#ifdef __cplusplus
}
#endif

#endif /* __SERVICE_FILE_H__ */

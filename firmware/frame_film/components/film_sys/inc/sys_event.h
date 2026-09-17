#ifndef __SYS_EVENT_H__
#define __SYS_EVENT_H__


/*********************************************************************
 * INCLUDES
 */
#include <stdint.h>

/*********************************************************************
 * CPPMIX
 */
#ifdef __cplusplus
extern "C" {
#endif

/*********************************************************************
 * MACROS
 */
#define SYS_EVENT_MAX_SUBSCRIBERS   (8)     // 订阅槽上限（固定表，免动态内存）
#define SYS_EVENT_PAYLOAD_MAX       (16)    // 内联负载上限，值语义 memcpy

/*********************************************************************
* TYPEDEFS
*/
/**
 * @brief 全局事件 ID（按模块分段，值一旦定义不再变更）
 *        0x01xx 文件/SD  0x02xx WiFi  0x03xx BLE  0x04xx OTA
 */
typedef enum {
    SYS_EVT_NONE         = 0x0000,

    SYS_EVT_FILE_SAVED   = 0x0101,  // film 落盘完成（BLE/WiFi/TF 通用）
    SYS_EVT_FILE_LIST    = 0x0102,  // 文件列表刷新完成，payload: u32 count
    SYS_EVT_SD_MOUNT     = 0x0103,  // TF 卡挂载
    SYS_EVT_SD_UNMOUNT   = 0x0104,  // TF 卡卸载

    SYS_EVT_WIFI_STATE   = 0x0201,  // WiFi 连接状态变化，payload: u8 state
    SYS_EVT_WIFI_DL_DONE = 0x0202,  // WiFi 下载完成

    SYS_EVT_BLE_CONN     = 0x0301,  // BLE 连接建立
    SYS_EVT_BLE_DISCONN  = 0x0302,  // BLE 断开
    SYS_EVT_BLE_APP_CMD  = 0x0303,  // BLE app 控制命令上浮，payload: u8 ch

    SYS_EVT_OTA_PROGRESS = 0x0401,  // OTA 进度，payload: u8 percent

    SYS_EVT_ANY          = 0xFFFF,  // 订阅通配：接收全部事件（仅用于 subscribe）
} sys_event_id_t;

/**
 * @brief 事件对象：固定大小、值语义，投递后与发布方栈无关
 */
typedef struct {
    uint16_t id;                            // sys_event_id_t
    uint16_t len;                           // payload 有效字节数
    uint8_t  from_isr;                      // 1=ISR 上下文发布
    uint8_t  payload[SYS_EVENT_PAYLOAD_MAX];// 内联负载
} sys_event_t;

/**
 * @brief 订阅回调：在发布方上下文同步执行，必须短小非阻塞
 *        禁止在回调内阻塞等待 / 直接刷屏等耗时操作
 */
typedef void (*sys_event_cb_t)(const sys_event_t *e, void *ctx);

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
extern int  sys_event_init(void);
extern int  sys_event_subscribe(sys_event_id_t id, sys_event_cb_t cb, void *ctx);
extern void sys_event_unsubscribe(int handle);
extern int  sys_event_publish(sys_event_id_t id, const void *payload, uint16_t len);
extern int  sys_event_publish_isr(sys_event_id_t id, const void *payload, uint16_t len);


#ifdef __cplusplus
}
#endif

#endif /* __SYS_EVENT_H__ */

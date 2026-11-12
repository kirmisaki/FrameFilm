#ifndef __APP_ANIMATION_H__
#define __APP_ANIMATION_H__

/*********************************************************************
 * INCLUDES
 */
#include "app_interface.h"

/*********************************************************************
 * CPPMIX
 */
#ifdef __cplusplus
extern "C" {
#endif

/*********************************************************************
 * MACROS
 */
/* ---- 参数通道契约（payload = TLV 列表，多字节大端）----
 * TAG / 取值范围都放在 app 自己的头文件里：BLE 参数通道（0x49 / 0x4A）与
 * 设置页（app_manager_param_get / set）共用同一套语义，只此一份。 */

/* 每帧间隔（毫秒，即"播放速度"）：下限受 on_tick 周期（100ms）约束 */
#define APP_ANIM_FRAME_MS_MIN   (100)
#define APP_ANIM_FRAME_MS_MAX   (2000)

/* 播放模式 */
#define APP_ANIM_PLAY_SINGLE    (0)     // 单 film 循环：只播当前文件，播完等待后重播
#define APP_ANIM_PLAY_SEQ       (1)     // film 列表循环：依次播每个文件，到末尾回绕

/* TAG */
#define APP_ANIM_TAG_PLAY_MODE  (0x01)  // 1B：0=单 film 循环 1=列表循环
#define APP_ANIM_TAG_FRAME_MS   (0x02)  // 2B：100~2000 毫秒
#define APP_ANIM_TAG_LOOP_SEC   (0x03)  // 2B：0~600 秒
#define APP_ANIM_TAG_FILE_ID    (0x04)  // 4B：当前文件下标

/*********************************************************************
 * GLOBAL VARIABLES
 */

/**
 * @brief 动图 app 接口实例（独立目录 /sdcard/animation 的多帧循环播放）
 */
extern const app_entry_t g_app_animation_entry;

/**
 * @brief 当前已在动图 app 内：按文件名切换并立即播放
 *
 * @return 0 成功；1 文件不存在/不可播放
 */
uint8_t app_animation_open_name(const char *name);

/**
 * @brief 登记一次性目标文件：下次进入动图 app 时按名打开
 *
 * 用于 BLE 远程播放选中：app_manager 先登记名字再切 app，
 * 由 on_enter 在列表就绪后解析，避免在外部临时切换文件目录。
 *
 * @param name 文件名；NULL 清除登记
 */
void app_animation_request_open(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* __APP_ANIMATION_H__ */

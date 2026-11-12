#ifndef __APP_SCREENSHOT_H__
#define __APP_SCREENSHOT_H__

/*********************************************************************
 * CPPMIX
 */
#ifdef __cplusplus
extern "C" {
#endif

/*********************************************************************
 * GLOBAL FUNCTIONS
 */

/**
 * @brief 截取当前画面并保存为单帧 MonoFast .film
 *
 * 只支持 UI 层页面（菜单 / 设置 / 时钟 / 通行证 / 卡包 / 开机页）：
 * 从 film_ui 的 mono 帧缓冲复制，写入 /sdcard/screenshot/shot_*.film。
 * 直绘层（图片/模板/动图）没有 UI 缓冲，会直接失败并打日志。
 * 调用前由 app_manager 检查 app_screenshot_enabled()（系统设置开关，默认关）。
 *
 * @return 0 成功；-1 SD 未挂载；-2 内存不足；-3 当前不是可截图的 UI 页面；
 *         -4 写文件失败
 */
int app_screenshot_capture(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_SCREENSHOT_H__ */

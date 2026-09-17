#ifndef __APP_INIT_H__
#define __APP_INIT_H__

/*********************************************************************
 * CPPMIX
 */
#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 统一 app 层入口
 *
 * 在 film_sys_init() 中、film_service_init() 之后调用。职责：
 * 1. 初始化 app 调度器（app_task + 事件队列）
 * 2. 注册各 app 到 app_manager
 * 3. 注册输入回调，统一转发到 app_manager 路由
 * 4. 首次开机默认进入图片 app（APP_ID_IMAGE）
 */
void film_app_init(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_INIT_H__ */

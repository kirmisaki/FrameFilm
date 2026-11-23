#ifndef ARK_SIM_SYS_LOG_H
#define ARK_SIM_SYS_LOG_H
#include <stdio.h>
#define sys_logd(tag, fmt, ...) ((void)0)
#define sys_logi(tag, fmt, ...) fprintf(stderr, "[I] %s: " fmt "\n", tag, ##__VA_ARGS__)
#define sys_logw(tag, fmt, ...) fprintf(stderr, "[W] %s: " fmt "\n", tag, ##__VA_ARGS__)
#define sys_loge(tag, fmt, ...) fprintf(stderr, "[E] %s: " fmt "\n", tag, ##__VA_ARGS__)
#endif

#ifndef __SERVICE_PARAM_H__
#define __SERVICE_PARAM_H__


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


/*********************************************************************
* TYPEDEFS
*/
typedef struct
{
    uint32_t current_file_id;  // 当前显示的文件ID
    uint8_t load_complete;     // 加载完成标志
    uint8_t play_mode;         // FILM模式（0：手动，1：本地切换 2.网络拉取）
} ServiceFilm_Def_t;

typedef struct
{
    uint8_t sleep_mode;        // 休眠模式开关 0：关闭 1：开启
    uint8_t sleep_auto;        // 定时唤醒开关 0：关闭 1：开启
    uint16_t sleep_time;       // 定时唤醒时间（单位分钟 10min - 48h(48*60min)）
} ServiceSleep_Def_t;

typedef struct
{
    uint8_t wifi_enable;       // WiFi开关 0：关闭 1：开启
    char wifi_ssid[64];        // WiFi SSID（最大63字符）
    char wifi_password[64];    // WiFi 密码（最大63字符）
    char film_api_url[128];    // HTTP下载film文件的API地址
} ServiceNetwork_Def_t;


#pragma pack(4)
typedef struct
{
    uint8_t factory_flag;
    ServiceFilm_Def_t film;
    ServiceSleep_Def_t sleep;
    ServiceNetwork_Def_t network;
} ServiceParam_Def_t; /*服务参数*/
#pragma pack()

/*********************************************************************
 * CONSTANTS
 */


/*********************************************************************
 * LOCAL VARIABLES
 */


/*********************************************************************
 * GLOBAL VARIABLES
 */
extern ServiceParam_Def_t g_service_param;

/*********************************************************************
 * LOCAL FUNCTIONS
 */


/*********************************************************************
 * GLOBAL FUNCTIONS
 */
extern void service_param_init(void);
extern void service_param_save(void);
extern void service_param_reset(void);


#ifdef __cplusplus
}
#endif

#endif /* __SERVICE_PARAM_H__ */

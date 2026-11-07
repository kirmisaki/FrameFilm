#ifndef __SERVICE_WIFI_H__
#define __SERVICE_WIFI_H__


/*********************************************************************
 * INCLUDES
 */
#include <stdint.h>
#include <stdbool.h>

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
typedef enum {
    WIFI_DOWNLOAD_IDLE = 0,
    WIFI_DOWNLOAD_DOWNLOADING,
    WIFI_DOWNLOAD_DONE,
    WIFI_DOWNLOAD_ERROR
} wifi_download_state_t;

typedef enum {
    WIFI_DIRECT_IDLE = 0,
    WIFI_DIRECT_CONNECTING = 1,
    WIFI_DIRECT_DOWNLOADING = 2,
    WIFI_DIRECT_RESTORING = 3,
    WIFI_DIRECT_DONE = 4,
    WIFI_DIRECT_ERROR = 5,
    WIFI_DIRECT_CANCELLED = 6,
    WIFI_DIRECT_READY = 7,
    WIFI_DIRECT_APPLYING = 8
} wifi_direct_state_t;

typedef struct {
    uint8_t state;
    uint8_t progress;
    uint8_t error;
    uint32_t received;
    uint32_t total;
} wifi_direct_status_t;

#define WIFI_DIRECT_ERR_NONE       0
#define WIFI_DIRECT_ERR_CONNECT    1
#define WIFI_DIRECT_ERR_HTTP       2
#define WIFI_DIRECT_ERR_SAVE       3
#define WIFI_DIRECT_ERR_CANCELLED  4
#define WIFI_DIRECT_ERR_RESTORE    5
#define WIFI_DIRECT_ERR_RESOURCE   6
#define WIFI_DIRECT_ERR_OTA 7
#define WIFI_DIRECT_ERR_INTEGRITY 8
#define WIFI_DIRECT_ERR_READY_TIMEOUT 9


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
extern void service_wifi_init(void);
extern void service_wifi_deinit(void);
extern void service_wifi_connect(void);
extern void service_wifi_disconnect(void);
extern uint8_t service_wifi_get_connect_status(void);
extern void service_wifi_clear_config(void);

extern void service_wifi_download_start(void);
extern void service_wifi_download_url(const char *url);
extern uint8_t service_wifi_download_get_progress(void);
extern wifi_download_state_t service_wifi_download_get_state(void);

extern void service_wifi_heartbeat_start(void);
#ifdef ARK_AP_FILE_BENCH
extern bool service_wifi_ap_file_bench_prepare(void);
extern void service_wifi_ap_file_bench_start(void);
#endif

/* Reserve a RAM-only AP session, then submit the existing direct-start payload. */
extern uint8_t service_wifi_softap_prepare(char ssid[33], char password[64]);
extern bool service_wifi_softap_waiting(void);

/* Temporary RAM-only WiFi session. No network parameters are persisted. */
extern uint8_t service_wifi_direct_start(const char *ssid, const char *password, const char *url);
extern uint8_t service_wifi_direct_file_start(const char *ssid, const char *password, const char *url, const char *path);
extern uint8_t service_wifi_direct_ota_start(const char *ssid, const char *password, const char *url, uint32_t size, const uint8_t sha[32]);
extern uint8_t service_wifi_direct_ota_apply(void);
extern void service_wifi_direct_cancel(void);
extern bool service_wifi_direct_busy(void);
extern void service_wifi_direct_get_status(wifi_direct_status_t *out);


#ifdef __cplusplus
}
#endif

#endif /* __SERVICE_WIFI_H__ */

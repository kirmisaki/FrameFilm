/* Startup scheduler collaborators; production lifecycle/event functions are injected by run.py. */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#define APP_SLEEP_STOP_CMD 0xfffffffeu
#define APP_QUEUE_LENGTH 16
#define APP_QUEUE_ITEM_SIZE 64
#define APP_TICK_MS 100
#define APP_TASK_NAME "host"
#define APP_TASK_STACK 4096
#define APP_TASK_PRIO 5
#define APP_MANAGER_TAG "host"
#define SYS_APP_SWITCH_MODE 2
#define SYS_EVT_ANY 0xffff
#define SYS_EVT_BLE_APP_CMD 1
#define BLE_FILM_TRANS_CH_CTRL_APP_SWITCH 0x4b
#define BLE_FILM_TRANS_CH_CTRL_KEY_INJECT 0x4e
#define APP_UI_REQ_BOOT_DONE 4
#define INPUT_PRESS_SHORT 1
#define UI_CALIB_FRAME 0
#define pdTRUE 1
#define pdPASS 1
#define pdMS_TO_TICKS(x) (x)
#define sys_loge(...) ((void)0)
#define sys_logw(...) ((void)0)
#define sys_logi(...) ((void)0)
enum { APP_ID_IMAGE=0, APP_ID_MENU=5, APP_ID_MAX=7, APP_LAYER_UI=1 };
enum {APP_EVT_INPUT,APP_EVT_TIMER,APP_EVT_SWITCH,APP_EVT_SYS,APP_EVT_BOOT,APP_EVT_UI_MSG};
enum {APP_INPUT_CONSUMED,APP_INPUT_PASS};
typedef int app_id_t; typedef int input_press_type_t;
typedef struct {int type,input;uint32_t cmd;void *data;uint8_t payload[64],len;} app_event_t;
typedef struct {int layer,tick_ms; void (*on_enter)(void);void (*on_exit)(void);void (*on_tick)(void);void (*on_event)(const app_event_t*);} app_entry_t;
typedef struct {int dummy;} app_ui_ops_t;
static const app_entry_t *m_app_registry[7];
static int m_app_running,m_current_app,m_boot_page,m_sleep_page,m_boot_ui_shown,m_boot_next_app,m_boot_evt_pending;
static int m_menu_sel,m_last_guest_app,m_tick_acc_ms,m_switch_mode;
static atomic_bool m_sleep_stopped,m_sleep_stop_sent;
static void *m_app_queue_hdl,*m_app_timer,*m_app_task_hdl;
static int enters,events,switches,ui_pages;
static app_event_t queued_switch;
static void app_handle_event(const app_event_t*);
static void app_manager_boot_end(void);
static int app_state_save(void){return 0;}
static int ui_core_stop_for_sleep(void){return 0;}
static int app_do_switch_target(int id,const char *f){(void)id;(void)f;return 0;}
static void app_do_switch(int id){switches++;m_current_app=id;m_boot_page=0;m_app_running=1;}
static void service_ble_send_resp(int a,void*b,int c){(void)a;(void)b;(void)c;}
static int app_param_ch_route(int a,const void*b,int c){(void)a;(void)b;(void)c;return 0;}
static int app_manager_process_input(int a){(void)a;return APP_INPUT_PASS;}
static int app_is_ui_layer(int id){return id==APP_ID_MENU;}
static void ui_core_post_key(int a){(void)a;}
static bool app_wants_sys_event(const app_entry_t *a,int id){(void)id;return a!=NULL;}
static void app_input_dispatch(int a){(void)a;}
static int ble_key_to_press(int a){return a;}
static void app_manager_boot_mark(void){m_boot_evt_pending=1;}
static void ui_core_page_enter(int id,const app_ui_ops_t *ops){(void)id;(void)ops;assert(m_boot_page && m_boot_ui_shown);ui_pages++;}
static void app_manager_switch(int id){queued_switch=(app_event_t){.type=APP_EVT_SWITCH,.data=(void*)(uintptr_t)id};}
static void *xQueueCreate(int a,int b){(void)a;(void)b;return (void*)1;}
static int sys_event_subscribe(int a,void*b,void*c){(void)a;(void)b;(void)c;assert(m_boot_page);return 1;}
static void *app_manager_on_sys_event;
static void app_timer_callback(void){}
static void app_task_handle(void){}
static void *xTimerCreate(const char*a,int b,int c,void*d,void(*e)(void)){(void)a;(void)b;(void)c;(void)d;(void)e;return (void*)1;}
static void xTimerStart(void*a,int b){(void)a;(void)b;assert(m_boot_page);}
static int xTaskCreate(void(*a)(void),const char*b,int c,void*d,int e,void**f){(void)a;(void)b;(void)c;(void)d;(void)e;*f=(void*)1;assert(m_boot_page);return 1;}
static int app_switch_effective_mode(void){return 2;}
static int app_render_has_app_menu(void){return 1;}
static void image_enter(void){enters++;}
static void image_event(const app_event_t*e){(void)e;events++;}

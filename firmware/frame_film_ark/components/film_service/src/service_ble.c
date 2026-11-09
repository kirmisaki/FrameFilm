/*********************************************************************
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (c) 2026 kiritro
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * 
 * FileName : /film_service/src/service_ble.c
 * Author: Kiritro  Version: v0.1  Date: 2026/4/5
 * Description: ble gatt服务
 * ChangeLog: Change Notes
 *
 *********************************************************************/

/*********************************************************************
 * INCLUDES
 */
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include <errno.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_system.h"
#include "esp_app_desc.h"
#include "esp_mac.h"

#include "sys_cfg.h"
#include "sys_log.h"
#include "sys_event.h"
#include "sys_hw.h"

#include "service_ble_gatts.h"
#include "service_ble.h"
#include "service_file.h"
#include "service_film.h"
#include "service_ota.h"
#include "service_param.h"
#include "service_time.h"
#include "service_wifi.h"
#include "service_log.h"
#include "freertos/semphr.h"
#include "service_monitor.h"

#include "hal_api.h"

/*********************************************************************
 * MACROS
 */
#define BLE_RESP_DATA_MAX          (192)  /* Includes read-only catalog filename responses. */
#define BLE_PROFILE_PATH           "/sdcard/app/pass/profile.json"
#define BLE_PROFILE_FILE_MAX       (350000L)
#define BLE_PROFILE_CHUNK_MAX      (128u)

/* 设备名固定前缀（机型识别用，用户只能改后缀） */
#define BLE_NAME_PREFIX            SYS_DEVICE_NAME "-"
#define BLE_NAME_PREFIX_LEN        (sizeof(BLE_NAME_PREFIX) - 1)

/* 前缀 + 后缀必须正好等于广播名上限（不含结尾 NUL），改任一处都要同步 */
_Static_assert(BLE_NAME_PREFIX_LEN + SERVICE_BLE_NAME_SUFFIX_MAX == BLE_DEVICE_NAME_MAX_BYTES,
               "BLE name prefix/suffix size mismatch");


/*********************************************************************
 * TYPEDEFS
 */
#define BEL_SERVICE_TAG            "ble_service"

#define BLE_MSG_QUEUE_LENGTH       50
#define BLE_MSG_QUEUE_ITEM_SIZE    sizeof( ble_msg_t )

#define BLE_FILM_TRANS_IDLE       0
#define BLE_FILM_TRANS_STARTED    1
#define BLE_FILM_TRANS_RECV_NAME  2
#define BLE_FILM_TRANS_RECV_LEN   3
#define BLE_FILM_TRANS_RECV_DATA  4
#define BLE_FILM_TRANS_STOPPED    5

#define BLE_OTA_TRANS_IDLE        0
#define BLE_OTA_TRANS_STARTED     1
#define BLE_OTA_TRANS_RECV_LEN    2
#define BLE_OTA_TRANS_RECV_DATA   3
#define BLE_OTA_TRANS_STOPPED     4


/*********************************************************************
 * CONSTANTS
 */


/*********************************************************************
 * LOCAL VARIABLES
 */
static TaskHandle_t m_ble_task_hdl = NULL;
static uint8_t m_ble_up = 0;   // 协议栈当前是否已拉起（运行期开关用）
static QueueHandle_t m_ble_msg_hdl = NULL;
static uint8_t m_film_trans_state = BLE_FILM_TRANS_IDLE;
static uint8_t m_film_trans_filename[256];
static uint32_t m_film_trans_file_size = 0;
static uint32_t m_film_trans_received = 0;
static volatile uint8_t m_ble_link_epoch = 0;
static volatile uint8_t m_ble_abort_pending = 0;
static atomic_bool m_ble_sleep_stop_sent = false;
static atomic_bool m_ble_sleep_done = false;
static atomic_bool m_wifi_ble_paused = false;
static SemaphoreHandle_t m_wifi_ble_mutex;
static uint8_t m_ota_trans_state = BLE_OTA_TRANS_IDLE;
static uint32_t m_ota_trans_file_size = 0;
static uint32_t m_ota_trans_received = 0;
static service_ble_app_id_get_cb_t m_app_id_get_cb = NULL;
static char m_ble_device_name[BLE_DEVICE_NAME_MAX_BYTES + 1];


/*********************************************************************
 * GLOBAL VARIABLES
 */


/*********************************************************************
 * LOCAL FUNCTIONS
 */
static void ble_task_handle(void *pvParameters);
static uint8_t ble_checksum(uint8_t arr[], int len);
static void ble_cmd_process(ble_cmd_t *cmd);
static int ble_file_reset(void);
static bool ble_name_suffix_valid(const uint8_t *bytes, size_t len);
static const char *ble_name_suffix_or_default(void);

void service_ble_transfer_disconnected(void)
{
    /* 终端日志绑定当前连接：断开即关闭，卸载日志输出钩子，避免无人订阅时白跑 */
    service_log_stream_set(0);
    if(!service_ble_wifi_paused()) service_wifi_direct_cancel();
    m_ble_link_epoch++;
    m_ble_abort_pending = 1;
    if(m_ble_msg_hdl)
    {
        ble_msg_t msg = {.ID = MSG_BLE_FILE_ABORT};
        (void)xQueueSend(m_ble_msg_hdl, &msg, 0);
    }
}

static int ble_file_reset(void)
{
    if(m_film_trans_state != BLE_FILM_TRANS_IDLE && m_film_trans_state != BLE_FILM_TRANS_STOPPED)
    {
        /* START/NAME 尚未打开文件；LEN/DATA 才有待关闭的暂存文件。 */
        if((m_film_trans_state == BLE_FILM_TRANS_RECV_LEN ||
            m_film_trans_state == BLE_FILM_TRANS_RECV_DATA) &&
           service_file_save_abort(FILE_SAVE_BLE) != 0) return -1;
    }
    m_film_trans_state = BLE_FILM_TRANS_IDLE;
    m_film_trans_received = 0;
    m_film_trans_file_size = 0;
    m_film_trans_filename[0] = '\0';
    return 0;
}

/* ---------------- 蓝牙设备名（0x54 / 0x55） ----------------
 * 完整名 = 编译期前缀 BLE_NAME_PREFIX + 可配置后缀；后缀持久化在
 * g_service_param.ble.ble_name（无独立 NVS key）。仅 gatts 在蓝牙初始化/
 * 重初始化时取当前名，故改名要重启或关开蓝牙才会反映到广播上。 */

/* 后缀必须是 1..16 字节合法 UTF-8 + NUL：名字会进广播包与 GATT Device Name，
 * 且任何 BLE 中心设备都能写 0x55，不能信客户端。 */
static bool ble_name_suffix_valid(const uint8_t *bytes, size_t len)
{
    if(bytes == NULL || len <= 1 || len > SERVICE_BLE_NAME_SUFFIX_MAX + 1 || bytes[len - 1] != 0)
        return false;

    /* Validate UTF-8 scalar values and reject C0/C1 control characters. */
    for(size_t i = 0; i + 1 < len;)
    {
        uint32_t cp;
        uint8_t first = bytes[i++];
        size_t more;
        if(first < 0x80) { cp = first; more = 0; }
        else if(first >= 0xC2 && first <= 0xDF) { cp = first & 0x1F; more = 1; }
        else if(first >= 0xE0 && first <= 0xEF) { cp = first & 0x0F; more = 2; }
        else if(first >= 0xF0 && first <= 0xF4) { cp = first & 0x07; more = 3; }
        else return false;

        if(i + more > len - 1) return false;
        for(size_t j = 0; j < more; j++)
        {
            if((bytes[i] & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (bytes[i++] & 0x3F);
        }
        if((more == 1 && cp < 0x80) || (more == 2 && cp < 0x800) ||
           (more == 3 && cp < 0x10000) || cp > 0x10FFFF ||
           (cp >= 0xD800 && cp <= 0xDFFF) || cp < 0x20 ||
           (cp >= 0x7F && cp <= 0x9F)) return false;
    }
    return true;
}

/* 返回有效后缀；未配置（空串）或存值非法时用蓝牙 MAC 派生并写回参数，作为默认名。 */
static const char *ble_name_suffix_or_default(void)
{
    char *suffix = g_service_param.ble.ble_name;
    size_t stored = strnlen(suffix, sizeof(g_service_param.ble.ble_name)) + 1;
    if(ble_name_suffix_valid((const uint8_t *)suffix, stored))
    {
        return suffix;
    }

    if(suffix[0] != '\0')
    {
        sys_logw(BEL_SERVICE_TAG, "stored name invalid, fallback to MAC");
    }

    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_BT);
    snprintf(suffix, sizeof(g_service_param.ble.ble_name), "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return suffix;
}

void service_ble_device_name_init(void)
{
    snprintf(m_ble_device_name, sizeof(m_ble_device_name), "%s%s",
             BLE_NAME_PREFIX, ble_name_suffix_or_default());
}

const char *service_ble_device_name_get(void)
{
    return m_ble_device_name;
}

uint8_t service_ble_device_name_set(const uint8_t *suffix, size_t payload_len)
{
    if(!ble_name_suffix_valid(suffix, payload_len)) return 1;

    memcpy(g_service_param.ble.ble_name, suffix, payload_len);
    g_service_param.ble.ble_name[payload_len - 1] = '\0'; /* 校验已保证末字节为 NUL */
    service_param_save();

    snprintf(m_ble_device_name, sizeof(m_ble_device_name), "%s%s",
             BLE_NAME_PREFIX, g_service_param.ble.ble_name);
    return 0;
}

bool service_ble_prepare_sleep(void)
{
    if(m_ble_task_hdl == NULL) return true; /* BLE 从未启用，无文件写入者。 */
    if(atomic_load(&m_ble_sleep_done)) return true;
    if(m_ble_msg_hdl == NULL) return false;

    if(!atomic_exchange(&m_ble_sleep_stop_sent, true))
    {
        /* 排到队首：正在执行的文件调用先完成，旧队列命令在入睡门闸下丢弃。 */
        ble_msg_t msg = {.ID = MSG_BLE_FILE_ABORT, .subID = 1};
        if(xQueueSendToFront(m_ble_msg_hdl, &msg, 0) != pdPASS)
        {
            atomic_store(&m_ble_sleep_stop_sent, false);
            return false;
        }
    }
    for(unsigned i = 0; i < 500 && atomic_load(&m_ble_sleep_stop_sent) &&
        !atomic_load(&m_ble_sleep_done); i++)
        vTaskDelay(pdMS_TO_TICKS(20));
    return atomic_load(&m_ble_sleep_done);
}

/*********************************************************************
 * GLOBAL FUNCTIONS
 */



/**
 * [service_ble_init 初始化ble服务]
 *
 * 开关逻辑与 WiFi 一致：**不开启就不初始化**（不拉协议栈、不占射频与栈空间）。
 * 运行期开关走 service_ble_apply_enable()。
 */
void service_ble_init(void)
{
    if(m_ble_up)
    {
        sys_logi(BEL_SERVICE_TAG, "BLE already initialized");
        return;
    }

    if(!g_service_param.ble.ble_enable)
    {
        sys_logi(BEL_SERVICE_TAG, "BLE disabled, skip init");
        return;
    }

    if(!m_wifi_ble_mutex) m_wifi_ble_mutex = xSemaphoreCreateMutex();
    // 初始化ble服务
    service_ble_gatt_server_init();
    service_ble_gatts_cmd_register_cb(service_ble_msg_gatts_cmd_send);

    if(m_ble_task_hdl == NULL)
    {
        if ( pdPASS != xTaskCreate( ble_task_handle, SYS_OS_NAME_BLE_TASK, SYS_OS_SIZE_BLE_TASK, NULL, SYS_OS_PRI_BLE_TASK, &m_ble_task_hdl ))
        {
            sys_loge(BEL_SERVICE_TAG, "ble task create error!");
        }
    }

    m_ble_up = 1;
}

/**
 * [service_ble_apply_enable 蓝牙开关：运行期起停]
 *
 * 参考 WiFi 的开关语义（置参数 -> 起停协议栈），由设置页与心跳下发调用。
 *
 * 关：停广播 + 注销服务（uninit 不关 bluedroid，栈仍活着，只是不再对外可见）。
 * 开：栈若已拉起过，只需 reinit（重新注册并恢复广播）；从未拉起过则整栈初始化。
 * 注意本函数**不落盘**：调用方负责 service_param_save()。
 */
void service_ble_apply_enable(uint8_t on)
{
    g_service_param.ble.ble_enable = on ? 1 : 0;
    if(service_ble_wifi_paused()) {
        /* The transfer owner restores the desired switch, without racing the host. */
        wifi_direct_status_t status;
        service_wifi_direct_get_status(&status);
        if(on && (!service_wifi_direct_busy() || status.state == WIFI_DIRECT_READY))
            (void)service_ble_wifi_resume();
        return;
    }

    if(on)
    {
        if(m_ble_up)
        {
            return;
        }
        if(m_ble_task_hdl != NULL)
        {
            service_ble_gatt_server_reinit();   // 曾在运行中被关：栈还活着，重新注册 + 恢复广播
            m_ble_up = 1;
        }
        else
        {
            service_ble_init();                 // 从未拉起过（开机时就是关的）
        }
    }
    else
    {
        if(!m_ble_up)
        {
            return;
        }
        service_ble_gatts_dev_disconnect();
        service_ble_transfer_disconnected();
        service_ble_gatt_server_uninit();
        m_ble_up = 0;
    }

    sys_logi(BEL_SERVICE_TAG, "BLE %s", on ? "ON" : "OFF");
}

bool service_ble_wifi_paused(void)
{
    return atomic_load(&m_wifi_ble_paused);
}

bool service_ble_wifi_pause(void)
{
    if(!m_wifi_ble_mutex || xSemaphoreTake(m_wifi_ble_mutex, pdMS_TO_TICKS(5000)) != pdTRUE)
        return false;
    if(atomic_exchange(&m_wifi_ble_paused, true)) {
        xSemaphoreGive(m_wifi_ble_mutex);
        return true;
    }
    if(m_ble_up) {
        m_ble_up = 0;
        service_ble_gatt_server_uninit();
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    int err = service_ble_gatt_wifi_pause();
    xSemaphoreGive(m_wifi_ble_mutex);
    sys_logi(BEL_SERVICE_TAG, "WiFi planned BLE pause result=%d", err);
    return err == ESP_OK;
}

bool service_ble_wifi_resume(void)
{
    if(!m_wifi_ble_mutex || xSemaphoreTake(m_wifi_ble_mutex, pdMS_TO_TICKS(5000)) != pdTRUE)
        return false;
    if(!atomic_load(&m_wifi_ble_paused) || !g_service_param.ble.ble_enable) {
        xSemaphoreGive(m_wifi_ble_mutex);
        return true;
    }
    int err = service_ble_gatt_wifi_resume();
    if(err == ESP_OK && g_service_param.ble.ble_enable) {
        m_ble_up = 1;
        atomic_store(&m_wifi_ble_paused, false);
    } else if(err == ESP_OK) {
        service_ble_gatt_server_uninit();
        err = service_ble_gatt_wifi_pause();
    }
    xSemaphoreGive(m_wifi_ble_mutex);
    sys_logi(BEL_SERVICE_TAG, "WiFi BLE restore result=%d", err);
    return err == ESP_OK;
}


/**
 * [service_ble_msg_send ble事件msg发送]
 * @param p_msg  [msg]
 * @param in_isr [is in interrupt]
 */
void service_ble_msg_send(void *p_msg, bool in_isr)
{
    /* The queue could not be created. */
    if(m_ble_msg_hdl != NULL)
    {
        if(in_isr == 0)
        {
            SYS_ERROR_CHECK((xQueueSend(m_ble_msg_hdl, p_msg, portMAX_DELAY) != pdPASS));
        }
        else /* Is In interrupt.*/
        {
            BaseType_t xHigherPriorityTaskWoken;
            /* No tasks have yet been unblocked. */
            xHigherPriorityTaskWoken = pdFALSE;

            /* Write the byte to the queue. xHigherPriorityTaskWoken will get set to
            pdTRUE if writing to the queue causes a task to leave the Blocked state,
            and the task leaving the Blocked state has a priority higher than the
            currently executing task (the task that was interrupted). */
            xQueueSendFromISR( m_ble_msg_hdl, p_msg, &xHigherPriorityTaskWoken );
            /* Now the buffer is empty, and the interrupt source has been cleared, a context
            switch should be performed if xHigherPriorityTaskWoken is equal to pdTRUE.
            NOTE: The syntax required to perform a context switch from an ISR varies from
            port to port, and from compiler to compiler. Check the web documentation and
            examples for the port being used to find the syntax required for your
            application. */
            portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
        }
    }
}

/**
 * [service_ble_msg_gatts_cmd_send 发送命令获取msg]
 * @param p_data [命令buf]
 * @param len    [长度]
 */
void service_ble_msg_gatts_cmd_send( uint8_t const *p_data, uint16_t len )
{
    /* The queue could not be created. */
    if(m_ble_msg_hdl != NULL)
    {
        ble_msg_t msg = {0};

        msg.ID = MSG_BLE_CH1_IN_CMD;
        msg.subID = m_ble_link_epoch;
        msg.len = len;
        msg.pdata = pvPortMalloc(len);
        if(msg.pdata == NULL)
        {
            sys_loge(BEL_SERVICE_TAG, "cmd msg malloc %u failed, drop", (unsigned)len);
            return;
        }
        memcpy(msg.pdata, p_data, len);

        /* 调用方为 GATTS 写事件回调（BLE 栈任务上下文），故用非阻塞 xQueueSend；
           队列满则回收内存并丢弃，避免阻塞 BLE 栈，也不会把 NULL 负载投递出去 */
        if(xQueueSend(m_ble_msg_hdl, &msg, 0) != pdPASS)
        {
            sys_logw(BEL_SERVICE_TAG, "ble cmd queue full, drop len=%u", (unsigned)len);
            vPortFree(msg.pdata);
        }
    }
}

/**
 * [service_ble_msg_gatts_data_send 发送数据msg]
 * @param p_data [命令buf]
 * @param len    [长度]
 * @param ch     [val MSG_BLE_CH1_OUT_DATA MSG_BLE_CH2_OUT_DATA MSG_BLE_CH3_OUT_DATA]
 */
void service_ble_msg_gatts_data_send( uint8_t const *p_data, uint16_t len, uint8_t ch)
{
    /* The queue could not be created. */
    if(m_ble_msg_hdl != NULL)
    {
        ble_msg_t msg = {0};

        msg.ID = ch;
        msg.len = len;
        msg.pdata = pvPortMalloc(len);
        if(msg.pdata == NULL)
        {
            sys_loge(BEL_SERVICE_TAG, "msg malloc %u failed, drop ch=0x%02X", (unsigned)len, ch);
            return;
        }
        memcpy(msg.pdata, p_data, len);

        /* 调用方均为任务上下文（ble 任务 / app 任务），故用非阻塞 xQueueSend；
           队列满则回收内存并丢弃，既不阻塞发布方，也不会把 NULL 负载投递出去 */
        if(xQueueSend(m_ble_msg_hdl, &msg, 0) != pdPASS)
        {
            sys_logw(BEL_SERVICE_TAG, "ble msg queue full, drop ch=0x%02X len=%u", ch, (unsigned)len);
            vPortFree(msg.pdata);
        }
    }
}

static void ble_task_handle(void *pvParameters)
{
    m_ble_msg_hdl = xQueueCreate( BLE_MSG_QUEUE_LENGTH, BLE_MSG_QUEUE_ITEM_SIZE );
    SYS_ERROR_CHECK(m_ble_msg_hdl == NULL);

    for(;;)
    {
        ble_msg_t msg;
        SYS_ERROR_CHECK( xQueueReceive( m_ble_msg_hdl, (void *const)&msg, portMAX_DELAY ) != pdPASS );

        if(m_ble_abort_pending)
        {
            m_ble_abort_pending = 0;
            ble_file_reset();
        }

        switch(msg.ID)
        {
        case MSG_BLE_CH1_IN_CMD :
        {
            if(msg.subID != m_ble_link_epoch)
            {
                vPortFree(msg.pdata);
                break;
            }
            /* 最小帧 = HEAD+CH+LEN+SUM，不足连 ch/len 都解不出来 */
            if(msg.len >= BLE_CMD_LEN_MIN && msg.pdata[0] == BLE_CMD_HEAD)
            {
                uint8_t sum = ble_checksum(msg.pdata, msg.len - 1);
                // sys_logi(BEL_SERVICE_TAG, "sum:0x%02x", sum);

                if(sum == msg.pdata[msg.len - 1])
                {
                    ble_cmd_t cmd = {0};

                    cmd.ch = msg.pdata[1];
                    cmd.len = msg.pdata[2];
                    if(cmd.len == msg.len - 4)
                    {
                        cmd.pdata = msg.pdata + 3;
                        if(cmd.pdata)
                        {
                            ble_cmd_process(&cmd);
                        }
                    }
                }
                vPortFree(msg.pdata);
            }
            break;
        }
        case MSG_BLE_CH1_OUT_DATA :
        {
            if( msg.len )
            {
                service_ble_send_notify_data(BLE_NOTIFY_SEND_CH1, msg.pdata, msg.len);
                vPortFree(msg.pdata);
            }
            break;
        }
        case MSG_BLE_CH2_OUT_DATA :
        {
            if( msg.len )
            {
                service_ble_send_notify_data(BLE_NOTIFY_SEND_CH2, msg.pdata, msg.len);
                vPortFree(msg.pdata);
            }
            break;
        }
        case MSG_BLE_CH3_OUT_DATA :
        {
            if( msg.len )
            {
                service_ble_send_notify_data(BLE_NOTIFY_SEND_CH3, msg.pdata, msg.len);
                vPortFree(msg.pdata);
            }
            break;
        }
        case MSG_BLE_GAP_DISCONNECT:
            service_ble_gatts_dev_disconnect();
            break;
        case MSG_BLE_FILE_ABORT:
            if(msg.subID == 1)
            {
                if(ble_file_reset() == 0) atomic_store(&m_ble_sleep_done, true);
                else atomic_store(&m_ble_sleep_stop_sent, false);
            }
            break; // 普通断连在队首统一执行 m_ble_abort_pending 撤销
        default :
        {
            if( msg.len )
            {
                vPortFree(msg.pdata);
            }
            break;
        }
        }
    }
}

static bool ble_cmd_conflicts_with_direct(const ble_cmd_t *cmd)
{
    uint8_t ch = cmd->ch;
    switch(ch)
    {
    case BLE_FILM_TRANS_CH_FILE_NAME:
    case BLE_FILM_TRANS_CH_FILE_LEN:
    case BLE_FILM_TRANS_CH_FILE_DATA:
    case BLE_FILM_TRANS_CH_FILE_STOP:
    case BLE_FILM_TRANS_CH_FILE_START:
    case BLE_FILM_TRANS_CH_DIRECT_FILE_START:
    case BLE_FILM_TRANS_CH_FILE_DELETE:
    case BLE_FILM_TRANS_CH_OTA_LEN:
    case BLE_FILM_TRANS_CH_OTA_DATA:
    case BLE_FILM_TRANS_CH_OTA_STOP:
    case BLE_FILM_TRANS_CH_OTA_START:
    case BLE_FILM_TRANS_CH_DEVICE_NAME_SET:
    case BLE_FILM_TRANS_CH_CTRL_APP_SWITCH:
    case BLE_FILM_TRANS_CH_CTRL_KEY_INJECT:
    case BLE_FILM_TRANS_CH_APP_IMAGE_PARAM:
    case BLE_FILM_TRANS_CH_APP_TEMPLATE_PARAM:
    case BLE_FILM_TRANS_CH_APP_ANIM_PARAM:
#if (SYS_NFC_ENABLE == 1)
    case BLE_FILM_TRANS_CH_NFC_CARD_BEGIN:
    case BLE_FILM_TRANS_CH_NFC_CARD_DATA:
    case BLE_FILM_TRANS_CH_NFC_CARD_COMMIT:
    case BLE_FILM_TRANS_CH_NFC_WALLET_CTRL:
    case BLE_FILM_TRANS_CH_NFC_COVER_BEGIN:
    case BLE_FILM_TRANS_CH_NFC_COVER_DATA:
    case BLE_FILM_TRANS_CH_NFC_COVER_COMMIT:
#endif
    case BLE_FILM_TRANS_CH_CTRL_TIME_SYNC:
    case BLE_FILM_TRANS_CH_CTRL_SLEEPONOFF:
    case BLE_FILM_TRANS_CH_CTRL_SLEEPMODE:
    case BLE_FILM_TRANS_CH_CTRL_SLEEPMODE_TIME:
    case BLE_FILM_TRANS_CH_CTRL_RESET:
    case BLE_FILM_TRANS_CH_CTRL_REBOOT:
    case BLE_FILM_TRANS_CH_CTRL_SDRESET:
    case BLE_FILM_TRANS_CH_CTRL_WIFI_ENABLE:
    case BLE_FILM_TRANS_CH_CTRL_WIFI_SSID:
    case BLE_FILM_TRANS_CH_CTRL_WIFI_PASSWORD:
    case BLE_FILM_TRANS_CH_CTRL_FILM_API_URL:
    case BLE_FILM_TRANS_CH_CTRL_WIFI_CONNECT:
    case BLE_FILM_TRANS_CH_CTRL_WIFI_DISCONNECT:
    case BLE_FILM_TRANS_CH_CTRL_WIFI_CLEAR:
    case BLE_FILM_TRANS_CH_CTRL_FILM_DOWNLOAD:
    case BLE_FILM_TRANS_CH_CTRL_FILM_HEARTBEAT_URL:
    case BLE_FILM_TRANS_CH_CTRL_FILM_HEARTBEAT_INTERVAL:
        return true;
    default:
        return false;
    }
}

static uint8_t ble_direct_start(const ble_cmd_t *cmd)
{
    if(service_monitor_sleep_pending()) return 1;
    if(cmd->len == 0 || cmd->len > BLE_DIRECT_START_DATA_MAX) return 2;
    if((m_film_trans_state > BLE_FILM_TRANS_IDLE && m_film_trans_state < BLE_FILM_TRANS_STOPPED) ||
       (m_ota_trans_state > BLE_OTA_TRANS_IDLE && m_ota_trans_state < BLE_OTA_TRANS_STOPPED)) return 1;

    bool ota = cmd->ch == BLE_FILM_TRANS_CH_DIRECT_OTA_START;
    bool file = cmd->ch == BLE_FILM_TRANS_CH_DIRECT_FILE_START;
    if(ota && cmd->len <= 36) return 2;
    uint32_t size = ota ? ((uint32_t)cmd->pdata[0] << 24) | ((uint32_t)cmd->pdata[1] << 16) |
                         ((uint32_t)cmd->pdata[2] << 8) | cmd->pdata[3] : 0;
    const uint8_t *cursor = cmd->pdata + (ota ? 36 : 0);
    size_t remaining = cmd->len - (ota ? 36 : 0);
    const char *fields[4];
    for(size_t i = 0; i < (file ? 4 : 3); i++)
    {
        const uint8_t *end = memchr(cursor, '\0', remaining);
        if(end == NULL) return 2;
        fields[i] = (const char *)cursor;
        size_t consumed = (size_t)(end - cursor) + 1;
        cursor += consumed;
        remaining -= consumed;
    }
    if(remaining != 0) return 2;
    return ota ? service_wifi_direct_ota_start(fields[0], fields[1], fields[2], size, &cmd->pdata[4]) :
           file ? service_wifi_direct_file_start(fields[0], fields[1], fields[2], fields[3]) :
                  service_wifi_direct_start(fields[0], fields[1], fields[2]);
}

/*
 * 固定资源的分块读取：每个请求独立开关文件，不保留跨请求句柄。
 * 响应 DATA = status(1) + offset(4 BE) + total(4 BE) + bytes(0..128)。
 */
static void ble_profile_read(const ble_cmd_t *cmd)
{
    uint8_t out[9 + BLE_PROFILE_CHUNK_MAX] = {0};
    uint32_t offset = 0;
    uint32_t total = 0;
    uint8_t count = 0;
    size_t actual = 0;
    FILE *fp = NULL;

    if(cmd->len != 5 || cmd->pdata == NULL)
    {
        out[0] = 4;  /* 参数错误 */
        goto reply;
    }
    offset = ((uint32_t)cmd->pdata[0] << 24) |
             ((uint32_t)cmd->pdata[1] << 16) |
             ((uint32_t)cmd->pdata[2] << 8) |
             (uint32_t)cmd->pdata[3];
    count = cmd->pdata[4];
    if(count == 0 || count > BLE_PROFILE_CHUNK_MAX)
    {
        out[0] = 4;
        goto reply;
    }
    if((m_film_trans_state != BLE_FILM_TRANS_IDLE &&
        m_film_trans_state != BLE_FILM_TRANS_STOPPED) ||
       (m_ota_trans_state != BLE_OTA_TRANS_IDLE &&
        m_ota_trans_state != BLE_OTA_TRANS_STOPPED) ||
       service_wifi_direct_busy())
    {
        out[0] = 3;  /* 文件写入/直传进行中 */
        goto reply;
    }

    fp = fopen(BLE_PROFILE_PATH, "rb");
    if(fp == NULL)
    {
        out[0] = errno == ENOENT ? 1 : 2;
        goto reply;
    }
    if(fseek(fp, 0, SEEK_END) != 0)
    {
        out[0] = 2;
        goto reply;
    }
    long size = ftell(fp);
    if(size < 0 || size > BLE_PROFILE_FILE_MAX)
    {
        out[0] = 2;
        goto reply;
    }
    total = (uint32_t)size;
    if(offset > total)
    {
        out[0] = 4;
        goto reply;
    }
    if(fseek(fp, (long)offset, SEEK_SET) != 0)
    {
        out[0] = 2;
        goto reply;
    }
    if(count > total - offset) count = (uint8_t)(total - offset);
    if(count > 0)
    {
        actual = fread(&out[9], 1, count, fp);
        if(actual != count)
        {
            actual = 0;
            out[0] = 2;
        }
    }

reply:
    if(fp != NULL) fclose(fp);
    out[1] = (uint8_t)(offset >> 24);
    out[2] = (uint8_t)(offset >> 16);
    out[3] = (uint8_t)(offset >> 8);
    out[4] = (uint8_t)offset;
    out[5] = (uint8_t)(total >> 24);
    out[6] = (uint8_t)(total >> 16);
    out[7] = (uint8_t)(total >> 8);
    out[8] = (uint8_t)total;
    service_ble_send_resp(BLE_FILM_TRANS_CH_PROFILE_READ, out, (uint8_t)(9 + actual));
}

static void ble_cmd_publish_app(const ble_cmd_t *cmd)
{
    /* payload: [0]=ch, [1..]=命令数据，值语义拷贝，app 侧在自身任务执行 */
    uint8_t evt[SYS_EVENT_PAYLOAD_MAX];
    uint8_t n = cmd->len;
    if(cmd->ch == BLE_FILM_TRANS_CH_CTRL_APP_SWITCH && n > SYS_EVENT_PAYLOAD_MAX - 1)
    {
        uint8_t response[2] = {cmd->pdata[0], 1};
        service_ble_send_resp(cmd->ch, response, sizeof(response));
        return;
    }
    if(n > SYS_EVENT_PAYLOAD_MAX - 1)
    {
        sys_logw(BEL_SERVICE_TAG, "app cmd 0x%02X len=%u exceeds event payload %u, truncate",
                 cmd->ch, (unsigned)cmd->len, (unsigned)(SYS_EVENT_PAYLOAD_MAX - 1));
        n = SYS_EVENT_PAYLOAD_MAX - 1;
    }
    evt[0] = cmd->ch;
    if(n > 0 && cmd->pdata != NULL)
    {
        memcpy(&evt[1], cmd->pdata, n);
    }
    sys_event_publish(SYS_EVT_BLE_APP_CMD, evt, (uint16_t)(n + 1));
}

static void ble_cmd_process(ble_cmd_t *cmd)
{
    if(cmd == NULL)
    {
        sys_logw(BEL_SERVICE_TAG, "cmd is NULL");
        return;
    }

    /* 入睡期间阻止新的写入，但手机仍须能查询/取消正在恢复的直传。 */
    if(service_monitor_sleep_pending() &&
       cmd->ch != BLE_FILM_TRANS_CH_DIRECT_STATUS &&
       cmd->ch != BLE_FILM_TRANS_CH_DIRECT_CANCEL)
    {
        return;
    }

    bool ap_start = service_wifi_softap_waiting() &&
                    (cmd->ch == BLE_FILM_TRANS_CH_DIRECT_START ||
                     cmd->ch == BLE_FILM_TRANS_CH_DIRECT_FILE_START ||
                     cmd->ch == BLE_FILM_TRANS_CH_DIRECT_OTA_START);
    if(service_wifi_direct_busy() && !ap_start && ble_cmd_conflicts_with_direct(cmd))
    {
        const uint8_t busy = 1;
        service_ble_send_resp(cmd->ch, &busy, 1);
        return;
    }

    switch(cmd->ch)
    {
        case BLE_FILM_TRANS_CH_DIRECT_AP_PREPARE:
        {
            /* 纯空请求；带 DATA 属于错误用法，直接忽略（不回包） */
            if(cmd->len != 0) break;
            uint8_t out[1 + 33 + 64 + 16] = {2};
            char ssid[33], password[64];
            bool legacy_busy = (m_film_trans_state > BLE_FILM_TRANS_IDLE && m_film_trans_state < BLE_FILM_TRANS_STOPPED) ||
                               (m_ota_trans_state > BLE_OTA_TRANS_IDLE && m_ota_trans_state < BLE_OTA_TRANS_STOPPED);
            out[0] = legacy_busy ? 1 : service_wifi_softap_prepare(ssid, password);
            size_t length = 1;
            if(out[0] == 0) {
                memcpy(out + length, ssid, strlen(ssid) + 1); length += strlen(ssid) + 1;
                memcpy(out + length, password, strlen(password) + 1); length += strlen(password) + 1;
                memcpy(out + length, "192.168.4.1", sizeof("192.168.4.1")); length += sizeof("192.168.4.1");
            }
            service_ble_send_resp(cmd->ch, out, length);
            break;
        }
        case BLE_FILM_TRANS_CH_CAPABILITIES_GET:
        {
            if(cmd->len != 0) break;
            uint8_t capabilities[5] = {1, 0, 0, 0, 0x5F};
#if defined(CONFIG_ARK_EPD_ABC_PREVIEW) || defined(CONFIG_ARK_EPD_ABC_DIAG)
            capabilities[4] |= 0x20;  /* Experimental ABC9 playback. */
#endif
#if (SYS_NFC_ENABLE == 1)
            capabilities[4] |= 0x80;
#endif
            service_ble_send_resp(cmd->ch, capabilities, sizeof(capabilities));
            break;
        }
        case BLE_FILM_TRANS_CH_STORAGE_CATALOG:
        {
            uint8_t response[192] = {3};
            uint8_t length = 1;
            if(cmd->len == 1 && cmd->pdata[0] == 0)
            {
                uint64_t total = 0, free_bytes = 0;
                uint32_t films = 0, animations = 0;
                response[0] = hal_sd_get_status() == SD_MOUNT ? 0 : 1;
                response[1] = 0;
                if(response[0] == 0)
                {
                    if(hal_sd_get_space(&total, &free_bytes) == 0) response[1] |= 1;
                    if(service_file_catalog(1, 0, &films, NULL, 0) == 0) response[1] |= 2;
                    if(service_file_catalog(2, 0, &animations, NULL, 0) == 0) response[1] |= 4;
                }
                for(unsigned i = 0; i < 8; ++i) {
                    response[2+i] = (uint8_t)(total >> (56-i*8));
                    response[10+i] = (uint8_t)(free_bytes >> (56-i*8));
                }
                for(unsigned i = 0; i < 4; ++i) {
                    response[18+i] = (uint8_t)(films >> (24-i*8));
                    response[22+i] = (uint8_t)(animations >> (24-i*8));
                }
                length = 26;
            }
            else if(cmd->len == 5 && (cmd->pdata[0] == 1 || cmd->pdata[0] == 2))
            {
                uint32_t index = ((uint32_t)cmd->pdata[1]<<24) | ((uint32_t)cmd->pdata[2]<<16) |
                                 ((uint32_t)cmd->pdata[3]<<8) | cmd->pdata[4];
                uint32_t count = 0;
                char name[187] = {0};
                response[0] = service_file_catalog(cmd->pdata[0], index, &count, name, sizeof(name));
                for(unsigned i = 0; i < 4; ++i) response[1+i] = (uint8_t)(count >> (24-i*8));
                length = 5;
                if(response[0] == 0) {
                    if(index >= count) response[0] = 4;
                    else { memcpy(response+5, name, strlen(name)+1); length += strlen(name)+1; }
                }
            }
            service_ble_send_resp(cmd->ch, response, length);
            break;
        }
        case BLE_FILM_TRANS_CH_HARDWARE_INFO_GET:
        {
            /* 纯空请求；带 DATA 属于错误用法，直接忽略（不回包） */
            if(cmd->len != 0) break;
            SysHwInfo_t info;
            sys_hw_get_info(&info);
            uint8_t data[14 + 32] = {1};
            data[1] = (uint8_t)(info.chip_id >> 8); data[2] = (uint8_t)info.chip_id;
            data[3] = (uint8_t)(info.chip_revision >> 8); data[4] = (uint8_t)info.chip_revision;
            for(unsigned i = 0; i < 4; ++i) {
                data[5 + i] = (uint8_t)(info.flash_bytes >> (24 - i * 8));
                data[9 + i] = (uint8_t)(info.psram_bytes >> (24 - i * 8));
            }
            data[13] = info.psram_bus;
            size_t target_len = strlen(info.target_id);
            memcpy(data + 14, info.target_id, target_len);
            service_ble_send_resp(cmd->ch, data, (uint8_t)(15 + target_len));
            break;
        }
        case BLE_FILM_TRANS_CH_BUILD_INFO_GET:
        {
            if(cmd->len != 0) break;
            const esp_app_desc_t *desc = esp_app_get_description();
            uint32_t max = service_ota_direct_max();
            uint8_t data[37 + sizeof(desc->version) + sizeof(desc->project_name)] = {1};
            for(unsigned i = 0; i < 4; ++i) data[1 + i] = (uint8_t)(max >> (24 - i * 8));
            memcpy(data + 5, desc->app_elf_sha256, 32);
            size_t version_len = strnlen(desc->version, sizeof(desc->version) - 1);
            size_t project_len = strnlen(desc->project_name, sizeof(desc->project_name) - 1);
            memcpy(data + 37, desc->version, version_len);
            memcpy(data + 38 + version_len, desc->project_name, project_len);
            service_ble_send_resp(cmd->ch, data, (uint8_t)(39 + version_len + project_len));
            break;
        }
        case BLE_FILM_TRANS_CH_DIRECT_OTA_APPLY:
        {
            if(cmd->len != 0) break;
            uint8_t result = service_wifi_direct_ota_apply();
            service_ble_send_resp(cmd->ch, &result, 1);
            break;
        }
        case BLE_FILM_TRANS_CH_DIRECT_OTA_START:
        case BLE_FILM_TRANS_CH_DIRECT_START:
        case BLE_FILM_TRANS_CH_DIRECT_FILE_START:
        {
            uint8_t result = ble_direct_start(cmd);
            service_ble_send_resp(cmd->ch, &result, 1);
            break;
        }
        case BLE_FILM_TRANS_CH_DIRECT_STATUS:
        {
            if(cmd->len != 0) break;
            wifi_direct_status_t status;
            service_wifi_direct_get_status(&status);
            uint8_t data[11] = {status.state, status.progress, status.error};
            for(unsigned i = 0; i < 4; i++)
            {
                data[3 + i] = (uint8_t)(status.received >> (24 - 8 * i));
                data[7 + i] = (uint8_t)(status.total >> (24 - 8 * i));
            }
            service_ble_send_resp(cmd->ch, data, sizeof(data));
            break;
        }
        case BLE_FILM_TRANS_CH_DIRECT_CANCEL:
        {
            if(cmd->len != 0) break;
            service_wifi_direct_cancel();
            const uint8_t accepted = 0;
            service_ble_send_resp(cmd->ch, &accepted, 1);
            break;
        }
        case BLE_FILM_TRANS_CH_PROFILE_READ:
            ble_profile_read(cmd);
            break;
        case BLE_FILM_TRANS_CH_DEVICE_NAME_GET:
        case BLE_FILM_TRANS_CH_DEVICE_NAME_SET:
        {
            uint8_t status = 0;
            if(cmd->ch == BLE_FILM_TRANS_CH_DEVICE_NAME_GET)
                status = cmd->len == 0 ? 0 : 1;
            else
                status = service_ble_device_name_set(cmd->pdata, cmd->len);

            uint8_t response[1 + BLE_DEVICE_NAME_MAX_BYTES + 1] = {status};
            uint8_t response_len = 1;
            if(status == 0)
            {
                const char *name = service_ble_device_name_get();
                size_t name_len = strlen(name) + 1;
                memcpy(&response[1], name, name_len);
                response_len += (uint8_t)name_len;
            }
            service_ble_send_resp(cmd->ch, response, response_len);
            break;
        }
        case BLE_FILM_TRANS_CH_FILE_START :
        {
            if(ble_file_reset() != 0)
            {
                sys_loge(BEL_SERVICE_TAG, "FILE_START: previous transfer abort failed");
                break;
            }
            m_film_trans_state = BLE_FILM_TRANS_STARTED;
            sys_logi(BEL_SERVICE_TAG, "Film transfer started");
            break;
        }
        case BLE_FILM_TRANS_CH_FILE_NAME :
        {
            /* m_film_trans_filename 为 256B，cmd->len 是 uint8_t（≤255），加 NUL 必然放得下 */
            if(m_film_trans_state == BLE_FILM_TRANS_STARTED && cmd->len > 0)
            {
                memcpy(m_film_trans_filename, cmd->pdata, cmd->len);
                m_film_trans_filename[cmd->len] = '\0';
                m_film_trans_state = BLE_FILM_TRANS_RECV_NAME;
                sys_logi(BEL_SERVICE_TAG, "Received filename: %s", m_film_trans_filename);
            }
            else
            {
                sys_logw(BEL_SERVICE_TAG, "Invalid state or length for FILE_NAME: state=%d, len=%d", m_film_trans_state, cmd->len);
            }
            break;
        }
        case BLE_FILM_TRANS_CH_FILE_LEN :
        {
            if(m_film_trans_state == BLE_FILM_TRANS_RECV_NAME && cmd->len == 4)
            {
                m_film_trans_file_size = (cmd->pdata[0] << 24) | (cmd->pdata[1] << 16) | (cmd->pdata[2] << 8) | cmd->pdata[3];
                sys_logi(BEL_SERVICE_TAG, "Received file size: %d", m_film_trans_file_size);

                // 文件名含 '/' 视为显式相对路径（如 app/image/cover.film），
                // 写入 /sdcard/<相对路径>，不参与图片/动图列表与事件
                int result;
                if(strchr((const char*)m_film_trans_filename, '/') != NULL)
                {
                    result = service_file_save_start_to(FILE_SAVE_BLE,
                              (const char*)m_film_trans_filename, m_film_trans_file_size);
                }
                else
                {
                    result = service_file_save_start(FILE_SAVE_BLE,
                              (const char*)m_film_trans_filename, m_film_trans_file_size);
                }
                if(result == 0) m_film_trans_state = BLE_FILM_TRANS_RECV_LEN;
                else { sys_loge(BEL_SERVICE_TAG, "FILE_LEN: save start failed"); ble_file_reset(); }
            }
            else
            {
                sys_logw(BEL_SERVICE_TAG, "Invalid state or length for FILE_LEN: state=%d, len=%d", m_film_trans_state, cmd->len);
            }
            break;
        }
        case BLE_FILM_TRANS_CH_FILE_DATA :
        {
            if((m_film_trans_state == BLE_FILM_TRANS_RECV_LEN || m_film_trans_state == BLE_FILM_TRANS_RECV_DATA) && cmd->len > 0)
            {
                uint8_t *pdata_copy = (uint8_t*)pvPortMalloc(cmd->len);
                if(pdata_copy && cmd->len <= m_film_trans_file_size - m_film_trans_received)
                {
                    memcpy(pdata_copy, cmd->pdata, cmd->len);
                    if(service_file_save_data(FILE_SAVE_BLE, pdata_copy, cmd->len) == 0)
                    {
                        m_film_trans_received += cmd->len;
                        m_film_trans_state = BLE_FILM_TRANS_RECV_DATA;
                    }
                    else { sys_loge(BEL_SERVICE_TAG, "FILE_DATA: write failed"); ble_file_reset(); }
                }
                else
                {
                    if(pdata_copy) vPortFree(pdata_copy);
                    sys_loge(BEL_SERVICE_TAG, "FILE_DATA: allocation or length failed");
                    ble_file_reset();
                }
            }
            else
            {
                sys_logw(BEL_SERVICE_TAG, "Invalid state or length for FILE_DATA: state=%d, len=%d", m_film_trans_state, cmd->len);
            }
            break;
        }
        case BLE_FILM_TRANS_CH_FILE_STOP :
        {
            if(m_film_trans_state == BLE_FILM_TRANS_RECV_DATA)
            {
                // 静默保存 flag：0x04 LEN=1 DATA=[0x01] 时保存后不自动加载（批量上传用）
                uint8_t auto_load = 1;
                if(cmd->len == 1 && cmd->pdata[0] == 0x01)
                {
                    auto_load = 0;
                }
                int result = -1;
                if(m_film_trans_received == m_film_trans_file_size)
                    result = service_file_save_stop(FILE_SAVE_BLE, auto_load);
                if(result == 0)
                {
                    sys_logi(BEL_SERVICE_TAG, "Film transfer saved: %d bytes", m_film_trans_received);
                    m_film_trans_state = BLE_FILM_TRANS_STOPPED;
                }
                else
                {
                    sys_loge(BEL_SERVICE_TAG, "FILE_STOP: save failed, received=%d expected=%d",
                             m_film_trans_received, m_film_trans_file_size);
                    ble_file_reset();
                }
            }
            else
            {
                sys_logw(BEL_SERVICE_TAG, "Invalid state for FILE_STOP: %d", m_film_trans_state);
            }
            break;
        }
        case BLE_FILM_TRANS_CH_FILE_DELETE : // 删除文件
        {
            if(cmd->len == 1)
            {
                uint8_t file_id = cmd->pdata[0];
                sys_logi(BEL_SERVICE_TAG, "Delete file id: %d", file_id);
                service_file_delete(file_id);
            }
            break;
        }
        case BLE_FILM_TRANS_CH_FILE_LIST : // 查询文件列表
        {
            uint32_t file_count = service_file_get_count();
            sys_logi(BEL_SERVICE_TAG, "File list count: %d", file_count);

            /* 单帧 = HEAD + CH + LEN + ID + NAME_LEN + NAME + NUL + SUM = 6 + name_len，
               其中 name_len = strlen(name) + 1。缓冲按下发通知的长度上限分配，
               并逐条校验帧长：文件名最长 255 字节，早先用固定 150B 缓冲且不校验长度，
               超长名会越界写堆。 */
            uint8_t *cmd_buf = pvPortMalloc(BLE_GATTS_CHAR_VAL_LEN_MAX);
            if(cmd_buf == NULL) break;

            for(uint32_t i = 0; i < file_count; i++)
            {
                memset(cmd_buf, 0, BLE_GATTS_CHAR_VAL_LEN_MAX);

                char filename[256];
                if(service_file_get_filename_safe(i, filename, sizeof(filename)) != 0)
                {
                    sys_logw(BEL_SERVICE_TAG, "File list changed during query, abort at index: %d", i);
                    break;
                }
                size_t name_len = strlen(filename) + 1;

                /* 帧长或 LEN 字段放不下就跳过该条：GATT 层会直接丢弃超长通知，
                   发出去也只会是坏帧 */
                if(name_len + 6 > BLE_GATTS_CHAR_VAL_LEN_MAX || name_len > 253)
                {
                    sys_logw(BEL_SERVICE_TAG, "File name too long, skip id=%u name_len=%u",
                             (unsigned)i, (unsigned)name_len);
                    continue;
                }

                cmd_buf[0] = BLE_CMD_HEAD;
                cmd_buf[1] = BLE_FILM_TRANS_CH_FILE_LIST;
                cmd_buf[2] = (uint8_t)(2 + name_len);
                cmd_buf[3] = i & 0xff;
                cmd_buf[4] = (uint8_t)name_len;

                memcpy(&cmd_buf[5], filename, name_len);

                uint8_t checksum = ble_checksum(cmd_buf, (int)(5 + name_len));
                cmd_buf[5 + name_len] = checksum;

                service_ble_send_notify_data(BLE_NOTIFY_SEND_CH1, cmd_buf, (uint16_t)(6 + name_len));

                vTaskDelay(50 / portTICK_PERIOD_MS);
            }

            vPortFree(cmd_buf);
            break;
        }
        case BLE_FILM_TRANS_CH_FILE_DISPLAY : // 显示id对应的文件
        {
            if(cmd->len == 1)
            {
                uint8_t file_id = cmd->pdata[0];
                sys_logi(BEL_SERVICE_TAG, "Display file id: %d", file_id);
                service_film_display(file_id);
            }
            break;
        }
        case BLE_FILM_TRANS_CH_FILE_DISPLAY_GET : // 查询当前显示的文件id
        {
            uint32_t current_id = service_file_get_current_id();
            sys_logi(BEL_SERVICE_TAG, "Current display file id: %d", current_id);

            uint8_t cmd_buf[5];
            cmd_buf[0] = BLE_CMD_HEAD;
            cmd_buf[1] = BLE_FILM_TRANS_CH_FILE_DISPLAY_GET;
            cmd_buf[2] = 1;
            cmd_buf[3] = current_id & 0xFF;
            cmd_buf[4] = ble_checksum(cmd_buf, 4);
            service_ble_msg_gatts_data_send(cmd_buf, sizeof(cmd_buf), MSG_BLE_CH1_OUT_DATA);
            break;
        }
        case BLE_FILM_TRANS_CH_OTA_LEN :
        {
            if(m_ota_trans_state == BLE_OTA_TRANS_IDLE && cmd->len == 4)
            {
                m_ota_trans_file_size = (cmd->pdata[0] << 24) | (cmd->pdata[1] << 16) | (cmd->pdata[2] << 8) | cmd->pdata[3];
                m_ota_trans_received = 0;
                m_ota_trans_state = BLE_OTA_TRANS_RECV_LEN;
                sys_logi(BEL_SERVICE_TAG, "OTA file size: %d bytes", m_ota_trans_file_size);
                service_ota_start();
                service_set_length(m_ota_trans_file_size);
            }
            else
            {
                sys_logw(BEL_SERVICE_TAG, "Invalid state or length for OTA_LEN: state=%d, len=%d", m_ota_trans_state, cmd->len);
            }
            break;
        }
        case BLE_FILM_TRANS_CH_OTA_DATA :
        {
            if((m_ota_trans_state == BLE_OTA_TRANS_RECV_LEN || m_ota_trans_state == BLE_OTA_TRANS_RECV_DATA) && cmd->len > 0)
            {
                service_ota_write(cmd->pdata, cmd->len);
                m_ota_trans_received += cmd->len;
                m_ota_trans_state = BLE_OTA_TRANS_RECV_DATA;
            }
            else
            {
                sys_logw(BEL_SERVICE_TAG, "Invalid state or length for OTA_DATA: state=%d, len=%d", m_ota_trans_state, cmd->len);
            }
            break;
        }
        case BLE_FILM_TRANS_CH_OTA_START :
        {
            if(m_ota_trans_state == BLE_OTA_TRANS_IDLE)
            {
                m_ota_trans_state = BLE_OTA_TRANS_STARTED;
                m_ota_trans_received = 0;
                service_ota_start();
                sys_logi(BEL_SERVICE_TAG, "OTA started (without length)");
            }
            else
            {
                sys_logw(BEL_SERVICE_TAG, "Invalid state for OTA_START: %d", m_ota_trans_state);
            }
            break;
        }
        case BLE_FILM_TRANS_CH_OTA_STOP :
        {
            if(m_ota_trans_state == BLE_OTA_TRANS_RECV_DATA || m_ota_trans_state == BLE_OTA_TRANS_RECV_LEN)
            {
                service_ota_stop();
                sys_logi(BEL_SERVICE_TAG, "OTA stopped, received: %d/%d bytes", m_ota_trans_received, m_ota_trans_file_size);
                m_ota_trans_state = BLE_OTA_TRANS_STOPPED;
            }
            else
            {
                sys_logw(BEL_SERVICE_TAG, "Invalid state for OTA_STOP: %d", m_ota_trans_state);
            }
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_MODE : // [已废弃] Film模式切换
        {
            // 播放模式语义已下移到图片 app 参数通道（0x45），此命令号按"命令值不再变更"原则保留，收到后忽略
            sys_logw(BEL_SERVICE_TAG, "Deprecated cmd 0x%02X ignored, use app param channel instead", cmd->ch);
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_MODE_GET : // [已废弃] Film模式查询
        {
            // 回 0xFF 表示已废弃
            sys_logw(BEL_SERVICE_TAG, "Deprecated cmd 0x%02X, reply 0xFF", cmd->ch);
            uint8_t resp[1];
            resp[0] = 0xFF;
            service_ble_send_resp(BLE_FILM_TRANS_CH_CTRL_MODE_GET, resp, sizeof(resp));
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_RESET : // 重置
        {
            sys_logi(BEL_SERVICE_TAG, "Resetting parameters...");
            service_param_reset();
            vTaskDelay(100 / portTICK_PERIOD_MS);
            sys_reboot();
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_PWRREAD : // 读取电池电压
        {
            uint8_t *cmd = NULL;
            cmd = pvPortMalloc(BLE_CMD_LEN_MIN + 1);
            if(cmd)
            {
                cmd[0] = BLE_CMD_HEAD;
                cmd[1] = BLE_FILM_TRANS_CH_CTRL_PWRREAD;
                cmd[2] = 1;
                cmd[3] = hal_bat_get_percent();
                cmd[4] = ble_checksum(cmd, BLE_CMD_LEN_MIN);
                service_ble_msg_gatts_data_send(cmd, BLE_CMD_LEN_MIN + 1, MSG_BLE_CH1_OUT_DATA);
                vPortFree(cmd);
                cmd = NULL;
                sys_logi(BEL_SERVICE_TAG, "Battery level: %d%%", hal_bat_get_percent());
            }
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_REBOOT : // 重启
        {
            sys_logi(BEL_SERVICE_TAG, "Rebooting...");
            sys_reboot();
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_SLEEPONOFF : // 休眠模式开关
        {
            if(cmd->len == 1 && (cmd->pdata[0] == 0 || cmd->pdata[0] == 1))
            {
                uint8_t mode = cmd->pdata[0];
                sys_logi(BEL_SERVICE_TAG, "Set sleep mode: %s", mode ? "ON" : "OFF");
                g_service_param.sleep.sleep_mode = mode;
                service_param_save();
            }
            break;
        }         
        case BLE_FILM_TRANS_CH_CTRL_SLEEPONOFF_GET : // 休眠模式开关查询
        {
            sys_logi(BEL_SERVICE_TAG, "Sleep mode: %s", g_service_param.sleep.sleep_mode ? "ON" : "OFF");
            uint8_t mode = g_service_param.sleep.sleep_mode;
            service_ble_send_resp(cmd->ch, &mode, 1);
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_SLEEPMODE : // 定时唤醒开关
        {
            if(cmd->len == 1 && (cmd->pdata[0] == 0 || cmd->pdata[0] == 1))
            {
                uint8_t auto_wake = cmd->pdata[0];
                sys_logi(BEL_SERVICE_TAG, "Set auto wake: %s", auto_wake ? "ON" : "OFF");
                g_service_param.sleep.sleep_auto = auto_wake;
                service_param_save();
            }
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_SLEEPMODE_GET : // 定时唤醒开关查询
        {
            sys_logi(BEL_SERVICE_TAG, "Auto wake: %s", g_service_param.sleep.sleep_auto ? "ON" : "OFF");
            uint8_t auto_wake = g_service_param.sleep.sleep_auto;
            service_ble_send_resp(cmd->ch, &auto_wake, 1);
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_SLEEPMODE_TIME : // 定时唤醒时间（单位分钟）
        {
            if(cmd->len == 2)
            {
                uint16_t time_min = (cmd->pdata[0] << 8) | cmd->pdata[1];
                if(time_min >= 10 && time_min <= 2880)
                {
                    sys_logi(BEL_SERVICE_TAG, "Set sleep wake time: %d min", time_min);
                    g_service_param.sleep.sleep_time = time_min;
                    service_param_save();
                }
            }
            break;
        }   
        case BLE_FILM_TRANS_CH_CTRL_SLEEPMODE_TIME_GET : // 定时唤醒时间查询（单位分钟）
        {
            sys_logi(BEL_SERVICE_TAG, "Sleep wake time: %d min", g_service_param.sleep.sleep_time);
            uint8_t time_min[2] = {
                (uint8_t)(g_service_param.sleep.sleep_time >> 8),
                (uint8_t)g_service_param.sleep.sleep_time,
            };
            service_ble_send_resp(cmd->ch, time_min, sizeof(time_min));
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_SDRESET : // SD卡格式化
        {
            sys_logi(BEL_SERVICE_TAG, "SD card format requested");
            int ret = hal_sd_format();
            uint8_t resp_buf[5];   /* HEAD+CH+LEN+DATA+SUM */
            resp_buf[0] = BLE_CMD_HEAD;
            resp_buf[1] = BLE_FILM_TRANS_CH_CTRL_SDRESET;
            resp_buf[2] = 1;
            resp_buf[3] = (ret == 0) ? 0 : 1;
            resp_buf[4] = ble_checksum(resp_buf, 4);
            service_ble_msg_gatts_data_send(resp_buf, sizeof(resp_buf), MSG_BLE_CH1_OUT_DATA);
            if(ret == 0)
            {
                sys_logi(BEL_SERVICE_TAG, "SD card formatted, rebooting...");
                vTaskDelay(500 / portTICK_PERIOD_MS);
                sys_reboot();
            }
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_WIFI_ENABLE : // WiFi开关设置
        {
            if(cmd->len == 1 && (cmd->pdata[0] == 0 || cmd->pdata[0] == 1))
            {
                uint8_t enable = cmd->pdata[0];
                sys_logi(BEL_SERVICE_TAG, "Set WiFi enable: %s", enable ? "ON" : "OFF");
                g_service_param.network.wifi_enable = enable;
                service_param_save();
                if(enable)
                {
                    service_wifi_init();
                }
                else
                {
                    service_wifi_disconnect();
                    service_wifi_deinit();
                }
            }
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_WIFI_ENABLE_GET : // WiFi开关查询
        {
            sys_logi(BEL_SERVICE_TAG, "WiFi enable: %s", g_service_param.network.wifi_enable ? "ON" : "OFF");
            uint8_t resp_buf[5];   /* HEAD+CH+LEN+DATA+SUM */
            resp_buf[0] = BLE_CMD_HEAD;
            resp_buf[1] = BLE_FILM_TRANS_CH_CTRL_WIFI_ENABLE_GET;
            resp_buf[2] = 1;
            resp_buf[3] = g_service_param.network.wifi_enable & 0xFF;
            resp_buf[4] = ble_checksum(resp_buf, 4);
            service_ble_msg_gatts_data_send(resp_buf, sizeof(resp_buf), MSG_BLE_CH1_OUT_DATA);
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_WIFI_SSID : // WiFi SSID设置
        {
            if(cmd->len > 0 && cmd->len < sizeof(g_service_param.network.wifi_ssid))
            {
                memset(g_service_param.network.wifi_ssid, 0, sizeof(g_service_param.network.wifi_ssid));
                memcpy(g_service_param.network.wifi_ssid, cmd->pdata, cmd->len);
                sys_logi(BEL_SERVICE_TAG, "Set WiFi SSID: %s", g_service_param.network.wifi_ssid);
                service_param_save();
            }
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_WIFI_SSID_GET : // WiFi SSID查询
        {
            uint8_t ssid_len = strlen(g_service_param.network.wifi_ssid);
            sys_logi(BEL_SERVICE_TAG, "WiFi SSID: %s", g_service_param.network.wifi_ssid);
            uint8_t resp_ssid_len = 4 + ssid_len; // HEAD + CH + LEN + DATA
            uint8_t *resp_buf = pvPortMalloc(resp_ssid_len);
            if(resp_buf)
            {
                resp_buf[0] = BLE_CMD_HEAD;
                resp_buf[1] = BLE_FILM_TRANS_CH_CTRL_WIFI_SSID_GET;
                resp_buf[2] = ssid_len;
                if(ssid_len > 0)
                {
                    memcpy(&resp_buf[3], g_service_param.network.wifi_ssid, ssid_len);
                }
                resp_buf[3 + ssid_len] = ble_checksum(resp_buf, 3 + ssid_len);
                service_ble_msg_gatts_data_send(resp_buf, resp_ssid_len, MSG_BLE_CH1_OUT_DATA);
                vPortFree(resp_buf);
            }
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_WIFI_PASSWORD : // WiFi 密码设置
        {
            if(cmd->len > 0 && cmd->len < sizeof(g_service_param.network.wifi_password))
            {
                memset(g_service_param.network.wifi_password, 0, sizeof(g_service_param.network.wifi_password));
                memcpy(g_service_param.network.wifi_password, cmd->pdata, cmd->len);
                sys_logi(BEL_SERVICE_TAG, "Set WiFi password");
                service_param_save();
            }
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_WIFI_PASSWORD_GET : // WiFi 密码查询
        {
            uint8_t pwd_len = strlen(g_service_param.network.wifi_password);
            sys_logi(BEL_SERVICE_TAG, "WiFi password query");
            uint8_t resp_pwd_len = 4 + pwd_len;
            uint8_t *resp_buf = pvPortMalloc(resp_pwd_len);
            if(resp_buf)
            {
                resp_buf[0] = BLE_CMD_HEAD;
                resp_buf[1] = BLE_FILM_TRANS_CH_CTRL_WIFI_PASSWORD_GET;
                resp_buf[2] = pwd_len;
                if(pwd_len > 0)
                {
                    memcpy(&resp_buf[3], g_service_param.network.wifi_password, pwd_len);
                }
                resp_buf[3 + pwd_len] = ble_checksum(resp_buf, 3 + pwd_len);
                service_ble_msg_gatts_data_send(resp_buf, resp_pwd_len, MSG_BLE_CH1_OUT_DATA);
                vPortFree(resp_buf);
            }
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_FILM_API_URL : // HTTP下载film文件的API地址设置
        {
            if(cmd->len > 0 && cmd->len < sizeof(g_service_param.network.film_api_url))
            {
                memset(g_service_param.network.film_api_url, 0, sizeof(g_service_param.network.film_api_url));
                memcpy(g_service_param.network.film_api_url, cmd->pdata, cmd->len);
                sys_logi(BEL_SERVICE_TAG, "Set film API URL: %s", g_service_param.network.film_api_url);
                service_param_save();
            }
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_FILM_API_URL_GET : // HTTP下载film文件的API地址查询
        {
            uint8_t url_len = strlen(g_service_param.network.film_api_url);
            sys_logi(BEL_SERVICE_TAG, "Film API URL: %s", g_service_param.network.film_api_url);
            uint8_t resp_url_len = 4 + url_len;
            uint8_t *resp_buf = pvPortMalloc(resp_url_len);
            if(resp_buf)
            {
                resp_buf[0] = BLE_CMD_HEAD;
                resp_buf[1] = BLE_FILM_TRANS_CH_CTRL_FILM_API_URL_GET;
                resp_buf[2] = url_len;
                if(url_len > 0)
                {
                    memcpy(&resp_buf[3], g_service_param.network.film_api_url, url_len);
                }
                resp_buf[3 + url_len] = ble_checksum(resp_buf, 3 + url_len);
                service_ble_msg_gatts_data_send(resp_buf, resp_url_len, MSG_BLE_CH1_OUT_DATA);
                vPortFree(resp_buf);
            }
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_WIFI_CONNECT : // 连接WiFi
        {
            sys_logi(BEL_SERVICE_TAG, "WiFi connect requested");
            service_wifi_connect();
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_WIFI_DISCONNECT : // 断开WiFi连接
        {
            sys_logi(BEL_SERVICE_TAG, "WiFi disconnect requested");
            service_wifi_disconnect();
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_WIFI_CONNECT_GET : // 查询WiFi连接状态
        {
            uint8_t status = service_wifi_get_connect_status();
            sys_logi(BEL_SERVICE_TAG, "WiFi connect status: %s", status ? "Connected" : "Disconnected");
            uint8_t resp_buf[5];   /* HEAD+CH+LEN+DATA+SUM */
            resp_buf[0] = BLE_CMD_HEAD;
            resp_buf[1] = BLE_FILM_TRANS_CH_CTRL_WIFI_CONNECT_GET;
            resp_buf[2] = 1;
            resp_buf[3] = status;
            resp_buf[4] = ble_checksum(resp_buf, 4);
            service_ble_msg_gatts_data_send(resp_buf, sizeof(resp_buf), MSG_BLE_CH1_OUT_DATA);
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_WIFI_CLEAR : // 清除网络配置信息
        {
            sys_logi(BEL_SERVICE_TAG, "WiFi config clear requested");
            service_wifi_clear_config();
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_FILM_DOWNLOAD : // 开始下载film文件
        {
            sys_logi(BEL_SERVICE_TAG, "Film download requested");
            service_wifi_download_start();
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_FILM_DOWNLOAD_STATE : // 查询下载状态
        {
            wifi_download_state_t state = service_wifi_download_get_state();
            uint8_t progress = service_wifi_download_get_progress();
            sys_logi(BEL_SERVICE_TAG, "Download state: %d, progress: %d%%", state, progress);
            uint8_t resp_buf[6];   /* HEAD+CH+LEN+DATA(2B)+SUM */
            resp_buf[0] = BLE_CMD_HEAD;
            resp_buf[1] = BLE_FILM_TRANS_CH_CTRL_FILM_DOWNLOAD_STATE;
            resp_buf[2] = 2;
            resp_buf[3] = (uint8_t)state;
            resp_buf[4] = progress;
            resp_buf[5] = ble_checksum(resp_buf, 5);
            service_ble_msg_gatts_data_send(resp_buf, sizeof(resp_buf), MSG_BLE_CH1_OUT_DATA);
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_FILM_HEARTBEAT_URL : // HTTP心跳地址设置
        {
            if(cmd->len > 0 && cmd->len < sizeof(g_service_param.network.film_heartbeat_url))
            {
                memset(g_service_param.network.film_heartbeat_url, 0, sizeof(g_service_param.network.film_heartbeat_url));
                memcpy(g_service_param.network.film_heartbeat_url, cmd->pdata, cmd->len);
                sys_logi(BEL_SERVICE_TAG, "Set heartbeat URL: %s", g_service_param.network.film_heartbeat_url);
                service_param_save();
            }
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_FILM_HEARTBEAT_URL_GET : // HTTP心跳地址查询
        {
            uint8_t url_len = strlen(g_service_param.network.film_heartbeat_url);
            sys_logi(BEL_SERVICE_TAG, "Heartbeat URL: %s", g_service_param.network.film_heartbeat_url);
            uint8_t resp_url_len = 4 + url_len;
            uint8_t *resp_buf = pvPortMalloc(resp_url_len);
            if(resp_buf)
            {
                resp_buf[0] = BLE_CMD_HEAD;
                resp_buf[1] = BLE_FILM_TRANS_CH_CTRL_FILM_HEARTBEAT_URL_GET;
                resp_buf[2] = url_len;
                if(url_len > 0)
                {
                    memcpy(&resp_buf[3], g_service_param.network.film_heartbeat_url, url_len);
                }
                resp_buf[3 + url_len] = ble_checksum(resp_buf, 3 + url_len);
                service_ble_msg_gatts_data_send(resp_buf, resp_url_len, MSG_BLE_CH1_OUT_DATA);
                vPortFree(resp_buf);
            }
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_FILM_HEARTBEAT_INTERVAL : // 心跳间隔设置
        {
            if(cmd->len == 1 && cmd->pdata[0] >= 5 && cmd->pdata[0] <= 180)
            {
                g_service_param.network.film_heartbeat_interval = cmd->pdata[0];
                sys_logi(BEL_SERVICE_TAG, "Set heartbeat interval: %ds", cmd->pdata[0]);
                service_param_save();
            }
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_FILM_HEARTBEAT_INTERVAL_GET : // 心跳间隔查询
        {
            sys_logi(BEL_SERVICE_TAG, "Heartbeat interval: %ds", g_service_param.network.film_heartbeat_interval);
            uint8_t resp_buf[5];
            resp_buf[0] = BLE_CMD_HEAD;
            resp_buf[1] = BLE_FILM_TRANS_CH_CTRL_FILM_HEARTBEAT_INTERVAL_GET;
            resp_buf[2] = 1;
            resp_buf[3] = g_service_param.network.film_heartbeat_interval;
            resp_buf[4] = ble_checksum(resp_buf, 4);
            service_ble_msg_gatts_data_send(resp_buf, 5, MSG_BLE_CH1_OUT_DATA);
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_SCREEN_RESOLUTION_GET : // 查询屏幕面板 ID 与分辨率
        {
            uint8_t resp_buf[9];
            resp_buf[0] = BLE_CMD_HEAD;
            resp_buf[1] = BLE_FILM_TRANS_CH_CTRL_SCREEN_RESOLUTION_GET;
            resp_buf[2] = 5;
            resp_buf[3] = EPD_PANEL_ID;
            resp_buf[4] = (EPD_WIDTH >> 8) & 0xFF;
            resp_buf[5] = EPD_WIDTH & 0xFF;
            resp_buf[6] = (EPD_HEIGHT >> 8) & 0xFF;
            resp_buf[7] = EPD_HEIGHT & 0xFF;
            resp_buf[8] = ble_checksum(resp_buf, 8);
            service_ble_msg_gatts_data_send(resp_buf, sizeof(resp_buf), MSG_BLE_CH1_OUT_DATA);
            sys_logi(BEL_SERVICE_TAG, "Screen info: panel_id=0x%02x, %d x %d", EPD_PANEL_ID, EPD_WIDTH, EPD_HEIGHT);
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_TIME_SYNC : // 时间 + 时区同步
        {
            /* payload: 4B 大端 Unix 秒（UTC）+ 2B 大端时区（距 UTC 分钟数，东为正） */
            if(cmd->len == 6)
            {
                uint32_t sec = ((uint32_t)cmd->pdata[0] << 24) | ((uint32_t)cmd->pdata[1] << 16)
                             | ((uint32_t)cmd->pdata[2] << 8)  |  (uint32_t)cmd->pdata[3];
                int16_t tz = (int16_t)(((uint16_t)cmd->pdata[4] << 8) | (uint16_t)cmd->pdata[5]);

                if(service_time_sync((int64_t)sec, tz) == 0)
                {
                    /* 回显原样 6 字节：连接端据此确认设备确实应用了，不必再等下一帧 */
                    uint8_t resp_buf[10];

                    resp_buf[0] = BLE_CMD_HEAD;
                    resp_buf[1] = BLE_FILM_TRANS_CH_CTRL_TIME_SYNC;
                    resp_buf[2] = 6;
                    memcpy(&resp_buf[3], cmd->pdata, 6);
                    resp_buf[9] = ble_checksum(resp_buf, 9);
                    service_ble_msg_gatts_data_send(resp_buf, sizeof(resp_buf), MSG_BLE_CH1_OUT_DATA);
                }
            }
            else
            {
                sys_logw(BEL_SERVICE_TAG, "time sync: bad len %u (expect 6)", (unsigned)cmd->len);
            }
            break;
        }
        // app 相关通道（0x4B 切 app / 0x45~0x4A 参数通道 / 0x63~0x69 NFC 卡包）：BLE 层不解析语义，
        // 整包上浮给 app 层，由 app_manager 按 ch 归属路由（0x4B 走切换逻辑，0x45~0x4A 走目标 app 的参数读写）
        case BLE_FILM_TRANS_CH_CTRL_APP_SWITCH :
        case BLE_FILM_TRANS_CH_APP_IMAGE_PARAM :
        case BLE_FILM_TRANS_CH_APP_IMAGE_PARAM_GET :
        case BLE_FILM_TRANS_CH_APP_TEMPLATE_PARAM :
        case BLE_FILM_TRANS_CH_APP_TEMPLATE_PARAM_GET :
        case BLE_FILM_TRANS_CH_APP_ANIM_PARAM :
        case BLE_FILM_TRANS_CH_APP_ANIM_PARAM_GET :
#if (SYS_NFC_ENABLE == 1)
        case BLE_FILM_TRANS_CH_NFC_CARD_BEGIN :
        case BLE_FILM_TRANS_CH_NFC_CARD_DATA :
        case BLE_FILM_TRANS_CH_NFC_CARD_COMMIT :
        case BLE_FILM_TRANS_CH_NFC_WALLET_CTRL :
        case BLE_FILM_TRANS_CH_NFC_COVER_BEGIN :
        case BLE_FILM_TRANS_CH_NFC_COVER_DATA :
        case BLE_FILM_TRANS_CH_NFC_COVER_COMMIT :
#endif
        {
            ble_cmd_publish_app(cmd);
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_APP_CURRENT_GET : // 查询当前 app
        {
            uint8_t app_id = m_app_id_get_cb ? m_app_id_get_cb() : 0xFF;
            uint8_t resp_buf[5];
            resp_buf[0] = BLE_CMD_HEAD;
            resp_buf[1] = BLE_FILM_TRANS_CH_CTRL_APP_CURRENT_GET;
            resp_buf[2] = 1;
            resp_buf[3] = app_id;
            resp_buf[4] = ble_checksum(resp_buf, 4);
            service_ble_msg_gatts_data_send(resp_buf, sizeof(resp_buf), MSG_BLE_CH1_OUT_DATA);
            sys_logi(BEL_SERVICE_TAG, "Current app id: %d", app_id);
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_KEY_INJECT : // 远程按键注入（遥控器）
        {
            /* payload: 1B 键值（BLE_KEY_*）。
               BLE 层只做"值域校验 + 上浮"，不解析按键语义（与 app 参数通道同款：
               上下隔离，BLE 层不认识"上/下/确认"）。app 层收到后按真实按键事件投递，
               因此菜单导航、双击退回、长按休眠等全部语义自动一致。 */
            if(cmd->len == 1 && cmd->pdata[0] <= BLE_KEY_MAX)
            {
                uint8_t evt[2];

                evt[0] = cmd->ch;
                evt[1] = cmd->pdata[0];
                sys_event_publish(SYS_EVT_BLE_APP_CMD, evt, sizeof(evt));

                /* 回显同 1 字节：连接端据此确认已注入（不保证一定被消费：
                   开机卡/休眠卡占屏期间按键按本机语义一样被丢弃） */
                (void)service_ble_send_resp(cmd->ch, cmd->pdata, 1);
                sys_logi(BEL_SERVICE_TAG, "key inject: %u", (unsigned)cmd->pdata[0]);
            }
            else
            {
                sys_logw(BEL_SERVICE_TAG, "key inject: bad payload (len=%u key=%u)",
                         (unsigned)cmd->len, (unsigned)((cmd->len > 0) ? cmd->pdata[0] : 0xFF));
            }
            break;
        }
        case BLE_FILM_TRANS_CH_LOG_STREAM_CTRL : // 蓝牙实时日志开关/查询
        {
            /* payload: 1B（0=关 1=开）；空 payload 视为查询。回 1B 当前状态。
               打开时设备按当前日志级别（不额外放开）采集日志，经 0x5F 逐帧推送；
               关闭/断开连接后自动卸载钩子。 */
            if(cmd->len == 1 && (cmd->pdata[0] == 0 || cmd->pdata[0] == 1))
            {
                service_log_stream_set(cmd->pdata[0]);
                uint8_t state = service_log_stream_get();
                (void)service_ble_send_resp(cmd->ch, &state, 1);
            }
            else if(cmd->len == 0)
            {
                uint8_t state = service_log_stream_get();
                (void)service_ble_send_resp(cmd->ch, &state, 1);
            }
            else
            {
                sys_logw(BEL_SERVICE_TAG, "log stream: bad payload (len=%u)", (unsigned)cmd->len);
            }
            break;
        }
        default :
        {
            break;
        }
    }
}

/**
 * [service_ble_set_app_id_get_cb 注册当前 app 查询回调]
 */
void service_ble_set_app_id_get_cb(service_ble_app_id_get_cb_t cb)
{
    m_app_id_get_cb = cb;
}

/**
 * [ble_checksum 和校验]
 * @param  arr [校验函数]
 * @param  len [校验长度]
 * @return     [校验值]
 */
static uint8_t ble_checksum(uint8_t arr[], int len)
{
    uint8_t sum = 0;
    for (int i = 0; i < len; i++)
    {
        sum += arr[i];
    }
    return sum;
}

/**
 * [service_ble_send_resp 按 BLE 帧格式回发一包数据]
 * @param  ch   [通道]
 * @param  data [数据负载，可为 NULL]
 * @param  len  [数据长度，超过上限自动截断]
 */
void service_ble_send_resp(uint8_t ch, const uint8_t *data, uint8_t len)
{
    uint8_t buf[BLE_RESP_DATA_MAX + 4];

    if(len > BLE_RESP_DATA_MAX)
    {
        len = BLE_RESP_DATA_MAX;
    }

    buf[0] = BLE_CMD_HEAD;
    buf[1] = ch;
    buf[2] = len;
    if(len > 0 && data != NULL)
    {
        memcpy(&buf[3], data, len);
    }
    buf[3 + len] = ble_checksum(buf, 3 + len);

    service_ble_msg_gatts_data_send(buf, (uint16_t)(4 + len), MSG_BLE_CH1_OUT_DATA);
}

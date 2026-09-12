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
 * FileName : /film_service/src/service_cmd.c
 * Author: Kiritro  Version: v0.1  Date: 2026/9/11
 * Description: 传输无关的命令解析与分发（BLE / USB 共用），回包按来源链路返回
 * ChangeLog: Change Notes
 *
 *********************************************************************/

/*********************************************************************
 * INCLUDES
 */
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "sys_log.h"
#include "sys_com.h"

#include "hal_api.h"

#include "service_cmd.h"
#include "service_file.h"
#include "service_film.h"
#include "service_ota.h"
#include "service_param.h"
#include "service_wifi.h"

/*********************************************************************
 * MACROS
 */
#define CMD_MSG_QUEUE_LENGTH        (30)
#define CMD_MSG_QUEUE_ITEM_SIZE     sizeof( cmd_msg_t )

/* 每条链路的收包聚合缓冲：需容纳一个完整帧（最长 4 + 255 字节） */
#define CMD_RX_BUF_SIZE             (512)

#define CMD_FILM_TRANS_IDLE         (0)
#define CMD_FILM_TRANS_STARTED      (1)
#define CMD_FILM_TRANS_RECV_NAME    (2)
#define CMD_FILM_TRANS_RECV_LEN     (3)
#define CMD_FILM_TRANS_RECV_DATA    (4)
#define CMD_FILM_TRANS_STOPPED      (5)

#define CMD_OTA_TRANS_IDLE          (0)
#define CMD_OTA_TRANS_STARTED       (1)
#define CMD_OTA_TRANS_RECV_LEN      (2)
#define CMD_OTA_TRANS_RECV_DATA     (3)
#define CMD_OTA_TRANS_STOPPED       (4)

/*********************************************************************
* TYPEDEFS
*/
typedef struct
{
    service_cmd_src_t src;
    uint16_t len;
    uint8_t *pdata;
} cmd_msg_t;

typedef struct
{
    uint16_t len;
    uint8_t buf[CMD_RX_BUF_SIZE];
} cmd_rx_t;

/*********************************************************************
 * CONSTANTS
 */

/*********************************************************************
 * LOCAL VARIABLES
 */
static TaskHandle_t m_cmd_task_hdl = NULL;
static QueueHandle_t m_cmd_msg_hdl = NULL;
static service_cmd_out_cb_t m_out_cb[SERVICE_CMD_SRC_MAX] = { NULL };
static cmd_rx_t m_rx[SERVICE_CMD_SRC_MAX];

// 传输状态（同一时刻只允许一条链路占用）
static uint8_t m_film_trans_state = CMD_FILM_TRANS_IDLE;
static service_cmd_src_t m_film_trans_owner = SERVICE_CMD_SRC_MAX;
static uint8_t m_film_trans_filename[256];
static uint32_t m_film_trans_file_size = 0;
static uint32_t m_film_trans_received = 0;
static uint8_t m_ota_trans_state = CMD_OTA_TRANS_IDLE;
static service_cmd_src_t m_ota_trans_owner = SERVICE_CMD_SRC_MAX;
static uint32_t m_ota_trans_file_size = 0;
static uint32_t m_ota_trans_received = 0;

// 按键事件 -> HID 键值配置映射（下标为 service_key_event_t）
static ServiceKey_Def_t *const m_key_map[SERVICE_KEY_EVENT_MAX] = {
    &g_service_param.key.short_press,
    &g_service_param.key.double_press,
    &g_service_param.key.long_press,
};

/*********************************************************************
 * GLOBAL VARIABLES
 */

/*********************************************************************
 * LOCAL FUNCTIONS
 */
static void cmd_task_handle(void *pvParameters);
static void cmd_rx_append(service_cmd_src_t src, const uint8_t *p_data, uint16_t len);
static void cmd_process(service_cmd_src_t src, uint8_t ch, const uint8_t *pdata, uint8_t len);
static uint8_t cmd_checksum(const uint8_t *arr, int len);

/*********************************************************************
 * GLOBAL FUNCTIONS
 */

/**
 * [service_cmd_init 初始化命令服务]
 */
void service_cmd_init(void)
{
    for(int i = 0; i < SERVICE_CMD_SRC_MAX; i++)
    {
        m_rx[i].len = 0;
        m_out_cb[i] = NULL;
    }

    if(m_cmd_task_hdl == NULL)
    {
        if(pdPASS != xTaskCreate(cmd_task_handle, SYS_OS_NAME_CMD_TASK, SYS_OS_SIZE_CMD_TASK, NULL, SYS_OS_PRI_CMD_TASK, &m_cmd_task_hdl))
        {
            sys_loge(CMD_TAG, "cmd task create error!");
        }
    }
}

/**
 * [service_cmd_src_register 注册链路输出回调]
 */
void service_cmd_src_register(service_cmd_src_t src, service_cmd_out_cb_t cb)
{
    if(src < SERVICE_CMD_SRC_MAX)
    {
        m_out_cb[src] = cb;
    }
}

/**
 * [service_cmd_input 投递原始字节流]
 */
void service_cmd_input(service_cmd_src_t src, const uint8_t *p_data, uint16_t len)
{
    if(src >= SERVICE_CMD_SRC_MAX || p_data == NULL || len == 0)
    {
        return;
    }

    if(m_cmd_msg_hdl != NULL)
    {
        cmd_msg_t msg = {0};
        msg.src = src;
        msg.len = len;
        msg.pdata = pvPortMalloc(len);

        if(msg.pdata == NULL)
        {
            sys_loge(CMD_TAG, "cmd msg malloc error!");
            return;
        }

        memcpy(msg.pdata, p_data, len);
        SYS_ERROR_CHECK(xQueueSend(m_cmd_msg_hdl, &msg, portMAX_DELAY) != pdPASS);
    }
}

/**
 * [service_cmd_send 从指定链路发送一帧应答]
 */
void service_cmd_send(service_cmd_src_t src, const uint8_t *p_data, uint16_t len)
{
    if(src >= SERVICE_CMD_SRC_MAX || p_data == NULL || len == 0)
    {
        return;
    }

    if(m_out_cb[src] != NULL)
    {
        m_out_cb[src](src, p_data, len);
    }
    else
    {
        sys_logw(CMD_TAG, "no out cb for src %d", src);
    }
}

static void cmd_task_handle(void *pvParameters)
{
    m_cmd_msg_hdl = xQueueCreate( CMD_MSG_QUEUE_LENGTH, CMD_MSG_QUEUE_ITEM_SIZE );
    if(m_cmd_msg_hdl == NULL)
    {
        sys_loge(CMD_TAG, "cmd msg queue create error!");
        vTaskDelete(NULL);
        return;
    }

    for(;;)
    {
        cmd_msg_t msg;
        if(xQueueReceive( m_cmd_msg_hdl, (void *const)&msg, portMAX_DELAY ) == pdPASS)
        {
            cmd_rx_append(msg.src, msg.pdata, msg.len);

            if(msg.pdata)
            {
                vPortFree(msg.pdata);
            }
        }
    }
}

/**
 * [cmd_rx_append 聚合字节流并按帧解析分发]
 *
 * 允许半包/粘包：先累积到链路缓冲，再按 HEAD + LEN 定位完整帧并校验，
 * 校验失败的帧丢弃首字节重新同步。
 */
static void cmd_rx_append(service_cmd_src_t src, const uint8_t *p_data, uint16_t len)
{
    cmd_rx_t *rx = &m_rx[src];

    if(p_data == NULL || len == 0)
    {
        return;
    }

    // 缓冲不足以容纳新数据时整体丢弃，重新同步
    if((uint32_t)rx->len + len > CMD_RX_BUF_SIZE)
    {
        sys_logw(CMD_TAG, "rx buf overflow, drop %d bytes", rx->len);
        rx->len = 0;
    }

    if(len > CMD_RX_BUF_SIZE)
    {
        p_data += (len - CMD_RX_BUF_SIZE);
        len = CMD_RX_BUF_SIZE;
    }

    memcpy(rx->buf + rx->len, p_data, len);
    rx->len += len;

    uint16_t pos = 0;
    while((uint16_t)(rx->len - pos) >= 3)
    {
        if(rx->buf[pos] != BLE_CMD_HEAD)
        {
            pos++;
            continue;
        }

        uint8_t data_len = rx->buf[pos + 2];
        uint16_t frame_len = (uint16_t)data_len + BLE_CMD_LEN_MIN;

        if((uint16_t)(rx->len - pos) < frame_len)
        {
            // 半包，等待后续数据
            break;
        }

        if(cmd_checksum(&rx->buf[pos], 3 + data_len) == rx->buf[pos + 3 + data_len])
        {
            cmd_process(src, rx->buf[pos + 1], &rx->buf[pos + 3], data_len);
            pos += frame_len;
        }
        else
        {
            sys_logw(CMD_TAG, "checksum error, resync");
            pos++;
        }
    }

    if(pos > 0)
    {
        memmove(rx->buf, rx->buf + pos, rx->len - pos);
        rx->len -= pos;
    }
}

static void cmd_process(service_cmd_src_t src, uint8_t ch, const uint8_t *pdata, uint8_t len)
{
    switch(ch)
    {
        case BLE_FILM_TRANS_CH_FILE_START :
        {
            if(m_film_trans_state == CMD_FILM_TRANS_IDLE || m_film_trans_state == CMD_FILM_TRANS_STOPPED)
            {
                m_film_trans_state = CMD_FILM_TRANS_STARTED;
                m_film_trans_owner = src;
                m_film_trans_received = 0;
                memset(m_film_trans_filename, 0, sizeof(m_film_trans_filename));
                m_film_trans_file_size = 0;
                sys_logi(CMD_TAG, "Film transfer started, src %d", src);
            }
            else
            {
                sys_logw(CMD_TAG, "Invalid state for FILE_START: %d", m_film_trans_state);
            }
            break;
        }
        case BLE_FILM_TRANS_CH_FILE_NAME :
        {
            if(m_film_trans_state == CMD_FILM_TRANS_STARTED && m_film_trans_owner == src && len > 0)
            {
                memcpy(m_film_trans_filename, pdata, len);
                m_film_trans_filename[len] = '\0';
                m_film_trans_state = CMD_FILM_TRANS_RECV_NAME;
                sys_logi(CMD_TAG, "Received filename: %s", m_film_trans_filename);
            }
            else
            {
                sys_logw(CMD_TAG, "Invalid state or length for FILE_NAME: state=%d, len=%d", m_film_trans_state, len);
            }
            break;
        }
        case BLE_FILM_TRANS_CH_FILE_LEN :
        {
            if(m_film_trans_state == CMD_FILM_TRANS_RECV_NAME && m_film_trans_owner == src && len == 4)
            {
                m_film_trans_file_size = (pdata[0] << 24) | (pdata[1] << 16) | (pdata[2] << 8) | pdata[3];
                m_film_trans_state = CMD_FILM_TRANS_RECV_LEN;
                sys_logi(CMD_TAG, "Received file size: %d", m_film_trans_file_size);

                service_file_save_start((const char*)m_film_trans_filename, m_film_trans_file_size);
            }
            else
            {
                sys_logw(CMD_TAG, "Invalid state or length for FILE_LEN: state=%d, len=%d", m_film_trans_state, len);
            }
            break;
        }
        case BLE_FILM_TRANS_CH_FILE_DATA :
        {
            if((m_film_trans_state == CMD_FILM_TRANS_RECV_LEN || m_film_trans_state == CMD_FILM_TRANS_RECV_DATA) &&
               m_film_trans_owner == src && len > 0)
            {
                uint8_t *pdata_copy = (uint8_t*)pvPortMalloc(len);
                if(pdata_copy)
                {
                    memcpy(pdata_copy, pdata, len);
                    service_file_save_data((const char*)m_film_trans_filename, m_film_trans_file_size, pdata_copy, len);
                    m_film_trans_received += len;
                    m_film_trans_state = CMD_FILM_TRANS_RECV_DATA;
                }
            }
            else
            {
                sys_logw(CMD_TAG, "Invalid state or length for FILE_DATA: state=%d, len=%d", m_film_trans_state, len);
            }
            break;
        }
        case BLE_FILM_TRANS_CH_FILE_STOP :
        {
            if(m_film_trans_state == CMD_FILM_TRANS_RECV_DATA && m_film_trans_owner == src)
            {
                // 静默保存 flag：0x04 LEN=1 DATA=[0x01] 时保存后不自动加载（批量上传用）
                uint8_t auto_load = 1;
                if(len == 1 && pdata[0] == 0x01)
                {
                    auto_load = 0;
                }
                service_file_save_stop(auto_load);
                sys_logi(CMD_TAG, "Film transfer stopped, received: %d/%d bytes", m_film_trans_received, m_film_trans_file_size);
                m_film_trans_state = CMD_FILM_TRANS_STOPPED;
                m_film_trans_owner = SERVICE_CMD_SRC_MAX;
            }
            else
            {
                sys_logw(CMD_TAG, "Invalid state for FILE_STOP: %d", m_film_trans_state);
            }
            break;
        }
        case BLE_FILM_TRANS_CH_FILE_DELETE : // 删除文件
        {
            if(len == 1)
            {
                uint8_t file_id = pdata[0];
                sys_logi(CMD_TAG, "Delete file id: %d", file_id);
                service_file_delete(file_id);
            }
            break;
        }
        case BLE_FILM_TRANS_CH_FILE_LIST : // 查询文件列表
        {
            uint32_t file_count = service_file_get_count();
            sys_logi(CMD_TAG, "File list count: %d", file_count);

            uint8_t *cmd_buf = pvPortMalloc(150);
            if(cmd_buf == NULL) break;

            for(uint32_t i = 0; i < file_count; i++)
            {
                memset(cmd_buf, 0, 150);

                char filename[256];
                if(service_file_get_filename_safe(i, filename, sizeof(filename)) != 0)
                {
                    sys_logw(CMD_TAG, "File list changed during query, abort at index: %d", i);
                    break;
                }
                uint8_t name_len = strlen(filename) + 1;

                cmd_buf[0] = BLE_CMD_HEAD;
                cmd_buf[1] = BLE_FILM_TRANS_CH_FILE_LIST;
                cmd_buf[2] = 2 + name_len;
                cmd_buf[3] = i & 0xff;
                cmd_buf[4] = name_len;

                memcpy(&cmd_buf[5], filename, name_len);

                uint8_t checksum = cmd_checksum(cmd_buf, 5 + name_len);
                cmd_buf[5 + name_len] = checksum;

                service_cmd_send(src, cmd_buf, 6 + name_len);

                vTaskDelay(50 / portTICK_PERIOD_MS);
            }

            vPortFree(cmd_buf);
            break;
        }
        case BLE_FILM_TRANS_CH_FILE_DISPLAY : // 显示id对应的文件
        {
            if(len == 1)
            {
                uint8_t file_id = pdata[0];
                sys_logi(CMD_TAG, "Display file id: %d", file_id);
                service_film_display(file_id);
            }
            break;
        }
        case BLE_FILM_TRANS_CH_FILE_DISPLAY_GET : // 查询当前显示的文件id
        {
            uint32_t current_id = service_film_get_current_id();
            sys_logi(CMD_TAG, "Current display file id: %d", current_id);

            uint8_t cmd_buf[5];
            cmd_buf[0] = BLE_CMD_HEAD;
            cmd_buf[1] = BLE_FILM_TRANS_CH_FILE_DISPLAY_GET;
            cmd_buf[2] = 1;
            cmd_buf[3] = current_id & 0xFF;
            cmd_buf[4] = cmd_checksum(cmd_buf, 4);
            service_cmd_send(src, cmd_buf, sizeof(cmd_buf));
            break;
        }
        case BLE_FILM_TRANS_CH_OTA_LEN :
        {
            if(m_ota_trans_state == CMD_OTA_TRANS_IDLE && len == 4)
            {
                m_ota_trans_file_size = (pdata[0] << 24) | (pdata[1] << 16) | (pdata[2] << 8) | pdata[3];
                m_ota_trans_received = 0;
                m_ota_trans_state = CMD_OTA_TRANS_RECV_LEN;
                m_ota_trans_owner = src;
                sys_logi(CMD_TAG, "OTA file size: %d bytes", m_ota_trans_file_size);
                service_ota_start();
                service_set_length(m_ota_trans_file_size);
            }
            else
            {
                sys_logw(CMD_TAG, "Invalid state or length for OTA_LEN: state=%d, len=%d", m_ota_trans_state, len);
            }
            break;
        }
        case BLE_FILM_TRANS_CH_OTA_DATA :
        {
            if((m_ota_trans_state == CMD_OTA_TRANS_RECV_LEN || m_ota_trans_state == CMD_OTA_TRANS_RECV_DATA) &&
               m_ota_trans_owner == src && len > 0)
            {
                service_ota_write((uint8_t *)pdata, len);
                m_ota_trans_received += len;
                m_ota_trans_state = CMD_OTA_TRANS_RECV_DATA;
            }
            else
            {
                sys_logw(CMD_TAG, "Invalid state or length for OTA_DATA: state=%d, len=%d", m_ota_trans_state, len);
            }
            break;
        }
        case BLE_FILM_TRANS_CH_OTA_START :
        {
            if(m_ota_trans_state == CMD_OTA_TRANS_IDLE)
            {
                m_ota_trans_state = CMD_OTA_TRANS_STARTED;
                m_ota_trans_received = 0;
                m_ota_trans_owner = src;
                service_ota_start();
                sys_logi(CMD_TAG, "OTA started (without length)");
            }
            else
            {
                sys_logw(CMD_TAG, "Invalid state for OTA_START: %d", m_ota_trans_state);
            }
            break;
        }
        case BLE_FILM_TRANS_CH_OTA_STOP :
        {
            if(m_ota_trans_state == CMD_OTA_TRANS_RECV_DATA || m_ota_trans_state == CMD_OTA_TRANS_RECV_LEN)
            {
                service_ota_stop();
                sys_logi(CMD_TAG, "OTA stopped, received: %d/%d bytes", m_ota_trans_received, m_ota_trans_file_size);
                m_ota_trans_state = CMD_OTA_TRANS_STOPPED;
                m_ota_trans_owner = SERVICE_CMD_SRC_MAX;
            }
            else
            {
                sys_logw(CMD_TAG, "Invalid state for OTA_STOP: %d", m_ota_trans_state);
            }
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_MODE : // Film模式切换
        {
            if(len == 1 && (pdata[0] == 0 || pdata[0] == 1 || pdata[0] == 2))
            {
                uint8_t mode = pdata[0];
                sys_logi(CMD_TAG, "Set play mode: %d", mode);
                // 通过接口设置，同步刷新本地轮播定时器
                service_film_set_play_mode(mode);
            }
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_MODE_GET : // Film模式查询
        {
            sys_logi(CMD_TAG, "Current play mode: %d", g_service_param.film.play_mode);
            uint8_t cmd_buf[5];
            cmd_buf[0] = BLE_CMD_HEAD;
            cmd_buf[1] = BLE_FILM_TRANS_CH_CTRL_MODE_GET;
            cmd_buf[2] = 1;
            cmd_buf[3] = g_service_param.film.play_mode & 0xFF;
            cmd_buf[4] = cmd_checksum(cmd_buf, 4);
            service_cmd_send(src, cmd_buf, sizeof(cmd_buf));
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_RESET : // 重置
        {
            sys_logi(CMD_TAG, "Resetting parameters...");
            service_param_reset();
            vTaskDelay(100 / portTICK_PERIOD_MS);
            sys_reboot();
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_PWRREAD : // 读取电池电压
        {
            uint8_t cmd_buf[5];
            cmd_buf[0] = BLE_CMD_HEAD;
            cmd_buf[1] = BLE_FILM_TRANS_CH_CTRL_PWRREAD;
            cmd_buf[2] = 1;
            cmd_buf[3] = 100;
            cmd_buf[4] = cmd_checksum(cmd_buf, 4);
            service_cmd_send(src, cmd_buf, sizeof(cmd_buf));
            sys_logi(CMD_TAG, "Battery level: %d%%", 100);
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_REBOOT : // 重启
        {
            sys_logi(CMD_TAG, "Rebooting...");
            sys_reboot();
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_SLEEPONOFF : // 休眠模式开关
        {
            if(len == 1 && (pdata[0] == 0 || pdata[0] == 1))
            {
                uint8_t mode = pdata[0];
                sys_logi(CMD_TAG, "Set sleep mode: %s", mode ? "ON" : "OFF");
                g_service_param.sleep.sleep_mode = mode;
                service_param_save();
            }
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_SLEEPONOFF_GET : // 休眠模式开关查询
        {
            sys_logi(CMD_TAG, "Sleep mode: %s", g_service_param.sleep.sleep_mode ? "ON" : "OFF");
            uint8_t resp_buf[5];
            resp_buf[0] = BLE_CMD_HEAD;
            resp_buf[1] = BLE_FILM_TRANS_CH_CTRL_SLEEPONOFF_GET;
            resp_buf[2] = 1;
            resp_buf[3] = g_service_param.sleep.sleep_mode & 0xFF;
            resp_buf[4] = cmd_checksum(resp_buf, 4);
            service_cmd_send(src, resp_buf, sizeof(resp_buf));
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_SLEEPMODE : // 定时唤醒开关
        {
            if(len == 1 && (pdata[0] == 0 || pdata[0] == 1))
            {
                uint8_t auto_wake = pdata[0];
                sys_logi(CMD_TAG, "Set auto wake: %s", auto_wake ? "ON" : "OFF");
                g_service_param.sleep.sleep_auto = auto_wake;
                service_param_save();
            }
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_SLEEPMODE_GET : // 定时唤醒开关查询
        {
            sys_logi(CMD_TAG, "Auto wake: %s", g_service_param.sleep.sleep_auto ? "ON" : "OFF");
            uint8_t resp_buf[5];
            resp_buf[0] = BLE_CMD_HEAD;
            resp_buf[1] = BLE_FILM_TRANS_CH_CTRL_SLEEPMODE_GET;
            resp_buf[2] = 1;
            resp_buf[3] = g_service_param.sleep.sleep_auto & 0xFF;
            resp_buf[4] = cmd_checksum(resp_buf, 4);
            service_cmd_send(src, resp_buf, sizeof(resp_buf));
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_SLEEPMODE_TIME : // 定时唤醒时间（单位分钟）
        {
            if(len == 2)
            {
                uint16_t time_min = (pdata[0] << 8) | pdata[1];
                if(time_min >= 10 && time_min <= 2880)
                {
                    sys_logi(CMD_TAG, "Set sleep wake time: %d min", time_min);
                    g_service_param.sleep.sleep_time = time_min;
                    service_param_save();
                    // 本地轮播间隔复用该参数，刷新定时器周期
                    service_film_refresh_auto_timer();
                }
            }
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_SLEEPMODE_TIME_GET : // 定时唤醒时间查询（单位分钟）
        {
            sys_logi(CMD_TAG, "Sleep wake time: %d min", g_service_param.sleep.sleep_time);
            uint8_t resp_buf[6];
            resp_buf[0] = BLE_CMD_HEAD;
            resp_buf[1] = BLE_FILM_TRANS_CH_CTRL_SLEEPMODE_TIME_GET;
            resp_buf[2] = 2;
            resp_buf[3] = (g_service_param.sleep.sleep_time >> 8) & 0xFF;
            resp_buf[4] = g_service_param.sleep.sleep_time & 0xFF;
            resp_buf[5] = cmd_checksum(resp_buf, 5);
            service_cmd_send(src, resp_buf, sizeof(resp_buf));
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_SDRESET : // SD卡格式化
        {
            sys_logi(CMD_TAG, "SD card format requested");
            int ret = hal_sd_format();
            uint8_t resp_buf[5];
            resp_buf[0] = BLE_CMD_HEAD;
            resp_buf[1] = BLE_FILM_TRANS_CH_CTRL_SDRESET;
            resp_buf[2] = 1;
            resp_buf[3] = (ret == 0) ? 0 : 1;
            resp_buf[4] = cmd_checksum(resp_buf, 4);
            service_cmd_send(src, resp_buf, sizeof(resp_buf));
            if(ret == 0)
            {
                sys_logi(CMD_TAG, "SD card formatted, rebooting...");
                vTaskDelay(500 / portTICK_PERIOD_MS);
                sys_reboot();
            }
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_WIFI_ENABLE : // WiFi开关设置
        {
            if(len == 1 && (pdata[0] == 0 || pdata[0] == 1))
            {
                uint8_t enable = pdata[0];
                sys_logi(CMD_TAG, "Set WiFi enable: %s", enable ? "ON" : "OFF");
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
            sys_logi(CMD_TAG, "WiFi enable: %s", g_service_param.network.wifi_enable ? "ON" : "OFF");
            uint8_t resp_buf[5];
            resp_buf[0] = BLE_CMD_HEAD;
            resp_buf[1] = BLE_FILM_TRANS_CH_CTRL_WIFI_ENABLE_GET;
            resp_buf[2] = 1;
            resp_buf[3] = g_service_param.network.wifi_enable & 0xFF;
            resp_buf[4] = cmd_checksum(resp_buf, 4);
            service_cmd_send(src, resp_buf, sizeof(resp_buf));
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_WIFI_SSID : // WiFi SSID设置
        {
            if(len > 0 && len < sizeof(g_service_param.network.wifi_ssid))
            {
                memset(g_service_param.network.wifi_ssid, 0, sizeof(g_service_param.network.wifi_ssid));
                memcpy(g_service_param.network.wifi_ssid, pdata, len);
                sys_logi(CMD_TAG, "Set WiFi SSID: %s", g_service_param.network.wifi_ssid);
                service_param_save();
            }
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_WIFI_SSID_GET : // WiFi SSID查询
        {
            uint8_t ssid_len = strlen(g_service_param.network.wifi_ssid);
            sys_logi(CMD_TAG, "WiFi SSID: %s", g_service_param.network.wifi_ssid);
            uint8_t resp_ssid_len = 4 + ssid_len; // HEAD + CH + LEN + DATA + SUM
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
                resp_buf[3 + ssid_len] = cmd_checksum(resp_buf, 3 + ssid_len);
                service_cmd_send(src, resp_buf, resp_ssid_len);
                vPortFree(resp_buf);
            }
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_WIFI_PASSWORD : // WiFi 密码设置
        {
            if(len > 0 && len < sizeof(g_service_param.network.wifi_password))
            {
                memset(g_service_param.network.wifi_password, 0, sizeof(g_service_param.network.wifi_password));
                memcpy(g_service_param.network.wifi_password, pdata, len);
                sys_logi(CMD_TAG, "Set WiFi password");
                service_param_save();
            }
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_WIFI_PASSWORD_GET : // WiFi 密码查询
        {
            uint8_t pwd_len = strlen(g_service_param.network.wifi_password);
            sys_logi(CMD_TAG, "WiFi password query");
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
                resp_buf[3 + pwd_len] = cmd_checksum(resp_buf, 3 + pwd_len);
                service_cmd_send(src, resp_buf, resp_pwd_len);
                vPortFree(resp_buf);
            }
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_FILM_API_URL : // HTTP下载film文件的API地址设置
        {
            if(len > 0 && len < sizeof(g_service_param.network.film_api_url))
            {
                memset(g_service_param.network.film_api_url, 0, sizeof(g_service_param.network.film_api_url));
                memcpy(g_service_param.network.film_api_url, pdata, len);
                sys_logi(CMD_TAG, "Set film API URL: %s", g_service_param.network.film_api_url);
                service_param_save();
            }
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_FILM_API_URL_GET : // HTTP下载film文件的API地址查询
        {
            uint8_t url_len = strlen(g_service_param.network.film_api_url);
            sys_logi(CMD_TAG, "Film API URL: %s", g_service_param.network.film_api_url);
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
                resp_buf[3 + url_len] = cmd_checksum(resp_buf, 3 + url_len);
                service_cmd_send(src, resp_buf, resp_url_len);
                vPortFree(resp_buf);
            }
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_WIFI_CONNECT : // 连接WiFi
        {
            sys_logi(CMD_TAG, "WiFi connect requested");
            service_wifi_connect();
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_WIFI_DISCONNECT : // 断开WiFi连接
        {
            sys_logi(CMD_TAG, "WiFi disconnect requested");
            service_wifi_disconnect();
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_WIFI_CONNECT_GET : // 查询WiFi连接状态
        {
            uint8_t status = service_wifi_get_connect_status();
            sys_logi(CMD_TAG, "WiFi connect status: %s", status ? "Connected" : "Disconnected");
            uint8_t resp_buf[5];
            resp_buf[0] = BLE_CMD_HEAD;
            resp_buf[1] = BLE_FILM_TRANS_CH_CTRL_WIFI_CONNECT_GET;
            resp_buf[2] = 1;
            resp_buf[3] = status;
            resp_buf[4] = cmd_checksum(resp_buf, 4);
            service_cmd_send(src, resp_buf, sizeof(resp_buf));
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_WIFI_CLEAR : // 清除网络配置信息
        {
            sys_logi(CMD_TAG, "WiFi config clear requested");
            service_wifi_clear_config();
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_FILM_DOWNLOAD : // 开始下载film文件
        {
            sys_logi(CMD_TAG, "Film download requested");
            service_wifi_download_start();
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_FILM_DOWNLOAD_STATE : // 查询下载状态
        {
            wifi_download_state_t state = service_wifi_download_get_state();
            uint8_t progress = service_wifi_download_get_progress();
            sys_logi(CMD_TAG, "Download state: %d, progress: %d%%", state, progress);
            uint8_t resp_buf[6];
            resp_buf[0] = BLE_CMD_HEAD;
            resp_buf[1] = BLE_FILM_TRANS_CH_CTRL_FILM_DOWNLOAD_STATE;
            resp_buf[2] = 2;
            resp_buf[3] = (uint8_t)state;
            resp_buf[4] = progress;
            resp_buf[5] = cmd_checksum(resp_buf, 5);
            service_cmd_send(src, resp_buf, sizeof(resp_buf));
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_FILM_HEARTBEAT_URL : // HTTP心跳地址设置
        {
            if(len > 0 && len < sizeof(g_service_param.network.film_heartbeat_url))
            {
                memset(g_service_param.network.film_heartbeat_url, 0, sizeof(g_service_param.network.film_heartbeat_url));
                memcpy(g_service_param.network.film_heartbeat_url, pdata, len);
                sys_logi(CMD_TAG, "Set heartbeat URL: %s", g_service_param.network.film_heartbeat_url);
                service_param_save();
            }
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_FILM_HEARTBEAT_URL_GET : // HTTP心跳地址查询
        {
            uint8_t url_len = strlen(g_service_param.network.film_heartbeat_url);
            sys_logi(CMD_TAG, "Heartbeat URL: %s", g_service_param.network.film_heartbeat_url);
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
                resp_buf[3 + url_len] = cmd_checksum(resp_buf, 3 + url_len);
                service_cmd_send(src, resp_buf, resp_url_len);
                vPortFree(resp_buf);
            }
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_FILM_HEARTBEAT_INTERVAL : // 心跳间隔设置
        {
            if(len == 1 && pdata[0] >= 5 && pdata[0] <= 180)
            {
                g_service_param.network.film_heartbeat_interval = pdata[0];
                sys_logi(CMD_TAG, "Set heartbeat interval: %ds", pdata[0]);
                service_param_save();
            }
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_FILM_HEARTBEAT_INTERVAL_GET : // 心跳间隔查询
        {
            sys_logi(CMD_TAG, "Heartbeat interval: %ds", g_service_param.network.film_heartbeat_interval);
            uint8_t resp_buf[5];
            resp_buf[0] = BLE_CMD_HEAD;
            resp_buf[1] = BLE_FILM_TRANS_CH_CTRL_FILM_HEARTBEAT_INTERVAL_GET;
            resp_buf[2] = 1;
            resp_buf[3] = g_service_param.network.film_heartbeat_interval;
            resp_buf[4] = cmd_checksum(resp_buf, 4);
            service_cmd_send(src, resp_buf, sizeof(resp_buf));
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
            resp_buf[8] = cmd_checksum(resp_buf, 8);
            service_cmd_send(src, resp_buf, sizeof(resp_buf));
            sys_logi(CMD_TAG, "Screen info: panel_id=0x%02x, %d x %d", EPD_PANEL_ID, EPD_WIDTH, EPD_HEIGHT);
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_KEYBOARD_KEY_SET : // 设置单击/双击/长按 HID 键值
        {
            // 数据：事件(1) + 修饰键(1) + 键数(1) + 键码(n)
            if(len >= 3)
            {
                uint8_t event    = pdata[0];
                uint8_t modifier = pdata[1];
                uint8_t key_num  = pdata[2];
                ServiceKey_Def_t *pkey = (event < SERVICE_KEY_EVENT_MAX) ? m_key_map[event] : NULL;

                if(pkey != NULL && key_num <= SERVICE_KEY_MAX_NUM && len == (3 + key_num))
                {
                    pkey->modifier = modifier;
                    memset(pkey->keycode, 0, sizeof(pkey->keycode));
                    pkey->key_num = key_num;
                    if(key_num > 0)
                    {
                        memcpy(pkey->keycode, &pdata[3], key_num);
                    }
                    service_param_save();
                    sys_logi(CMD_TAG, "Set keyboard key: event=%d modifier=0x%02x num=%d", event, modifier, key_num);
                }
                else
                {
                    sys_logw(CMD_TAG, "Invalid keyboard key set: event=%d len=%d num=%d", event, len, key_num);
                }
            }
            break;
        }
        case BLE_FILM_TRANS_CH_CTRL_KEYBOARD_KEY_GET : // 查询单击/双击/长按 HID 键值
        {
            // 请求无数据返回三组；1字节指定事件则只返回该组
            uint8_t start = 0;
            uint8_t count = SERVICE_KEY_EVENT_MAX;
            if(len == 1 && pdata[0] < SERVICE_KEY_EVENT_MAX)
            {
                start = pdata[0];
                count = 1;
            }

            uint8_t resp_buf[3 + SERVICE_KEY_EVENT_MAX * (2 + SERVICE_KEY_MAX_NUM) + 1];
            uint8_t pos = 3;
            resp_buf[0] = BLE_CMD_HEAD;
            resp_buf[1] = BLE_FILM_TRANS_CH_CTRL_KEYBOARD_KEY_GET;

            for(uint8_t i = 0; i < count; i++)
            {
                ServiceKey_Def_t *pkey = m_key_map[start + i];
                resp_buf[pos++] = pkey->modifier;
                resp_buf[pos++] = pkey->key_num;
                memcpy(&resp_buf[pos], pkey->keycode, SERVICE_KEY_MAX_NUM);
                pos += SERVICE_KEY_MAX_NUM;
            }

            resp_buf[2] = pos - 3;
            resp_buf[pos] = cmd_checksum(resp_buf, pos);
            service_cmd_send(src, resp_buf, pos + 1);
            sys_logi(CMD_TAG, "Get keyboard key: start=%d count=%d", start, count);
            break;
        }
        default :
        {
            break;
        }
    }
}

/**
 * [cmd_checksum 和校验]
 * @param  arr [校验数据]
 * @param  len [校验长度]
 * @return     [校验值]
 */
static uint8_t cmd_checksum(const uint8_t *arr, int len)
{
    uint8_t sum = 0;
    for (int i = 0; i < len; i++)
    {
        sum += arr[i];
    }
    return sum;
}

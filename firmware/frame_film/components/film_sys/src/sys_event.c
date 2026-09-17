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
 * FileName : /film_sys/src/sys_event.c
 * Author: Kiritro  Version: v0.1  Date: 2026/9/9
 * Description: 全局事件总线：固定订阅表 + 同步扇出（服务发布 / app 订阅）
 * ChangeLog: Change Notes
 *
 *********************************************************************/

/*********************************************************************
 * INCLUDES
 */
#include <string.h>

#include "sys_event.h"
#include "sys_log.h"

/*********************************************************************
 * MACROS
 */
#define SYS_EVENT_TAG           "sys_event"

/*********************************************************************
* TYPEDEFS
*/
/**
 * @brief 订阅槽
 */
typedef struct {
    uint8_t         used;
    sys_event_id_t  id;
    sys_event_cb_t  cb;
    void           *ctx;
} sys_event_sub_t;

/*********************************************************************
 * CONSTANTS
 */


/*********************************************************************
 * LOCAL VARIABLES
 */
static sys_event_sub_t m_subs[SYS_EVENT_MAX_SUBSCRIBERS] = {0};

/*********************************************************************
 * GLOBAL VARIABLES
 */


/*********************************************************************
 * LOCAL FUNCTIONS
 */
static int sys_event_publish_internal(sys_event_id_t id, const void *payload, uint16_t len, uint8_t from_isr);

/*********************************************************************
 * GLOBAL FUNCTIONS
 */

/**
 * @brief 初始化事件总线（清空订阅表）
 */
int sys_event_init(void)
{
    memset(m_subs, 0, sizeof(m_subs));
    sys_logi(SYS_EVENT_TAG, "event bus init, slots=%d", SYS_EVENT_MAX_SUBSCRIBERS);
    return 0;
}

/**
 * @brief 订阅事件
 * @return 订阅句柄(>0)，失败返回 0
 * @note 订阅表在初始化阶段构建，发布阶段只读；回调须短小非阻塞
 */
int sys_event_subscribe(sys_event_id_t id, sys_event_cb_t cb, void *ctx)
{
    if(id == SYS_EVT_NONE || cb == NULL)
    {
        return 0;
    }

    for(int i = 0; i < SYS_EVENT_MAX_SUBSCRIBERS; i++)
    {
        if(m_subs[i].used == 0)
        {
            m_subs[i].used = 1;
            m_subs[i].id   = id;
            m_subs[i].cb   = cb;
            m_subs[i].ctx  = ctx;
            sys_logi(SYS_EVENT_TAG, "subscribe id=0x%04X handle=%d", (unsigned)id, i + 1);
            return i + 1;
        }
    }

    sys_loge(SYS_EVENT_TAG, "subscribe id=0x%04X full!", (unsigned)id);
    return 0;
}

/**
 * @brief 取消订阅
 */
void sys_event_unsubscribe(int handle)
{
    int idx = handle - 1;
    if(idx < 0 || idx >= SYS_EVENT_MAX_SUBSCRIBERS || m_subs[idx].used == 0)
    {
        return;
    }
    m_subs[idx].used = 0;
    m_subs[idx].cb   = NULL;
    m_subs[idx].ctx  = NULL;
}

/**
 * @brief 发布事件（任务上下文）：同步扇出给所有订阅者
 * @param payload 值指针，内部 memcpy 拷贝；可为 NULL（len 应为 0）
 * @return 实际通知的订阅者数量，<0 表示参数错误
 */
int sys_event_publish(sys_event_id_t id, const void *payload, uint16_t len)
{
    return sys_event_publish_internal(id, payload, len, 0);
}

/**
 * @brief 发布事件（ISR 上下文）
 * @note 订阅回调同样在 ISR 上下文执行，必须 ISR-safe（如使用 xQueueSendFromISR）
 */
int sys_event_publish_isr(sys_event_id_t id, const void *payload, uint16_t len)
{
    return sys_event_publish_internal(id, payload, len, 1);
}

static int sys_event_publish_internal(sys_event_id_t id, const void *payload, uint16_t len, uint8_t from_isr)
{
    if(id == SYS_EVT_NONE)
    {
        return -1;
    }
    if(len > SYS_EVENT_PAYLOAD_MAX)
    {
        sys_logw(SYS_EVENT_TAG, "id=0x%04X payload len=%u clamp to %d", (unsigned)id, (unsigned)len, SYS_EVENT_PAYLOAD_MAX);
        len = SYS_EVENT_PAYLOAD_MAX;
    }

    sys_event_t e;
    e.id       = (uint16_t)id;
    e.len      = len;
    e.from_isr = from_isr;
    memset(e.payload, 0, sizeof(e.payload));
    if(payload != NULL && len > 0)
    {
        memcpy(e.payload, payload, len);
    }

    int notified = 0;
    for(int i = 0; i < SYS_EVENT_MAX_SUBSCRIBERS; i++)
    {
        if(m_subs[i].used == 0 || m_subs[i].cb == NULL)
        {
            continue;
        }
        if(m_subs[i].id != id && m_subs[i].id != SYS_EVT_ANY)
        {
            continue;
        }
        m_subs[i].cb(&e, m_subs[i].ctx);
        notified++;
    }

    return notified;
}

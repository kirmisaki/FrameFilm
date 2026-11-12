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
 * FileName : /film_app/core/app_boot_cfg.c
 * Author: Kiritro  Version: v0.1  Date: 2026/9/21
 * Description: 开机行为配置（是否画开机画面 / 开机直达哪个 app），持久化在 app7 槽位
 * ChangeLog: Change Notes
 *
 *********************************************************************/

/*********************************************************************
 * INCLUDES
 */
#include <string.h>
#include <stdatomic.h>

#include "sys_log.h"
#include "service_param.h"
#include "hal_input.h"

#include "app_manager.h"
#include "app_boot_cfg.h"

/*********************************************************************
 * MACROS
 */
#define APP_BOOT_CFG_TAG        "app_bootcfg"

/*********************************************************************
 * LOCAL VARIABLES
 */
static app_boot_cfg_t m_cfg;
static atomic_uchar m_language;

/* 默认值：显示开机画面 + 落主菜单 —— 与加这两个参数之前的行为完全一致。
   即"不配置就不会有任何变化"，避免升级固件后老设备开机行为突变。 */
static const app_boot_cfg_t m_cfg_default = {
    .boot_page = APP_BOOT_PAGE_SHOW,
    .start_app = APP_START_MENU,
    .language = APP_LANGUAGE_ZH_CN,
    .screenshot = APP_SCREENSHOT_OFF,
};

/*********************************************************************
 * LOCAL FUNCTIONS
 */

/**
 * @brief 当前 app 记录是否可用（越界 / 未注册都算不可用）
 */
static int last_app_valid(void)
{
    int last = service_param_app_current_get();

    if(last < 0 || last >= (int)APP_ID_MAX)
    {
        return 0;
    }
    return app_manager_is_registered((app_id_t)last);
}

/*********************************************************************
 * GLOBAL FUNCTIONS
 */

void app_boot_cfg_init(void)
{
    uint8_t legacy[2];
    /* v2 布局（无截图开关） */
    struct {
        uint8_t boot_page;
        uint8_t start_app;
        uint8_t language;
    } v2;
    memcpy(&m_cfg, &m_cfg_default, sizeof(m_cfg));

    if(service_param_app_load(SERVICE_PARAM_APP_ID_BOOT_CFG, &m_cfg,
                              (uint16_t)sizeof(m_cfg), APP_BOOT_CFG_VER) != 0)
    {
        if(service_param_app_load(SERVICE_PARAM_APP_ID_BOOT_CFG, &v2,
                                  (uint16_t)sizeof(v2), 2) == 0)
        {
            m_cfg.boot_page = v2.boot_page;
            m_cfg.start_app = v2.start_app;
            m_cfg.language = v2.language;
        }
        /* v1 was two bytes. Keep both boot choices when adding language. */
        else if(service_param_app_load(SERVICE_PARAM_APP_ID_BOOT_CFG, legacy,
                                       sizeof(legacy), 1) == 0)
        {
            m_cfg.boot_page = legacy[0];
            m_cfg.start_app = legacy[1];
        }
    }

    /* 值域兜底：NVS 里可能是别的版本写进去的/被写坏的值 */
    if(m_cfg.boot_page > APP_BOOT_PAGE_SKIP)
    {
        m_cfg.boot_page = APP_BOOT_PAGE_SHOW;
    }
    if(m_cfg.start_app >= APP_START_NUM)
    {
        m_cfg.start_app = APP_START_MENU;
    }
    if(m_cfg.language > APP_LANGUAGE_EN)
    {
        m_cfg.language = APP_LANGUAGE_ZH_CN;
    }
    if(m_cfg.screenshot > APP_SCREENSHOT_ON)
    {
        m_cfg.screenshot = APP_SCREENSHOT_OFF;
    }
    atomic_store(&m_language, m_cfg.language);

    /* 截图关闭时上键不认双击（单击不用再等双击窗口） */
    hal_input_set_up_double_click(m_cfg.screenshot == APP_SCREENSHOT_ON);

    sys_logi(APP_BOOT_CFG_TAG, "loaded: boot_page=%u start=%u language=%u shot=%u",
             (unsigned)m_cfg.boot_page, (unsigned)m_cfg.start_app,
             (unsigned)m_cfg.language, (unsigned)m_cfg.screenshot);
}

const app_boot_cfg_t *app_boot_cfg_get(void)
{
    return &m_cfg;
}

void app_boot_cfg_set(uint8_t boot_page, uint8_t start_app)
{
    if(boot_page > APP_BOOT_PAGE_SKIP || start_app >= APP_START_NUM)
    {
        sys_logw(APP_BOOT_CFG_TAG, "set rejected: boot_page=%u start=%u",
                 (unsigned)boot_page, (unsigned)start_app);
        return;
    }
    if(m_cfg.boot_page == boot_page && m_cfg.start_app == start_app)
    {
        return;   // 没变化就不写 NVS（擦写寿命）
    }

    m_cfg.boot_page = boot_page;
    m_cfg.start_app = start_app;
    (void)service_param_app_save(SERVICE_PARAM_APP_ID_BOOT_CFG, &m_cfg,
                                (uint16_t)sizeof(m_cfg), APP_BOOT_CFG_VER);

    sys_logi(APP_BOOT_CFG_TAG, "saved: boot_page=%u start=%u language=%u",
             (unsigned)boot_page, (unsigned)start_app, (unsigned)m_cfg.language);
}

uint8_t app_language_get(void)
{
    return atomic_load(&m_language);
}

void app_language_set(uint8_t language)
{
    if(language > APP_LANGUAGE_EN || m_cfg.language == language)
    {
        return;
    }
    m_cfg.language = language;
    atomic_store(&m_language, language);
    (void)service_param_app_save(SERVICE_PARAM_APP_ID_BOOT_CFG, &m_cfg,
                                (uint16_t)sizeof(m_cfg), APP_BOOT_CFG_VER);
    sys_logi(APP_BOOT_CFG_TAG, "saved: boot_page=%u start=%u language=%u shot=%u",
             (unsigned)m_cfg.boot_page, (unsigned)m_cfg.start_app,
             (unsigned)m_cfg.language, (unsigned)m_cfg.screenshot);
}

uint8_t app_screenshot_enabled(void)
{
    return m_cfg.screenshot ? APP_SCREENSHOT_ON : APP_SCREENSHOT_OFF;
}

void app_screenshot_set(uint8_t on)
{
    uint8_t v = on ? APP_SCREENSHOT_ON : APP_SCREENSHOT_OFF;

    if(m_cfg.screenshot == v)
    {
        return;
    }
    m_cfg.screenshot = v;
    hal_input_set_up_double_click(v == APP_SCREENSHOT_ON);
    (void)service_param_app_save(SERVICE_PARAM_APP_ID_BOOT_CFG, &m_cfg,
                                (uint16_t)sizeof(m_cfg), APP_BOOT_CFG_VER);
    sys_logi(APP_BOOT_CFG_TAG, "screenshot=%u", (unsigned)v);
}

app_id_t app_boot_cfg_resolve_target(void)
{
    app_id_t target;

    switch(m_cfg.start_app)
    {
    case APP_START_MENU:
        target = APP_ID_MENU;
        break;

    case APP_START_LAST:
        /* 上次的 app：越界/未注册都回落图片，与旧的启动路径同一套判据 */
        target = last_app_valid() ? (app_id_t)service_param_app_current_get() : APP_ID_IMAGE;
        break;

    case APP_START_TEMPLATE:  target = APP_ID_TEMPLATE;  break;
    case APP_START_CLOCK:     target = APP_ID_CLOCK;     break;
    case APP_START_ANIMATION: target = APP_ID_ANIMATION; break;
    case APP_START_SETTINGS:  target = APP_ID_SETTINGS;  break;
    case APP_START_PASS:      target = APP_ID_PASS;      break;
    case APP_START_WALLET:    target = APP_ID_WALLET;    break;

    case APP_START_IMAGE:
    default:
        target = APP_ID_IMAGE;
        break;
    }

    /* 目标被当前模式裁掉（如简易模式没有菜单 / UI 未启用没有设置页）→ 回落图片 */
    if(!app_manager_is_registered(target))
    {
        sys_logw(APP_BOOT_CFG_TAG, "start target %d not registered, fallback to image",
                 (int)target);
        target = APP_ID_IMAGE;
    }
    return target;
}

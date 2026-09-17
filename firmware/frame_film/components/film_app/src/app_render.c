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
 * FileName : /film_app/src/app_render.c
 * Author: Kiritro  Version: v0.1  Date: 2026/9/9
 * Description: App 层渲染/显示抽象（能力位 / 完整帧 / 8bpp / MonoFast / 切换封面）
 * ChangeLog: Change Notes
 *
 *********************************************************************/

/*********************************************************************
 * INCLUDES
 */
#include <stdio.h>
#include <string.h>

#include "sys_log.h"
#include "hal_epd.h"
#include "app_render.h"

#include "esp_heap_caps.h"

/*********************************************************************
 * MACROS
 */
#define APP_RENDER_TAG  "App_Render"

// 具备 app 封面菜单渲染能力的屏幕：3.7" 720×480（EPD_PANEL_ID 0x02）
// 菜单需要整屏绘制封面并靠 MonoFast 快速翻页，其余屏幕降级为简易切换模式。
#define APP_SWITCH_PANEL_ID     (0x02)

// 应用封面目录：TF 卡 /sdcard/app/<appname>/cover.film
#define APP_COVER_BASE_DIR      "/sdcard/app"

// .film 文件头
#define FILM_HDR_SIZE           (32)
#define FILM_HDR_OFFSET_FORMAT  (0x09)

/*********************************************************************
 * TYPEDEFS
*/
/**
 * @brief 封面缓存（单槽）：避免每次 UP/DOWN 都 fopen + malloc 整屏封面
 */
typedef struct {
    char name[24];      // 已缓存的 app 名（空串表示无缓存）
    uint8_t *buf;       // 封面 .film 数据（含 32B 头），PSRAM
    long size;          // 数据长度（字节）
} app_cover_cache_t;

/*********************************************************************
 * CONSTANTS
 */

/*********************************************************************
 * LOCAL VARIABLES
 */
static app_cover_cache_t m_cover_cache = {0};

/*********************************************************************
 * GLOBAL VARIABLES
 */

/*********************************************************************
 * LOCAL FUNCTIONS
 */

/*********************************************************************
 * GLOBAL FUNCTIONS
 */

uint32_t app_render_get_capabilities(void)
{
    return hal_epd_get_capabilities();
}

int app_render_has_cover_menu(void)
{
    // 封面菜单需同时满足：3.7" 屏（EPD_PANEL_ID 0x02，具备 MonoFast 快刷）+ 上/下导航与确认键
    if(EPD_PANEL_ID != APP_SWITCH_PANEL_ID)
    {
        return 0;
    }
    return SYS_INPUT_HAS_NAV_ENTER ? 1 : 0;
}

int app_render_has_8bpp(void)
{
    return (app_render_get_capabilities() & EPD_CAP_8BPP) != 0;
}

int app_render_has_monofast(void)
{
    return (app_render_get_capabilities() & EPD_CAP_MONOFAST) != 0;
}

void app_render_display_full(const unsigned char *filmData)
{
    if(filmData == NULL)
    {
        sys_loge(APP_RENDER_TAG, "display_full: NULL data");
        return;
    }

    /* app 层是 .film Format 的唯一分派点：
     * 0x01 MonoFast / 0x02 ColorQual / 0x03 ColorFast 走对应驱动，
     * 其余（v1 4bpp）交给 hal_epd_display_film。
     * 分派前按能力位判定，避免在不支持的屏上调用空实现。 */
    uint8_t format = filmData[FILM_HDR_OFFSET_FORMAT];
    uint32_t caps = app_render_get_capabilities();

    /* MonoFast 单独处理，不能走 hal_epd_display_init()/hal_epd_pwroff()：
     * 前者会硬复位面板（控制器里缓存的上一帧是差分基准），后者内部是 DSLP 深睡，
     * 两者都会让下一次 mono 刷新退化成整屏刷新（翻封面/时钟每帧整屏闪）。
     * 该路径的 spectra 会话与电源（PON/REF/POF）由 hal_epd_display_mono 自行管理，
     * 面板不会一直带电。 */
    if(format == 0x01)
    {
        if(caps & EPD_CAP_MONOFAST)
        {
            hal_epd_display_mono(filmData + FILM_HDR_SIZE);
        }
        else
        {
            sys_logw(APP_RENDER_TAG, "monofast film but panel unsupported");
        }
        return;
    }

    hal_epd_display_init();
    switch(format)
    {
    case 0x02:  // v2 ColorQual（3 相）
        if(caps & EPD_CAP_8BPP)
        {
            hal_epd_display_8bpp_mode(filmData + FILM_HDR_SIZE, 1);
        }
        else
        {
            sys_logw(APP_RENDER_TAG, "8bpp film but panel unsupported");
        }
        break;
    case 0x03:  // v2 ColorFast（2 相）
        if(caps & EPD_CAP_8BPP)
        {
            hal_epd_display_8bpp_mode(filmData + FILM_HDR_SIZE, 0);
        }
        else
        {
            sys_logw(APP_RENDER_TAG, "8bpp film but panel unsupported");
        }
        break;
    default:    // v1 4bpp 单帧
        hal_epd_display_film(filmData);
        break;
    }
    hal_epd_pwroff();
}

void app_render_display_8bpp(const unsigned char *index8Data, uint8_t mode)
{
    if(index8Data == NULL)
    {
        sys_loge(APP_RENDER_TAG, "display_8bpp: NULL data");
        return;
    }

    hal_epd_display_init();
    hal_epd_display_8bpp_mode(index8Data, mode);
    hal_epd_pwroff();
}

void app_render_display_mono(const unsigned char *mono_bitmap)
{
    if(mono_bitmap == NULL)
    {
        sys_loge(APP_RENDER_TAG, "display_mono: NULL data");
        return;
    }

    /* 同 display_full 的 MonoFast 分支：不能复位/深睡，否则时钟每秒一帧都会整屏闪。
       spectra 会话与电源由 hal_epd_display_mono 自行管理。 */
    hal_epd_display_mono(mono_bitmap);
}

void app_render_clear(void)
{
    hal_epd_display_init();
    hal_epd_display_white();
    hal_epd_pwroff();
}

void app_render_switch_menu(const char *app_name)
{
    if(app_name == NULL || app_name[0] == '\0')
    {
        sys_loge(APP_RENDER_TAG, "switch_menu: invalid app name");
        return;
    }

    /* 命中缓存：直接重绘，省去 fopen + malloc 整屏封面 */
    if(m_cover_cache.buf != NULL && strcmp(m_cover_cache.name, app_name) == 0)
    {
        app_render_display_full(m_cover_cache.buf);
        return;
    }

    char path[64] = {0};
    snprintf(path, sizeof(path), "%s/%s/cover.film", APP_COVER_BASE_DIR, app_name);

    FILE *f = fopen(path, "rb");
    if(f == NULL)
    {
        // 封面缺失：降级为日志提示，不绘制、不阻塞切换
        sys_logw(APP_RENDER_TAG, "app cover missing: %s", path);
        return;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if(size <= 0)
    {
        sys_logw(APP_RENDER_TAG, "app cover empty: %s", path);
        fclose(f);
        return;
    }

    uint8_t *buf = (uint8_t*)heap_caps_malloc((size_t)size, MALLOC_CAP_SPIRAM);
    if(buf == NULL)
    {
        sys_loge(APP_RENDER_TAG, "allocate cover buffer failed, size=%ld", size);
        fclose(f);
        return;
    }

    size_t rd = fread(buf, 1, (size_t)size, f);
    fclose(f);

    if(rd != (size_t)size)
    {
        sys_logw(APP_RENDER_TAG, "cover read mismatch: %s, read %d/%ld", path, (int)rd, size);
        heap_caps_free(buf);
        return;
    }

    /* 替换缓存（旧封面先释放，避免 PSRAM 泄漏） */
    if(m_cover_cache.buf != NULL)
    {
        heap_caps_free(m_cover_cache.buf);
    }
    m_cover_cache.buf = buf;
    m_cover_cache.size = size;
    snprintf(m_cover_cache.name, sizeof(m_cover_cache.name), "%s", app_name);

    sys_logi(APP_RENDER_TAG, "switch menu cover: %s", path);
    app_render_display_full(buf);
}

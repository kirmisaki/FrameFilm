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
 * FileName : /film_app/core/app_screenshot.c
 * Author: Kiritro  Version: v0.1  Date: 2026/11/12
 * Description: 上键双击截图：UI mono 帧 → /sdcard/screenshot/shot_*.film
 * ChangeLog: Change Notes
 *
 *********************************************************************/

/*********************************************************************
 * INCLUDES
 */
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

#include "esp_heap_caps.h"
#include "esp_timer.h"

#include "sys_log.h"
#include "hal_sd.h"
#include "service_file.h"
#include "ui_conf.h"
#include "ui_display.h"

#include "app_screenshot.h"

/*********************************************************************
 * MACROS
 */
#define APP_SCREENSHOT_TAG      "app_shot"

#define SHOT_HEADER_SIZE        (32)
#define SHOT_FORMAT_MONO        (0x01)   /* .film v2 MonoFast */

/*********************************************************************
 * LOCAL FUNCTIONS
 */

/**
 * @brief 生成不重名的截图路径
 *
 * 时间未同步（年份异常）时退化为开机秒数命名；两种命名都按"已存在就加序号"
 * 兜底，避免同一秒连拍或重启后覆盖。
 */
static int shot_make_path(char *path, size_t size)
{
    char base[48];
    time_t now = time(NULL);
    struct tm tmv;

    if(localtime_r(&now, &tmv) != NULL && (tmv.tm_year + 1900) >= 2024)
    {
        snprintf(base, sizeof(base), "shot_%04d%02d%02d_%02d%02d%02d",
                 tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
                 tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
    }
    else
    {
        snprintf(base, sizeof(base), "shot_%08lu",
                 (unsigned long)(esp_timer_get_time() / 1000000));
    }

    for(int n = 0; n < 1000; n++)
    {
        int len = (n == 0)
                ? snprintf(path, size, "%s/%s.film", SCREENSHOT_DIR, base)
                : snprintf(path, size, "%s/%s_%d.film", SCREENSHOT_DIR, base, n);
        if(len < 0 || (size_t)len >= size)
        {
            return -1;
        }

        struct stat st;
        if(stat(path, &st) != 0)
        {
            return 0;
        }
    }
    return -1;
}

/**
 * @brief 组 32 字节 .film v2 MonoFast 文件头（多字节字段为小端）
 */
static void shot_build_header(uint8_t *hdr, uint32_t body_size)
{
    memset(hdr, 0, SHOT_HEADER_SIZE);

    hdr[0x00] = (uint8_t)(body_size);
    hdr[0x01] = (uint8_t)(body_size >> 8);
    hdr[0x02] = (uint8_t)(body_size >> 16);
    hdr[0x03] = (uint8_t)(body_size >> 24);

    hdr[0x04] = (uint8_t)(UI_MONO_W);
    hdr[0x05] = (uint8_t)(UI_MONO_W >> 8);
    hdr[0x06] = (uint8_t)(UI_MONO_H);
    hdr[0x07] = (uint8_t)(UI_MONO_H >> 8);

    hdr[0x08] = 2;              /* ColorCount：黑白 */
    hdr[0x09] = SHOT_FORMAT_MONO;
    hdr[0x0A] = 1;              /* FrameCount 低字节 = 1（单帧） */
}

/*********************************************************************
 * GLOBAL FUNCTIONS
 */

int app_screenshot_capture(void)
{
    if(hal_sd_get_status() != SD_MOUNT)
    {
        sys_logw(APP_SCREENSHOT_TAG, "SD not mounted");
        return -1;
    }

    uint8_t *body = (uint8_t *)heap_caps_malloc(UI_MONO_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if(body == NULL)
    {
        body = (uint8_t *)heap_caps_malloc(UI_MONO_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if(body == NULL)
    {
        sys_loge(APP_SCREENSHOT_TAG, "no memory for %u byte snapshot", (unsigned)UI_MONO_BYTES);
        return -2;
    }

    int rc = ui_display_snapshot_mono(body, UI_MONO_BYTES);
    if(rc != 0)
    {
        heap_caps_free(body);
        /* -1：没有 UI 页面（图片/动图等直绘层）；-2：彩色卡页面 */
        sys_logw(APP_SCREENSHOT_TAG, "snapshot unavailable (%d)", rc);
        return -3;
    }

    struct stat st;
    if(mkdir(SCREENSHOT_DIR, 0777) != 0 && stat(SCREENSHOT_DIR, &st) != 0)
    {
        heap_caps_free(body);
        sys_loge(APP_SCREENSHOT_TAG, "mkdir %s failed (errno=%d)", SCREENSHOT_DIR, errno);
        return -4;
    }

    char path[160];
    if(shot_make_path(path, sizeof(path)) != 0)
    {
        heap_caps_free(body);
        sys_loge(APP_SCREENSHOT_TAG, "no free screenshot name");
        return -4;
    }

    uint8_t hdr[SHOT_HEADER_SIZE];
    shot_build_header(hdr, UI_MONO_BYTES);

    FILE *fp = fopen(path, "wb");
    if(fp == NULL)
    {
        heap_caps_free(body);
        sys_loge(APP_SCREENSHOT_TAG, "open %s failed (errno=%d)", path, errno);
        return -4;
    }

    size_t wrote = fwrite(hdr, 1, sizeof(hdr), fp);
    if(wrote == sizeof(hdr))
    {
        wrote = fwrite(body, 1, UI_MONO_BYTES, fp);
    }
    int saved = (wrote == UI_MONO_BYTES);
    fclose(fp);
    heap_caps_free(body);

    if(!saved)
    {
        remove(path);
        sys_loge(APP_SCREENSHOT_TAG, "write %s failed", path);
        return -4;
    }

    sys_logi(APP_SCREENSHOT_TAG, "saved %s (%u bytes)", path,
             (unsigned)(SHOT_HEADER_SIZE + UI_MONO_BYTES));
    return 0;
}

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
 * FileName : /film_ui/src/ui_assets.c
 * Author: Kiritro  Version: v0.1  Date: 2026/9/17
 * Description: UI 资源层：从 SD 卡加载可替换图标（1bpp），缺失/非法时回退内置默认图
 * ChangeLog: Change Notes
 *
 *********************************************************************/

/*********************************************************************
 * INCLUDES
 */
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"

#include "sys_log.h"

#include "ui_conf.h"
#include "ui_assets.h"

#if (SYS_UI_ENABLE == 0)

/*********************************************************************
 * UI 层被裁剪：空实现，且不引用任何 lv_* 符号
 *********************************************************************/

const void *ui_assets_icon(ui_icon_id_t id)                 { (void)id; return NULL; }
const void *ui_assets_icon_inverted(ui_icon_id_t id)        { (void)id; return NULL; }
const void *ui_assets_badge(void)                           { return NULL; }
void ui_assets_release(void)                                { }

#else /* SYS_UI_ENABLE */

#include "lvgl.h"

#include "ui_defaults.h"

/*********************************************************************
 * MACROS
 */
#define UI_ASSETS_TAG       "ui_asset"

/* FFUI 容器头（与 tools/ui-assets/gen_ui_assets.py 严格一致）：
 *   0x00 magic "FFUI" | 0x04 version | 0x05 format | 0x06 width(u16 LE)
 *   0x08 height(u16 LE) | 0x0A reserve(6B) | 0x10 载荷 w*h/8 字节
 * 载荷为 1bpp：1=黑 0=白，每字节 8 像素、MSB 在左。 */
#define UI_ASSET_HDR_LEN    (16)
#define UI_ASSET_VERSION    (1)
#define UI_ASSET_FMT_1BPP   (0x01)
#define UI_ASSET_BIN_NAME   "icon.bin"
#define UI_ASSET_BADGE_NAME "badge.bin"

#define UI_ASSET_MAX_ROW    (UI_BADGE_W / 8)   /* 最大行字节数（304/8 = 38） */

/*********************************************************************
* TYPEDEFS
*/
/**
 * @brief 资源槽位
 */
typedef struct {
    const char *code;        // SD 子目录名
    uint16_t w;              // 期望宽（尺寸必须精确匹配，固件不缩放）
    uint16_t h;              // 期望高
    const uint8_t *def;      // 内置默认图（1bpp 原始位图）
    const void *src;         // 解析结果（lv_image_dsc_t*）；NULL = 尚未解析
    lv_image_dsc_t dsc;      // 展开后的 L8 图描述
    uint8_t *l8;             // L8 缓冲
    const void *src_inv;     // 反色版（菜单选中项用）
    lv_image_dsc_t dsc_inv;
    uint8_t *l8_inv;
} ui_asset_slot_t;

/*********************************************************************
 * LOCAL VARIABLES
 */
static ui_asset_slot_t m_icons[UI_ICON_NUM] = {
    { .code = "image",     .w = UI_ICON_W, .h = UI_ICON_H, .def = ui_def_icon_image     },
    { .code = "pass",      .w = UI_ICON_W, .h = UI_ICON_H, .def = ui_def_icon_pass      },
    { .code = "wallet",    .w = UI_ICON_W, .h = UI_ICON_H, .def = ui_def_icon_wallet    },
    { .code = "template",  .w = UI_ICON_W, .h = UI_ICON_H, .def = ui_def_icon_template  },
    { .code = "clock",     .w = UI_ICON_W, .h = UI_ICON_H, .def = ui_def_icon_clock     },
    { .code = "animation", .w = UI_ICON_W, .h = UI_ICON_H, .def = ui_def_icon_animation },
    { .code = "settings",  .w = UI_ICON_W, .h = UI_ICON_H, .def = ui_def_icon_settings  },
};

static ui_asset_slot_t m_badge = {
    .code = "_ui", .w = UI_BADGE_W, .h = UI_BADGE_H, .def = ui_def_badge,
};

/*********************************************************************
 * LOCAL FUNCTIONS
 */

/**
 * @brief 1bpp 位图展开成 L8
 *
 * L8 用亮度直接表达黑白（0x00 = 黑、0xFF = 白），不涉及调色板索引方向，
 * 因此显示结果只取决于位图本身，规避了 I1 图像调色板语义的不确定性。
 */
static void ui_asset_expand(const uint8_t *bmp, uint16_t w, uint16_t h, uint8_t *l8)
{
    uint32_t total = (uint32_t)w * (uint32_t)h;
    uint32_t i;

    for(i = 0; i < total; i++)
    {
        uint8_t bit = (uint8_t)((bmp[i >> 3] >> (7u - (i & 7u))) & 1u);
        l8[i] = bit ? 0x00 : 0xFF;
    }
}

/**
 * @brief 从 SD 卡读取资源并直接逐行展开进 L8 缓冲
 *
 * 逐行读入，避免再额外申请一块 1bpp 临时缓冲（徽章有 3.8KB）。
 *
 * @return 0 成功；负值失败（调用方据此回退内置默认图）
 */
static int ui_asset_load_sd(ui_asset_slot_t *s, const char *fname)
{
    char path[96];
    uint8_t hdr[UI_ASSET_HDR_LEN];
    uint8_t row[UI_ASSET_MAX_ROW];
    uint16_t w;
    uint16_t h;
    uint16_t y;
    uint32_t stride;
    FILE *f;

    if(snprintf(path, sizeof(path), UI_ASSET_ROOT "/%s/%s", s->code, fname) >= (int)sizeof(path))
    {
        return -1;
    }

    f = fopen(path, "rb");
    if(f == NULL)
    {
        return -2;
    }

    if(fread(hdr, 1, UI_ASSET_HDR_LEN, f) != UI_ASSET_HDR_LEN)
    {
        fclose(f);
        return -3;
    }
    if(hdr[0] != 'F' || hdr[1] != 'F' || hdr[2] != 'U' || hdr[3] != 'I')
    {
        sys_logw(UI_ASSETS_TAG, "bad magic: %s", path);
        fclose(f);
        return -4;
    }
    if(hdr[4] != UI_ASSET_VERSION || hdr[5] != UI_ASSET_FMT_1BPP)
    {
        sys_logw(UI_ASSETS_TAG, "unsupported ver/fmt: %s", path);
        fclose(f);
        return -5;
    }

    w = (uint16_t)(hdr[6] | ((uint16_t)hdr[7] << 8));
    h = (uint16_t)(hdr[8] | ((uint16_t)hdr[9] << 8));
    if(w != s->w || h != s->h)
    {
        sys_logw(UI_ASSETS_TAG, "size mismatch: %s is %ux%u, expect %ux%u",
                 path, (unsigned)w, (unsigned)h, (unsigned)s->w, (unsigned)s->h);
        fclose(f);
        return -6;
    }

    stride = (uint32_t)s->w / 8u;
    for(y = 0; y < s->h; y++)
    {
        uint32_t x;

        if(fread(row, 1, stride, f) != stride)
        {
            sys_logw(UI_ASSETS_TAG, "truncated payload: %s", path);
            fclose(f);
            return -7;
        }
        for(x = 0; x < s->w; x++)
        {
            uint8_t bit = (uint8_t)((row[x >> 3] >> (7u - (x & 7u))) & 1u);
            s->l8[(uint32_t)y * s->w + x] = bit ? 0x00 : 0xFF;
        }
    }

    fclose(f);
    return 0;
}

/**
 * @brief 解析槽位：申请 L8 缓冲 -> 读 SD（失败则内置默认图）-> 组装图像描述
 */
static const void *ui_asset_resolve(ui_asset_slot_t *s, const char *fname)
{
    uint32_t l8_size = (uint32_t)s->w * (uint32_t)s->h;
    int r;

    if(s->src != NULL)
    {
        return s->src;
    }

    /* 展开后的 L8 只是一张图标，放 PSRAM；不可用再回退内部 RAM */
    s->l8 = (uint8_t *)heap_caps_malloc(l8_size, MALLOC_CAP_SPIRAM);
    if(s->l8 == NULL)
    {
        s->l8 = (uint8_t *)heap_caps_malloc(l8_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if(s->l8 == NULL)
    {
        sys_loge(UI_ASSETS_TAG, "no room for %s (%u B)", s->code, (unsigned)l8_size);
        return NULL;
    }

    r = ui_asset_load_sd(s, fname);
    if(r == 0)
    {
        sys_logi(UI_ASSETS_TAG, "%s/%s loaded from SD (%ux%u)",
                 s->code, fname, (unsigned)s->w, (unsigned)s->h);
    }
    else
    {
        /* 缺失 / 非法 / 尺寸不符都走这里：用内置默认图，界面不空 */
        sys_logw(UI_ASSETS_TAG, "%s/%s unusable (r=%d), fallback to builtin",
                 s->code, fname, r);
        ui_asset_expand(s->def, s->w, s->h, s->l8);
    }

    s->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    s->dsc.header.cf = LV_COLOR_FORMAT_L8;
    s->dsc.header.w = s->w;
    s->dsc.header.h = s->h;
    s->dsc.header.stride = s->w;
    s->dsc.data_size = l8_size;
    s->dsc.data = s->l8;

    s->src = &s->dsc;
    return s->src;
}

/*********************************************************************
 * GLOBAL FUNCTIONS
 */

const void *ui_assets_icon(ui_icon_id_t id)
{
    if((int)id < 0 || (int)id >= (int)UI_ICON_NUM)
    {
        return NULL;
    }
    return ui_asset_resolve(&m_icons[id], UI_ASSET_BIN_NAME);
}

/**
 * @brief 取反色版图标（菜单选中项：实心黑底上要画白色图形）
 *
 * 1bpp 的反色就是逐位取反；这里对已展开的 L8 做 0x00<->0xFF 互换。
 * 只需提供一份图标资源，选中态的反色由固件自动得出。
 */
const void *ui_assets_icon_inverted(ui_icon_id_t id)
{
    ui_asset_slot_t *s;
    uint32_t total;
    uint32_t i;

    if((int)id < 0 || (int)id >= (int)UI_ICON_NUM)
    {
        return NULL;
    }
    s = &m_icons[id];

    if(s->src_inv != NULL)
    {
        return s->src_inv;
    }

    /* 先确保正色版就绪（它同时决定了尺寸与来源） */
    if(ui_asset_resolve(s, UI_ASSET_BIN_NAME) == NULL)
    {
        return NULL;
    }

    total = (uint32_t)s->w * (uint32_t)s->h;
    s->l8_inv = (uint8_t *)heap_caps_malloc(total, MALLOC_CAP_SPIRAM);
    if(s->l8_inv == NULL)
    {
        s->l8_inv = (uint8_t *)heap_caps_malloc(total, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if(s->l8_inv == NULL)
    {
        sys_loge(UI_ASSETS_TAG, "no room for inverted %s", s->code);
        return NULL;
    }

    for(i = 0; i < total; i++)
    {
        s->l8_inv[i] = (uint8_t)~s->l8[i];
    }

    s->dsc_inv.header.magic = LV_IMAGE_HEADER_MAGIC;
    s->dsc_inv.header.cf = LV_COLOR_FORMAT_L8;
    s->dsc_inv.header.w = s->w;
    s->dsc_inv.header.h = s->h;
    s->dsc_inv.header.stride = s->w;
    s->dsc_inv.data_size = total;
    s->dsc_inv.data = s->l8_inv;

    s->src_inv = &s->dsc_inv;
    return s->src_inv;
}

const void *ui_assets_badge(void)
{
    return ui_asset_resolve(&m_badge, UI_ASSET_BADGE_NAME);
}

void ui_assets_release(void)
{
    ui_asset_slot_t *slots[UI_ICON_NUM + 1];
    int n = 0;
    int i;

    for(i = 0; i < (int)UI_ICON_NUM; i++)
    {
        slots[n++] = &m_icons[i];
    }
    slots[n++] = &m_badge;

    for(i = 0; i < n; i++)
    {
        ui_asset_slot_t *s = slots[i];

        if(s->l8 != NULL)
        {
            heap_caps_free(s->l8);
            s->l8 = NULL;
        }
        if(s->l8_inv != NULL)
        {
            heap_caps_free(s->l8_inv);
            s->l8_inv = NULL;
        }
        memset(&s->dsc, 0, sizeof(s->dsc));
        memset(&s->dsc_inv, 0, sizeof(s->dsc_inv));
        s->src = NULL;
        s->src_inv = NULL;
    }
}

#endif /* SYS_UI_ENABLE */

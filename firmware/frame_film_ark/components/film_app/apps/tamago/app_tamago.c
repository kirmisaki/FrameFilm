/*********************************************************************
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * FileName : /film_app/apps/tamago/app_tamago.c
 * Description: 拓麻歌子 app（UI 框架层，仅 n16r8）
 *
 *   经典 P1/P2 养育闭环：蛋 → 幼年 → 少年 → 青年 → 成年，
 *   饥饿 / 快乐 / 教养 / 体重 / 便便 / 生病 / 作息 / 死亡 / 代数。
 *   精灵为程序化 1-bit 绘制（tamago_sprite.c），不依赖外部素材。
 *
 *   线程模型：模型与视图都在 ui_task（页面 lv_timer + on_key），
 *   app_task 侧只做两件事：接收 BACK/双击并回投消息、框架存取状态。
 *
 *********************************************************************/

/*********************************************************************
 * INCLUDES
 */
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_random.h"
#include "esp_heap_caps.h"

#include "sys_log.h"
#include "sys_cfg.h"

#include "ui_fonts.h"
#include "ui_conf.h"
#include "ui_ops.h"
#include "ui_core.h"
#include "app_typography.h"
#include "app_shell.h"
#include "app_language.h"
#include "app_manager.h"
#include "app_tamago.h"
#include "tamago_sprite.h"
#include "tamago_frames.h"

/*********************************************************************
 * MACROS
 */
#define APP_TAMAGO_TAG          "app_tamago"

/* 模型步进周期：1s 一拍，实际按“真实经过的分钟”补算 */
#define TG_TICK_MS              (1000)
/* 动画帧间隔（ms）：16 帧 × 250ms = 4s 一轮，与原始视频等长
 * （视频 4s/30fps；电子纸跑不到 30fps，这里用 4fps 保住动作节奏） */
#define TG_FRAME_MS             (250)

/* ---- 养育规则（游戏分钟） ---- */
#define TG_HUNGER_DROP_MIN      (120)   /* 清醒：每 2h 饥饿 −1 */
#define TG_HAPPY_DROP_MIN       (180)   /* 清醒：每 3h 快乐 −1 */
#define TG_HAPPY_DROP_SLEEP_MIN (240)   /* 开灯睡：每 4h 快乐 −1 */
#define TG_POOP_MIN             (240)   /* 每 4h 便便 +1（上限 3） */
#define TG_SICK_ROLL_MIN        (300)   /* 有便便：每 5h 判定一次生病 */
#define TG_SICK_ROLL_SLEEP_MIN  (360)   /* 开灯睡：每 6h 判定一次生病 */
#define TG_STARVE_DIE_MIN       (360)   /* 饥饿归零 6h → 死亡 */
#define TG_SICK_DIE_MIN         (720)   /* 生病 12h → 死亡 */
#define TG_DISC_PERIOD_MIN      (480)   /* 每 8h 出现一次管教时机 */
#define TG_DISC_GRACE_MIN       (120)   /* 2h 内不处理记一次照顾失误 */

/* ---- 成长阶段（小时） ---- */
#define TG_STAGE_BABY_H         (2)
#define TG_STAGE_TEEN_H         (8)
#define TG_STAGE_YOUNG_H        (20)
#define TG_STAGE_ADULT_H        (40)

/* ---- 布局（正文区内坐标，正文宽 = 屏宽 − 2×边距） ---- */
#define TG_BODY_W               (UI_LOGICAL_W - 2 * SHELL_BODY_PAD_X)
#define TG_HDR_Y                (0)
#define TG_HDR_H                (18)
#define TG_RULE1_Y              (24)
#define TG_NAME_Y               (32)
#define TG_NAME_H               (56)
/* 心数行：容器高度必须 >= 14px 字库的行高（18），否则文字底部会被裁掉 */
#define TG_HEART1_Y             (94)
#define TG_HEART2_Y             (116)
#define TG_HEART_H              (18)
#define TG_STAGE_Y              (138)
#define TG_STAGE_H              (250)
#define TG_MSG_Y                (396)
#define TG_MSG_H                (16)
#define TG_ICON_Y               (420)
#define TG_ICON_H               (52)
#define TG_INFO_Y               (482)
#define TG_INFO_H               (74)

#define TG_ICON_NUM             (8)
#define TG_HEART_NUM            (4)
#define TG_DISC_NUM             (5)
#define TG_MARK_L               (12)

/* ---- 状态标志 ---- */
#define TG_FLAG_ALIVE           (1u << 0)
#define TG_FLAG_SICK            (1u << 1)
#define TG_FLAG_LIGHT           (1u << 2)
#define TG_FLAG_SET(f)          (m_st.flags |= (uint8_t)(f))
#define TG_FLAG_CLR(f)          (m_st.flags &= (uint8_t)~(f))
#define TG_FLAG_GET(f)          ((m_st.flags & (uint8_t)(f)) != 0u)

/* ---- 内部消息 ---- */
#define TG_MSG_BACK             (0x21u)   /* app_task → 页面：子页退回主界面 */

#define TG_STATE_MAGIC          (0x54474D31u)   /* 'TGM1' */

/*********************************************************************
 * TYPEDEFS
 */
typedef enum {
    TG_SCR_PET = 0,
    TG_SCR_FOOD,
    TG_SCR_GAME,
    TG_SCR_STATUS,
} tg_screen_t;

/**
 * @brief 持久化状态（由框架按 app_entry.state 存取，app 不写 NVS）
 *
 * 字段变更必须同时 +state_ver，否则老数据会被误当成新布局。
 */
typedef struct {
    uint32_t magic;
    uint32_t last_ts;       /* 上次结算的真实时间（epoch 秒；0=未校时） */
    uint32_t gen;           /* 代数 */
    uint16_t hours_alive;
    uint16_t starve_min;
    uint16_t sick_min;
    uint16_t disc_win_min;
    uint16_t sick_roll_min;
    uint16_t care;
    uint8_t  stage;
    uint8_t  hunger;
    uint8_t  happy;
    uint8_t  disc;
    uint8_t  weight;
    uint8_t  poop;
    uint8_t  flags;
    uint8_t  awake_min;
    uint8_t  happy_min;
    uint8_t  poop_min;
    uint8_t  alive_min;
    uint8_t  disc_grace_min;
    uint8_t  disc_call;
    uint8_t  reserved[3];
} tamago_state_t;

/*********************************************************************
 * CONSTANTS
 */
static const tamago_state_t TG_STATE_DEFAULT = {
    .magic = TG_STATE_MAGIC,
    .last_ts = 0,
    .gen = 1,
    .hours_alive = 0,
    .stage = 0,
    .hunger = 3,
    .happy = 3,
    .disc = 0,
    .weight = 12,
    .poop = 0,
    .flags = TG_FLAG_ALIVE | TG_FLAG_LIGHT,
};

static const char *const TG_STAGE_ZH[5] = {"蛋", "幼年", "少年", "青年", "成年"};
static const char *const TG_STAGE_EN[5] = {"EGG", "BABY", "TEEN", "YOUNG", "ADULT"};

/* 图标条：0 食物 1 灯光 2 游戏 3 吃药 4 清洁 5 管教 6 状态 7 呼叫 */
static const char *const TG_ICON_ZH[TG_ICON_NUM] = {
    "食物", "灯光", "游戏", "吃药", "清洁", "管教", "状态", "呼叫"
};
static const char *const TG_ICON_EN[TG_ICON_NUM] = {
    "MEAL", "LAMP", "PLAY", "MED", "CLEAN", "DISC", "INFO", "CALL"
};

/*********************************************************************
 * LOCAL VARIABLES
 */
/* 模型（持久化） */
static tamago_state_t m_st;

/* 视图状态（仅 ui_task） */
static tg_screen_t m_screen;
static uint8_t m_icon;
static int m_last_min;              /* 上次刷新的“日分钟”，抑制无谓上屏（-1 = 未初始化） */
static uint32_t m_frame;            /* 动画帧号 */
static uint8_t m_force_refresh;
static uint8_t m_need_rebuild;      /* 死忙等“模型强制换页”场合置位，由 tick 统一重建 */
static char m_msg[64];              /* 消息行文本（跨页面重建保留） */
static uint8_t *m_px;               /* 200×200 L8 帧缓冲（PSRAM） */
static uint8_t *m_sprite;           /* 160×160 程序化精灵兜底（蛋/墓碑） */
static lv_image_dsc_t m_dsc;
static lv_timer_t *m_timer;         /* 1s：模拟结算 */
static lv_timer_t *m_anim_timer;    /* 300ms：帧动画 */
static uint32_t m_evolve_until_ms;  /* 进化片段播放到何时 */
static int m_last_anim;             /* 当前播放的动画号（变化时从头播） */
static app_shell_t m_shell;

/* 未校时时的虚拟时钟（从 08:00 起算） */
static uint32_t m_virt_base = 8u * 3600u;
static uint32_t m_virt_t0;

/* 控件 */
static lv_obj_t *m_body;
static lv_obj_t *m_img;
static lv_obj_t *m_lbl_hdr_r;
static lv_obj_t *m_lbl_name;
static lv_obj_t *m_lbl_sub;
static lv_obj_t *m_heart[2][TG_HEART_NUM];
static lv_obj_t *m_disc[TG_DISC_NUM];
static lv_obj_t *m_lbl_alert;
static lv_obj_t *m_lbl_cap;
static lv_obj_t *m_lbl_msg;
static lv_obj_t *m_icon_box[TG_ICON_NUM];
static lv_obj_t *m_icon_lbl[TG_ICON_NUM];
static lv_obj_t *m_lbl_info[4];

/* 子页控件 */
static lv_obj_t *m_food_card[2];
static lv_obj_t *m_game_lbl[3];
static lv_obj_t *m_stat_val[8];

/* 子页 RAM 态 */
static uint8_t m_food_sel;
static struct {
    uint8_t round;
    uint8_t score;
    uint8_t pick;
    uint8_t target;
    uint8_t phase;      /* 0 选择 1 结果 */
    uint8_t win;
} m_game;

/* app_task 可见的当前页面（供 BACK/双击裁决；单字节竞争可接受） */
static volatile uint8_t m_ui_screen;

/*********************************************************************
 * LOCAL FUNCTIONS
 */
static void tg_tick_cb(lv_timer_t *timer);
static void tg_refresh(void);
static void tg_rebuild(void);
static void tg_build_pet(void);
static void tg_build_food(void);
static void tg_build_game(void);
static void tg_build_status(void);
static void tg_do_icon(void);
static void tg_feed(uint8_t snack);
static void tg_game_confirm(void);
static void tg_finish_game(void);
static void tg_simulate(uint32_t elapsed_s);
static void tg_step_minute(int minute_of_day);
static void tg_die(const char *reason);
static void tg_set_msg(const char *txt);

static lv_obj_t *tg_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color,
                          const char *txt);
static lv_obj_t *tg_box(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h);
static lv_obj_t *tg_bar(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h);
static void tg_rule(lv_obj_t *parent, int32_t y);
static void tg_marks(lv_obj_t *parent, int32_t w, int32_t h);

/*********************************************************************
 * LOCAL HELPERS
 */

static uint32_t tg_now_sec(void)
{
    time_t t = time(NULL);

    if(t >= (time_t)1700000000)
    {
        return (uint32_t)t;
    }
    if(m_virt_t0 == 0u)
    {
        m_virt_t0 = lv_tick_get();
    }
    return m_virt_base + (uint32_t)((lv_tick_get() - m_virt_t0) / 1000u);
}

static int tg_minute_of_day(void)
{
    time_t t = time(NULL);

    if(t >= (time_t)1700000000)
    {
        struct tm tmv;

        localtime_r(&t, &tmv);
        return tmv.tm_hour * 60 + tmv.tm_min;
    }
    return (int)((tg_now_sec() / 60u) % 1440u);
}

static uint8_t tg_stage_from_hours(uint16_t hours)
{
    if(hours >= TG_STAGE_ADULT_H) return 4;
    if(hours >= TG_STAGE_YOUNG_H) return 3;
    if(hours >= TG_STAGE_TEEN_H)  return 2;
    if(hours >= TG_STAGE_BABY_H)  return 1;
    return 0;
}

static const char *tg_text(const char *zh, const char *en)
{
    return app_text(zh, en);
}

static uint8_t tg_is_sleep_time(void)
{
    int mod = tg_minute_of_day();

    return (uint8_t)((mod >= 21 * 60) || (mod < 8 * 60));
}

static const char *tg_attention(void)
{
    if(!TG_FLAG_GET(TG_FLAG_ALIVE)) return tg_text("死亡", "DEAD");
    if(TG_FLAG_GET(TG_FLAG_SICK))   return tg_text("生病", "SICK");
    if(m_st.poop > 0)               return tg_text("便便", "DIRTY");
    if(m_st.hunger == 0)            return tg_text("饿了", "HUNGRY");
    if(m_st.happy == 0)             return tg_text("孤单", "LONELY");
    if(m_st.disc_call)              return tg_text("呼叫", "CALL");
    return NULL;
}

/**
 * @brief 写消息行
 *
 * 消息行只在宠物主界面存在；在子页里调用时先存到 m_msg，
 * 回到主界面重建时再显示（避免对 NULL 控件写文本）。
 */
static void tg_set_msg(const char *txt)
{
    snprintf(m_msg, sizeof(m_msg), "%s", (txt != NULL) ? txt : "");
    if(m_lbl_msg != NULL)
    {
        lv_label_set_text(m_lbl_msg, m_msg);
    }
}

/**
 * @brief 精灵表情：由当前状态推导（优先级：死 > 病 > 睡 > 吃/高兴 > 进化 > 待机）
 */
static uint8_t tg_face(void)
{
    if(!TG_FLAG_GET(TG_FLAG_ALIVE)) return TAMAGO_FACE_DEAD;
    if(TG_FLAG_GET(TG_FLAG_SICK))   return TAMAGO_FACE_SICK;
    if(tg_is_sleep_time())          return TAMAGO_FACE_SLEEP;
    if(m_screen == TG_SCR_FOOD)     return TAMAGO_FACE_EAT;
    if(m_screen == TG_SCR_GAME)     return TAMAGO_FACE_HAPPY;
    return TAMAGO_FACE_IDLE;
}

/**
 * @brief 当前状态对应的视频帧动画号；-1 表示用程序化精灵（蛋 / 墓碑）
 */
static int tg_anim_for_state(void)
{
    if(!TG_FLAG_GET(TG_FLAG_ALIVE))
    {
        return -1;
    }
    if(m_st.stage == 0)
    {
        return -1;      /* 蛋：视频里没有蛋形态 */
    }
    if((m_evolve_until_ms != 0u) && (lv_tick_get() < m_evolve_until_ms))
    {
        return TAMAGO_ANIM_EVOLVE;
    }
    if(TG_FLAG_GET(TG_FLAG_SICK)) return TAMAGO_ANIM_SICK;
    if(tg_is_sleep_time())        return TAMAGO_ANIM_SLEEP;
    if(m_screen == TG_SCR_FOOD)   return TAMAGO_ANIM_EAT;
    if(m_screen == TG_SCR_GAME)   return TAMAGO_ANIM_HAPPY;
    return TAMAGO_ANIM_IDLE;
}

/**
 * @brief 解包一帧 1-bit（MSB 优先）到 L8：1=黑(0x00)，0=白(0xFF)
 */
static void tg_unpack_frame(const uint8_t *src)
{
    uint32_t i;
    uint8_t *dst = m_px;

    for(i = 0; i < (uint32_t)TAMAGO_ANIM_BYTES_FRAME; i++)
    {
        uint8_t b = src[i];

        dst[0] = (b & 0x80u) ? 0x00u : 0xFFu;
        dst[1] = (b & 0x40u) ? 0x00u : 0xFFu;
        dst[2] = (b & 0x20u) ? 0x00u : 0xFFu;
        dst[3] = (b & 0x10u) ? 0x00u : 0xFFu;
        dst[4] = (b & 0x08u) ? 0x00u : 0xFFu;
        dst[5] = (b & 0x04u) ? 0x00u : 0xFFu;
        dst[6] = (b & 0x02u) ? 0x00u : 0xFFu;
        dst[7] = (b & 0x01u) ? 0x00u : 0xFFu;
        dst += 8;
    }
}

/**
 * @brief 绘制“当前帧”到 m_px 并让 LVGL 重绘
 *
 * 视频帧：直接在 flash 里按位展开（1-bit → L8），
 * 蛋 / 墓碑：程序化精灵渲染到 160×160 后居中贴到 200×200 画布。
 */
static void tg_draw_frame(void)
{
    int anim;

    if(m_px == NULL)
    {
        return;
    }

    anim = tg_anim_for_state();
    if(anim < 0)
    {
        if(m_sprite != NULL)
        {
            int y;
            const int off = (TAMAGO_ANIM_W - TAMAGO_SPRITE_W) / 2;

            memset(m_px, 0xFF, (size_t)TAMAGO_ANIM_W * TAMAGO_ANIM_H);
            tamago_sprite_render(m_sprite, tg_face(), m_st.stage, m_frame, m_st.poop);
            for(y = 0; y < TAMAGO_SPRITE_H; y++)
            {
                memcpy(m_px + (size_t)(y + off) * TAMAGO_ANIM_W + off,
                       m_sprite + (size_t)y * TAMAGO_SPRITE_W,
                       TAMAGO_SPRITE_W);
            }
        }
    }
    else
    {
        if(anim != m_last_anim)
        {
            m_last_anim = anim;
            m_frame = 0;
        }
        tg_unpack_frame(tamago_anim_data[anim] +
                        (uint32_t)(m_frame % TAMAGO_ANIM_FRAMES) * TAMAGO_ANIM_BYTES_FRAME);
    }

    if(m_img != NULL)
    {
        lv_obj_invalidate(m_img);
    }
}

/**
 * @brief 帧动画定时器：宠物主界面常驻循环播放
 */
static void tg_anim_cb(lv_timer_t *timer)
{
    (void)timer;

    if(m_body == NULL)
    {
        return;
    }
    m_frame++;
    tg_draw_frame();
}

/*********************************************************************
 * 模拟
 */

static void tg_die(const char *reason)
{
    (void)reason;
    TG_FLAG_CLR(TG_FLAG_ALIVE);
    TG_FLAG_CLR(TG_FLAG_SICK);
    m_screen = TG_SCR_PET;
    m_need_rebuild = 1;
    tg_set_msg(tg_text("它离开了 · 打开「状态」可新生", "GONE - OPEN INFO TO REBIRTH"));
}

/**
 * @brief 推进一分钟
 *
 * @param minute_of_day 该分钟对应的“分钟数（0..1439）”，由调用方按步进推算，
 *                      避免每步都去读 RTC
 */
static void tg_step_minute(int minute_of_day)
{
    uint8_t sleeping;

    if(!TG_FLAG_GET(TG_FLAG_ALIVE))
    {
        return;
    }

    sleeping = (uint8_t)((minute_of_day >= 21 * 60) || (minute_of_day < 8 * 60));

    /* ---- 饥饿 / 快乐 ---- */
    if(!sleeping)
    {
        if(++m_st.awake_min >= TG_HUNGER_DROP_MIN)
        {
            m_st.awake_min = 0;
            if(m_st.hunger > 0) m_st.hunger--;
        }
        if(++m_st.happy_min >= TG_HAPPY_DROP_MIN)
        {
            m_st.happy_min = 0;
            if(m_st.happy > 0) m_st.happy--;
        }
    }
    else if(TG_FLAG_GET(TG_FLAG_LIGHT))
    {
        /* 开灯睡：掉快乐 + 更容易生病 */
        if(++m_st.happy_min >= TG_HAPPY_DROP_SLEEP_MIN)
        {
            m_st.happy_min = 0;
            if(m_st.happy > 0) m_st.happy--;
        }
        if(++m_st.sick_roll_min >= TG_SICK_ROLL_SLEEP_MIN)
        {
            m_st.sick_roll_min = 0;
            if((esp_random() & 3u) == 0u)
            {
                TG_FLAG_SET(TG_FLAG_SICK);
            }
        }
    }

    /* ---- 便便 ---- */
    if(++m_st.poop_min >= TG_POOP_MIN)
    {
        m_st.poop_min = 0;
        if(m_st.poop < 3) m_st.poop++;
    }

    /* ---- 有便便时更容易生病（清醒时判定） ---- */
    if(!sleeping && (m_st.poop > 0) && !TG_FLAG_GET(TG_FLAG_SICK))
    {
        if(++m_st.sick_roll_min >= TG_SICK_ROLL_MIN)
        {
            m_st.sick_roll_min = 0;
            if((esp_random() & 3u) == 0u)
            {
                TG_FLAG_SET(TG_FLAG_SICK);
            }
        }
    }

    /* ---- 死亡判定 ---- */
    if(m_st.hunger == 0)
    {
        if(++m_st.starve_min >= TG_STARVE_DIE_MIN)
        {
            tg_die("饥饿");
            return;
        }
    }
    else
    {
        m_st.starve_min = 0;
    }

    if(TG_FLAG_GET(TG_FLAG_SICK))
    {
        if(++m_st.sick_min >= TG_SICK_DIE_MIN)
        {
            tg_die("生病");
            return;
        }
    }
    else
    {
        m_st.sick_min = 0;
    }

    /* ---- 存活时长 / 阶段 ---- */
    if(++m_st.alive_min >= 60)
    {
        uint8_t old_stage = m_st.stage;

        m_st.alive_min = 0;
        m_st.hours_alive++;
        m_st.stage = tg_stage_from_hours(m_st.hours_alive);
        if(m_st.stage != old_stage)
        {
            /* 进化：播放一段进化动画再回到常态 */
            m_evolve_until_ms = lv_tick_get() + 5000u;
        }
    }

    /* ---- 管教时机 ---- */
    if(!m_st.disc_call)
    {
        if(++m_st.disc_win_min >= TG_DISC_PERIOD_MIN)
        {
            m_st.disc_win_min = 0;
            m_st.disc_call = 1;
            m_st.disc_grace_min = 0;
        }
    }
    else if(++m_st.disc_grace_min >= TG_DISC_GRACE_MIN)
    {
        m_st.disc_grace_min = 0;
        m_st.disc_call = 0;
        m_st.care++;
    }
}

/**
 * @brief 按真实经过时间补算（含设备休眠 / app 未打开期间的欠账）
 */
static void tg_simulate(uint32_t elapsed_s)
{
    uint32_t mins;
    uint32_t i;
    int mod;
    int start;

    if(elapsed_s < 60u)
    {
        return;
    }

    mins = elapsed_s / 60u;
    if(mins > (uint32_t)(12 * 60))
    {
        mins = (uint32_t)(12 * 60);   /* 最多补算 12h，避免死循环 */
    }

    mod = tg_minute_of_day();
    start = mod - (int)mins;
    start = ((start % 1440) + 1440) % 1440;

    for(i = 0; i < mins; i++)
    {
        if(!TG_FLAG_GET(TG_FLAG_ALIVE))
        {
            break;
        }
        tg_step_minute((start + (int)i) % 1440);
    }
}

/*********************************************************************
 * 行为
 */

static void tg_do_icon(void)
{
    if(!TG_FLAG_GET(TG_FLAG_ALIVE))
    {
        /* 死亡：只有“状态”可用，里面可以重生 */
        if(m_icon == 6)
        {
            m_screen = TG_SCR_STATUS;
            tg_rebuild();
        }
        else
        {
            tg_set_msg(tg_text("它已经离开了 · 打开「状态」可新生",
                               "GONE - OPEN INFO TO REBIRTH"));
        }
        return;
    }

    switch(m_icon)
    {
    case 0:     /* 食物 */
        if(tg_is_sleep_time())
        {
            tg_set_msg(tg_text("它在睡觉 · 不能喂食", "SLEEPING - NO FEED"));
            break;
        }
        m_food_sel = (m_st.hunger >= 4) ? 1 : 0;
        m_screen = TG_SCR_FOOD;
        tg_rebuild();
        break;

    case 1:     /* 灯光 */
        if(TG_FLAG_GET(TG_FLAG_LIGHT))
        {
            TG_FLAG_CLR(TG_FLAG_LIGHT);
            tg_set_msg(tg_text("关灯 · 可以睡了", "LAMP OFF - GOOD NIGHT"));
        }
        else
        {
            TG_FLAG_SET(TG_FLAG_LIGHT);
            tg_set_msg(tg_text("开灯 · 该起床了", "LAMP ON - WAKE UP"));
        }
        m_force_refresh = 1;
        break;

    case 2:     /* 游戏 */
        if(tg_is_sleep_time())
        {
            tg_set_msg(tg_text("它在睡觉 · 不能游戏", "SLEEPING - NO PLAY"));
            break;
        }
        if(TG_FLAG_GET(TG_FLAG_SICK))
        {
            tg_set_msg(tg_text("生病了 · 先吃药", "SICK - TAKE MEDICINE"));
            break;
        }
        m_game.round = 0;
        m_game.score = 0;
        m_game.phase = 0;
        m_game.pick = 0;
        m_game.target = (uint8_t)(esp_random() & 1u);
        m_screen = TG_SCR_GAME;
        tg_rebuild();
        break;

    case 3:     /* 吃药 */
        if(TG_FLAG_GET(TG_FLAG_SICK))
        {
            TG_FLAG_CLR(TG_FLAG_SICK);
            m_st.sick_min = 0;
            tg_set_msg(tg_text("服药 · 已康复", "MEDICINE - RECOVERED"));
        }
        else
        {
            tg_set_msg(tg_text("没有生病", "NOT SICK"));
        }
        m_force_refresh = 1;
        break;

    case 4:     /* 清洁 */
        if(m_st.poop > 0)
        {
            m_st.poop = 0;
            tg_set_msg(tg_text("清洁完成", "CLEANED"));
        }
        else
        {
            tg_set_msg(tg_text("已经很干净", "ALREADY CLEAN"));
        }
        m_force_refresh = 1;
        break;

    case 5:     /* 管教 */
        if(m_st.disc_call)
        {
            m_st.disc_call = 0;
            m_st.disc_grace_min = 0;
            if(m_st.disc < TG_DISC_NUM) m_st.disc++;
            tg_set_msg(tg_text("管教 · 教养 +1", "DISCIPLINE - MANNERS +1"));
        }
        else
        {
            tg_set_msg(tg_text("现在不需要管教", "NO NEED NOW"));
        }
        m_force_refresh = 1;
        break;

    case 6:     /* 状态 */
        m_screen = TG_SCR_STATUS;
        tg_rebuild();
        break;

    case 7:     /* 呼叫：显示当前需求 */
    default:
        {
            const char *att = tg_attention();

            tg_set_msg(att != NULL ? att : tg_text("状态良好", "ALL GOOD"));
            break;
        }
    }
}

static void tg_feed(uint8_t snack)
{
    if(!TG_FLAG_GET(TG_FLAG_ALIVE) || tg_is_sleep_time())
    {
        m_screen = TG_SCR_PET;
        tg_rebuild();
        return;
    }

    if(!snack)
    {
        if(m_st.hunger >= 4)
        {
            tg_set_msg(tg_text("已经吃饱了", "FULL"));
        }
        else
        {
            m_st.hunger++;
            m_st.weight++;
            tg_set_msg(tg_text("正餐 · 饥饿 +1", "MEAL - HUNGER +1"));
        }
    }
    else
    {
        if(m_st.happy >= 4)
        {
            tg_set_msg(tg_text("它很开心 · 不需要零食", "HAPPY ENOUGH"));
        }
        else
        {
            m_st.happy++;
            m_st.weight++;
            tg_set_msg(tg_text("零食 · 快乐 +1", "SNACK - HAPPY +1"));
        }
    }

    m_screen = TG_SCR_PET;
    tg_rebuild();
}

static void tg_game_confirm(void)
{
    if(m_game.phase == 0)
    {
        m_game.win = (uint8_t)(m_game.pick == m_game.target);
        if(m_game.win) m_game.score++;
        m_game.phase = 1;
        tg_refresh();
        return;
    }

    m_game.round++;
    if(m_game.round >= 3)
    {
        tg_finish_game();
        return;
    }
    m_game.target = (uint8_t)(esp_random() & 1u);
    m_game.pick = 0;
    m_game.phase = 0;
    tg_refresh();
}

static void tg_finish_game(void)
{
    if(m_game.score >= 2)
    {
        if(m_st.happy < 4) m_st.happy++;
        if(m_st.weight > 1) m_st.weight--;
        tg_set_msg(tg_text("游戏胜利 · 快乐 +1 / 体重 −1",
                                            "WIN - HAPPY +1 / WEIGHT -1"));
    }
    else
    {
        tg_set_msg(tg_text("游戏结束 · 差一点", "GAME OVER"));
    }
    m_screen = TG_SCR_PET;
    tg_rebuild();
}

/*********************************************************************
 * 页面构建
 */

static lv_obj_t *tg_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color,
                          const char *txt)
{
    lv_obj_t *l = lv_label_create(parent);

    lv_obj_set_style_text_font(l, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(l, color, LV_PART_MAIN);
    lv_label_set_text(l, txt);
    return l;
}

static lv_obj_t *tg_box(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h)
{
    lv_obj_t *o = lv_obj_create(parent);

    lv_obj_remove_style_all(o);
    lv_obj_set_scrollable(o, false);
    lv_obj_set_size(o, w, h);
    lv_obj_set_pos(o, x, y);
    return o;
}

static lv_obj_t *tg_bar(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h)
{
    lv_obj_t *o = tg_box(parent, x, y, w, h);

    lv_obj_set_style_bg_color(o, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, LV_PART_MAIN);
    return o;
}

static void tg_rule(lv_obj_t *parent, int32_t y)
{
    (void)tg_bar(parent, 0, y, TG_BODY_W, 1);
}

static void tg_marks(lv_obj_t *parent, int32_t w, int32_t h)
{
    uint8_t i;

    for(i = 0; i < 4; i++)
    {
        int left = (i == 0 || i == 2);
        int top  = (i == 0 || i == 1);
        int32_t hx = left ? 0 : (w - TG_MARK_L);
        int32_t hy = top ? 0 : (h - 1);
        int32_t vx = left ? 0 : (w - 1);
        int32_t vy = top ? 0 : (h - TG_MARK_L);

        (void)tg_bar(parent, hx, hy, TG_MARK_L, 1);
        (void)tg_bar(parent, vx, vy, 1, TG_MARK_L);
    }
}

static void tg_heart_cell(lv_obj_t *parent, int32_t x, lv_obj_t **out)
{
    /* 行高 18，格子 12 高，下移 3 让它在文字行里垂直居中 */
    lv_obj_t *o = tg_box(parent, x, 3, 20, 12);

    lv_obj_set_style_border_width(o, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(o, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(o, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, LV_PART_MAIN);
    *out = o;
}

static void tg_build_pet(void)
{
    lv_obj_t *row;
    lv_obj_t *o;
    lv_obj_t *stage;
    lv_obj_t *icons;
    uint8_t i;

    /* ---- 表头 ---- */
    row = tg_box(m_body, 0, TG_HDR_Y, TG_BODY_W, TG_HDR_H);
    (void)tg_label(row, app_font_small(), lv_color_black(),
                   tg_text("宠物单元 01", "PET UNIT 01"));
    m_lbl_hdr_r = tg_label(row, app_font_small(), lv_color_black(), "");
    lv_obj_align(m_lbl_hdr_r, LV_ALIGN_TOP_RIGHT, 0, 0);
    tg_rule(m_body, TG_RULE1_Y);

    /* ---- 铭牌 ---- */
    row = tg_box(m_body, 0, TG_NAME_Y, TG_BODY_W, TG_NAME_H);
    m_lbl_name = tg_label(row, app_font_hero(), lv_color_black(), "TAMAGO");
    lv_obj_set_pos(m_lbl_name, 0, 0);
    /* 三行（代数/年龄/体重），每行 18px 共 54px，需贴顶放，否则末行会被 56px 的铭牌行裁掉 */
    m_lbl_sub = tg_label(row, app_font_small(), lv_color_black(), "");
    lv_obj_align(m_lbl_sub, LV_ALIGN_TOP_RIGHT, 0, 0);

    /* ---- 饥饿 / 快乐心数 ---- */
    row = tg_box(m_body, 0, TG_HEART1_Y, TG_BODY_W, TG_HEART_H);
    (void)tg_label(row, app_font_small(), lv_color_black(), tg_text("饥饿", "HUNGER"));
    for(i = 0; i < TG_HEART_NUM; i++)
    {
        tg_heart_cell(row, 60 + i * 24, &m_heart[0][i]);
    }

    row = tg_box(m_body, 0, TG_HEART2_Y, TG_BODY_W, TG_HEART_H);
    (void)tg_label(row, app_font_small(), lv_color_black(), tg_text("快乐", "HAPPY"));
    for(i = 0; i < TG_HEART_NUM; i++)
    {
        tg_heart_cell(row, 60 + i * 24, &m_heart[1][i]);
    }
    for(i = 0; i < TG_DISC_NUM; i++)
    {
        o = tg_box(row, 200 + i * 18, 5, 14, 8);
        lv_obj_set_style_border_width(o, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(o, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_bg_color(o, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, LV_PART_MAIN);
        m_disc[i] = o;
    }
    {
        lv_obj_t *manners = tg_label(row, app_font_small(), lv_color_black(),
                                     tg_text("教养", "MANNERS"));

        lv_obj_set_pos(manners, 152, 0);
    }

    /* ---- 舞台 ---- */
    stage = tg_box(m_body, 0, TG_STAGE_Y, TG_BODY_W, TG_STAGE_H);
    lv_obj_set_style_border_width(stage, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(stage, lv_color_black(), LV_PART_MAIN);

    (void)tg_label(stage, app_font_small(), lv_color_black(), tg_text("单元 01", "UNIT 01"));
    lv_obj_set_pos(lv_obj_get_child(stage, 0), 12, 10);

    m_lbl_alert = tg_label(stage, app_font_small(), lv_color_white(), "");
    lv_obj_set_style_bg_color(m_lbl_alert, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(m_lbl_alert, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(m_lbl_alert, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(m_lbl_alert, 3, LV_PART_MAIN);
    lv_obj_align(m_lbl_alert, LV_ALIGN_TOP_RIGHT, -10, 8);
    lv_obj_set_hidden(m_lbl_alert, true);

    if(m_px != NULL)
    {
        m_img = lv_image_create(stage);
        lv_image_set_src(m_img, &m_dsc);
        lv_obj_center(m_img);
    }

    m_lbl_cap = tg_label(stage, app_font_small(), lv_color_black(), "");
    lv_obj_align(m_lbl_cap, LV_ALIGN_BOTTOM_LEFT, 12, -8);

    tg_marks(stage, TG_BODY_W, TG_STAGE_H);

    /* ---- 消息行 ---- */
    if(m_msg[0] == '\0')
    {
        const char *att = tg_attention();

        tg_set_msg(att != NULL ? att : tg_text("状态良好 · 等待互动", "ALL GOOD"));
    }
    m_lbl_msg = tg_label(m_body, app_font_small(), lv_color_black(), m_msg);
    lv_obj_set_pos(m_lbl_msg, 0, TG_MSG_Y);
    lv_obj_set_width(m_lbl_msg, TG_BODY_W);
    lv_label_set_long_mode(m_lbl_msg, LV_LABEL_LONG_CLIP);

    /* ---- 图标条 ---- */
    icons = tg_box(m_body, 0, TG_ICON_Y, TG_BODY_W, TG_ICON_H);
    lv_obj_set_flex_flow(icons, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(icons, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    for(i = 0; i < TG_ICON_NUM; i++)
    {
        lv_obj_t *cell = lv_obj_create(icons);

        lv_obj_remove_style_all(cell);
        lv_obj_set_scrollable(cell, false);
        lv_obj_set_size(cell, 50, TG_ICON_H);
        lv_obj_set_style_border_width(cell, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(cell, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_bg_color(cell, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(cell, LV_OPA_TRANSP, LV_PART_MAIN);
        m_icon_box[i] = cell;
        m_icon_lbl[i] = tg_label(cell, app_font_small(), lv_color_black(),
                                 tg_text(TG_ICON_ZH[i], TG_ICON_EN[i]));
        lv_obj_center(m_icon_lbl[i]);
    }

    /* ---- 信息格 ---- */
    {
        lv_obj_t *info = tg_box(m_body, 0, TG_INFO_Y, TG_BODY_W, TG_INFO_H);

        lv_obj_set_style_border_width(info, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(info, lv_color_black(), LV_PART_MAIN);
        (void)tg_bar(info, TG_BODY_W / 2, 8, 1, TG_INFO_H - 16);
        (void)tg_bar(info, 8, TG_INFO_H / 2, TG_BODY_W - 16, 1);
        for(i = 0; i < 4; i++)
        {
            int32_t x = (i % 2) ? (TG_BODY_W / 2 + 12) : 12;
            int32_t y = (i / 2) ? (TG_INFO_H / 2 + 8) : 8;
            lv_obj_t *k = tg_label(info, app_font_small(), lv_color_black(), "");
            lv_obj_set_pos(k, x, y);
            m_lbl_info[i] = tg_label(info, app_font_small(), lv_color_black(), "");
            lv_obj_set_pos(m_lbl_info[i], x + 150, y);
        }
    }
}

static void tg_build_food(void)
{
    lv_obj_t *title;
    uint8_t i;

    title = tg_label(m_body, app_font_title(), lv_color_black(),
                     tg_text("喂食", "FEED"));
    lv_obj_set_pos(title, 0, 0);
    (void)tg_label(m_body, app_font_small(), lv_color_black(),
                   tg_text("上下选择 · 确认键喂食 · 双击返回", "UP/DOWN PICK - ENTER FEED"));
    lv_obj_align(lv_obj_get_child(m_body, 1), LV_ALIGN_TOP_RIGHT, 0, 6);
    tg_rule(m_body, 40);

    for(i = 0; i < 2; i++)
    {
        lv_obj_t *card = tg_box(m_body, 0, 64 + i * 110, TG_BODY_W, 100);

        lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(card, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_bg_color(card, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(card, LV_OPA_TRANSP, LV_PART_MAIN);
        m_food_card[i] = card;
        (void)tg_label(card, app_font_title(), lv_color_black(),
                       i == 0 ? tg_text("正餐", "MEAL") : tg_text("零食", "SNACK"));
        lv_obj_set_pos(lv_obj_get_child(card, 0), 16, 12);
        (void)tg_label(card, app_font_small(), lv_color_black(),
                       i == 0 ? tg_text("饥饿 +1 · 体重 +1", "HUNGER +1 / WEIGHT +1")
                              : tg_text("快乐 +1 · 体重 +1", "HAPPY +1 / WEIGHT +1"));
        lv_obj_set_pos(lv_obj_get_child(card, 1), 16, 56);
    }

    {
        lv_obj_t *tip = tg_label(m_body, app_font_small(), lv_color_black(),
                                 tg_text("Tip：睡觉时不能喂食", "TIP: NO FEED WHILE SLEEPING"));

        lv_obj_set_pos(tip, 0, 300);
    }
}

static void tg_build_game(void)
{
    lv_obj_t *title;

    title = tg_label(m_body, app_font_title(), lv_color_black(), tg_text("猜大小", "GUESS"));
    lv_obj_set_pos(title, 0, 0);
    (void)tg_label(m_body, app_font_small(), lv_color_black(),
                   tg_text("↑ = 大    ↓ = 小   确认键确定", "UP=BIG  DOWN=SMALL  ENTER=OK"));
    lv_obj_align(lv_obj_get_child(m_body, 1), LV_ALIGN_TOP_RIGHT, 0, 6);
    tg_rule(m_body, 40);

    m_game_lbl[0] = tg_label(m_body, app_font_hero(), lv_color_black(), "");
    lv_obj_set_pos(m_game_lbl[0], 0, 64);

    m_game_lbl[1] = tg_label(m_body, app_font_title(), lv_color_black(), "");
    lv_obj_set_pos(m_game_lbl[1], 0, 132);

    m_game_lbl[2] = tg_label(m_body, app_font_body(), lv_color_black(), "");
    lv_obj_set_pos(m_game_lbl[2], 0, 208);
}

static void tg_build_status(void)
{
    static const char *const NAME_ZH[8] = {
        "名字", "阶段", "年龄", "体重", "饥饿", "快乐", "教养", "便便"
    };
    static const char *const NAME_EN[8] = {
        "NAME", "STAGE", "AGE", "WEIGHT", "HUNGER", "HAPPY", "MANNERS", "POOP"
    };
    lv_obj_t *title;
    uint8_t i;

    title = tg_label(m_body, app_font_title(), lv_color_black(),
                     TG_FLAG_GET(TG_FLAG_ALIVE) ? tg_text("宠物状态", "PET STATUS")
                                                : tg_text("已离开", "GONE"));
    lv_obj_set_pos(title, 0, 0);
    tg_rule(m_body, 40);

    for(i = 0; i < 8; i++)
    {
        lv_obj_t *k = tg_label(m_body, app_font_small(), lv_color_black(),
                               tg_text(NAME_ZH[i], NAME_EN[i]));
        lv_obj_set_pos(k, 0, 60 + i * 34);
        m_stat_val[i] = tg_label(m_body, app_font_small(), lv_color_black(), "");
        lv_obj_align(m_stat_val[i], LV_ALIGN_TOP_RIGHT, 0, 60 + i * 34);
        (void)tg_bar(m_body, 0, 60 + i * 34 + 24, TG_BODY_W, 1);
    }

    {
        lv_obj_t *hint = tg_label(m_body, app_font_small(), lv_color_black(),
                                  TG_FLAG_GET(TG_FLAG_ALIVE)
                                      ? tg_text("确认键返回 · 双击回主菜单", "ENTER BACK - 2X MENU")
                                      : tg_text("确认键 = 新生（第 N+1 代）", "ENTER = REBIRTH"));

        lv_obj_set_pos(hint, 0, 350);
    }
}

/*********************************************************************
 * 刷新
 */

static void tg_refresh_pet(void)
{
    char buf[48];
    const char *att;
    uint8_t i;

    /* 表头 */
    snprintf(buf, sizeof(buf), "%s · %02d:%02d",
             tg_text(TG_STAGE_ZH[m_st.stage], TG_STAGE_EN[m_st.stage]),
             tg_minute_of_day() / 60, tg_minute_of_day() % 60);
    lv_label_set_text(m_lbl_hdr_r, buf);

    /* 铭牌 */
    snprintf(buf, sizeof(buf), "%s %u\n%s %u\n%s %ug",
             tg_text("代数", "GEN"), (unsigned)m_st.gen,
             tg_text("年龄", "AGE"), (unsigned)(m_st.hours_alive / 24),
             tg_text("体重", "WT"), (unsigned)m_st.weight);
    lv_label_set_text(m_lbl_sub, buf);

    /* 心数 */
    for(i = 0; i < TG_HEART_NUM; i++)
    {
        lv_obj_set_style_bg_opa(m_heart[0][i], (i < m_st.hunger) ? LV_OPA_COVER : LV_OPA_TRANSP,
                                LV_PART_MAIN);
        lv_obj_set_style_bg_opa(m_heart[1][i], (i < m_st.happy) ? LV_OPA_COVER : LV_OPA_TRANSP,
                                LV_PART_MAIN);
    }
    for(i = 0; i < TG_DISC_NUM; i++)
    {
        lv_obj_set_style_bg_opa(m_disc[i], (i < m_st.disc) ? LV_OPA_COVER : LV_OPA_TRANSP,
                                LV_PART_MAIN);
    }

    /* 图标选中态 */
    for(i = 0; i < TG_ICON_NUM; i++)
    {
        uint8_t on = (uint8_t)(i == m_icon);
        lv_obj_set_style_bg_opa(m_icon_box[i], on ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_text_color(m_icon_lbl[i], on ? lv_color_white() : lv_color_black(),
                                    LV_PART_MAIN);
    }

    /* 角标 */
    att = tg_attention();
    if(att != NULL)
    {
        lv_label_set_text(m_lbl_alert, att);
        lv_obj_set_hidden(m_lbl_alert, false);
    }
    else
    {
        lv_obj_set_hidden(m_lbl_alert, true);
    }

    /* 舞台角标 */
    if(!TG_FLAG_GET(TG_FLAG_ALIVE))
    {
        lv_label_set_text(m_lbl_cap, tg_text("无信号", "NO SIGNAL"));
    }
    else if(TG_FLAG_GET(TG_FLAG_SICK))
    {
        lv_label_set_text(m_lbl_cap, tg_text("生病", "SICK"));
    }
    else if(tg_is_sleep_time())
    {
        lv_label_set_text(m_lbl_cap, tg_text("睡觉", "ASLEEP"));
    }
    else
    {
        lv_label_set_text(m_lbl_cap, tg_text("待机", "IDLE"));
    }

    /* 信息格 */
    lv_label_set_text(m_lbl_info[0], tg_text(TG_STAGE_ZH[m_st.stage], TG_STAGE_EN[m_st.stage]));
    snprintf(buf, sizeof(buf), "%u / 3", (unsigned)m_st.poop);
    lv_label_set_text(m_lbl_info[1], buf);
    lv_label_set_text(m_lbl_info[2], TG_FLAG_GET(TG_FLAG_LIGHT)
                     ? tg_text("开", "ON") : tg_text("关", "OFF"));
    lv_label_set_text(m_lbl_info[3], TG_FLAG_GET(TG_FLAG_SICK)
                     ? tg_text("是", "YES") : tg_text("否", "NO"));

    /* 精灵（视频帧 / 蛋 / 墓碑） */
    tg_draw_frame();
}

static void tg_refresh_food(void)
{
    uint8_t i;

    for(i = 0; i < 2; i++)
    {
        uint8_t on = (uint8_t)(i == m_food_sel);

        lv_obj_set_style_bg_opa(m_food_card[i], on ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_text_color(lv_obj_get_child(m_food_card[i], 0),
                                    on ? lv_color_white() : lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_text_color(lv_obj_get_child(m_food_card[i], 1),
                                    on ? lv_color_white() : lv_color_black(), LV_PART_MAIN);
    }
}

static void tg_refresh_game(void)
{
    char buf[64];

    if(m_game.phase == 0)
    {
        snprintf(buf, sizeof(buf), "%s %u / 3",
                 tg_text("第", "ROUND"), (unsigned)(m_game.round + 1));
        lv_label_set_text(m_game_lbl[0], buf);
        lv_label_set_text(m_game_lbl[1], m_game.pick == 0 ? tg_text("大 ▲", "BIG")
                                                          : tg_text("小 ▼", "SMALL"));
        lv_label_set_text(m_game_lbl[2], tg_text("确认键锁定本次选择", "ENTER TO LOCK"));
    }
    else
    {
        snprintf(buf, sizeof(buf), "%s %s", tg_text("本局", "THIS"),
                 m_game.win ? tg_text("猜中", "HIT") : tg_text("猜错", "MISS"));
        lv_label_set_text(m_game_lbl[0], buf);
        snprintf(buf, sizeof(buf), "%s %u / 3", tg_text("得分", "SCORE"),
                 (unsigned)m_game.score);
        lv_label_set_text(m_game_lbl[1], buf);
        lv_label_set_text(m_game_lbl[2], m_game.round >= 2
                          ? tg_text("确认键看结果", "ENTER = RESULT")
                          : tg_text("确认键进入下一局", "ENTER = NEXT"));
    }
}

static void tg_refresh_status(void)
{
    char buf[40];

    snprintf(buf, sizeof(buf), "%s %u", tg_text("第", "GEN"), (unsigned)m_st.gen);
    lv_label_set_text(m_stat_val[0], buf);
    lv_label_set_text(m_stat_val[1], tg_text(TG_STAGE_ZH[m_st.stage], TG_STAGE_EN[m_st.stage]));
    snprintf(buf, sizeof(buf), "%u h", (unsigned)m_st.hours_alive);
    lv_label_set_text(m_stat_val[2], buf);
    snprintf(buf, sizeof(buf), "%u g", (unsigned)m_st.weight);
    lv_label_set_text(m_stat_val[3], buf);
    snprintf(buf, sizeof(buf), "%u / 4", (unsigned)m_st.hunger);
    lv_label_set_text(m_stat_val[4], buf);
    snprintf(buf, sizeof(buf), "%u / 4", (unsigned)m_st.happy);
    lv_label_set_text(m_stat_val[5], buf);
    snprintf(buf, sizeof(buf), "%u / 5", (unsigned)m_st.disc);
    lv_label_set_text(m_stat_val[6], buf);
    snprintf(buf, sizeof(buf), "%u / 3", (unsigned)m_st.poop);
    lv_label_set_text(m_stat_val[7], buf);
}

static void tg_refresh(void)
{
    switch(m_screen)
    {
    case TG_SCR_FOOD:   tg_refresh_food();   break;
    case TG_SCR_GAME:   tg_refresh_game();   break;
    case TG_SCR_STATUS: tg_refresh_status(); break;
    case TG_SCR_PET:
    default:            tg_refresh_pet();    break;
    }
    m_last_min = tg_minute_of_day();
    m_force_refresh = 0;
}

/*********************************************************************
 * 页面重建
 */

static void tg_rebuild(void)
{
    if(m_body == NULL)
    {
        return;
    }

    lv_obj_clean(m_body);

    m_img = NULL;
    m_lbl_hdr_r = NULL;
    m_lbl_name = NULL;
    m_lbl_sub = NULL;
    m_lbl_alert = NULL;
    m_lbl_cap = NULL;
    m_lbl_msg = NULL;
    memset(m_heart, 0, sizeof(m_heart));
    memset(m_disc, 0, sizeof(m_disc));
    memset(m_icon_box, 0, sizeof(m_icon_box));
    memset(m_icon_lbl, 0, sizeof(m_icon_lbl));
    memset(m_lbl_info, 0, sizeof(m_lbl_info));
    memset(m_food_card, 0, sizeof(m_food_card));
    memset(m_game_lbl, 0, sizeof(m_game_lbl));
    memset(m_stat_val, 0, sizeof(m_stat_val));

    m_ui_screen = (uint8_t)m_screen;

    switch(m_screen)
    {
    case TG_SCR_FOOD:
        tg_build_food();
        break;
    case TG_SCR_GAME:
        tg_build_game();
        break;
    case TG_SCR_STATUS:
        tg_build_status();
        break;
    case TG_SCR_PET:
    default:
        tg_build_pet();
        break;
    }

    app_shell_set_text(&m_shell,
                       m_screen == TG_SCR_PET
                           ? tg_text("↑↓ 选择图标   确认键执行   双击返回   长按休眠",
                                     "UP/DOWN  ENTER 2X BACK HOLD SLEEP")
                           : tg_text("确认键执行   双击返回", "ENTER  2X BACK"),
                       m_screen == TG_SCR_PET ? tg_text("宠物", "PET")
                           : (m_screen == TG_SCR_FOOD ? tg_text("食物", "FOOD")
                           : (m_screen == TG_SCR_GAME ? tg_text("游戏", "GAME")
                                                      : tg_text("状态", "STATUS"))));

    tg_refresh();
}

/*********************************************************************
 * 定时器
 */

static void tg_tick_cb(lv_timer_t *timer)
{
    uint32_t now = tg_now_sec();

    (void)timer;

    /* 0. 模型强制换页（如死亡）：先重建，再按新页面刷新 */
    if(m_body == NULL)
    {
        return;     /* 页面已销毁：定时器可能还没被删干净 */
    }
    if(m_need_rebuild)
    {
        m_need_rebuild = 0;
        tg_rebuild();
        return;
    }

    /* 1. 结算真实经过时间（含休眠 / 未打开期间的欠账） */
    if(m_st.last_ts == 0u)
    {
        m_st.last_ts = now;
    }
    else if(now > m_st.last_ts)
    {
        tg_simulate(now - m_st.last_ts);
        m_st.last_ts = now;
    }

    /* 2. 帧动画由 tg_anim_cb（300ms）驱动，这里只处理文本 */

    /* 3. 文本：分钟变化 / 强制刷新 / 切换页面后才更新 */
    if(m_force_refresh || m_last_min != tg_minute_of_day())
    {
        tg_refresh();
    }
}

/*********************************************************************
 * 按键（ui_task）
 */

static void tg_on_key(input_press_type_t key)
{
    switch(m_screen)
    {
    case TG_SCR_PET:
        /* 方向与主菜单一致：上 = 下一项（向右），下 = 上一项（向左） */
        if(key == INPUT_PRESS_UP)
        {
            m_icon = (uint8_t)((m_icon + 1) % TG_ICON_NUM);
            tg_refresh();
        }
        else if(key == INPUT_PRESS_DOWN)
        {
            m_icon = (uint8_t)((m_icon + TG_ICON_NUM - 1) % TG_ICON_NUM);
            tg_refresh();
        }
        else if(key == INPUT_PRESS_SHORT)
        {
            tg_do_icon();
            tg_refresh();
        }
        break;

    case TG_SCR_FOOD:
        if(key == INPUT_PRESS_UP || key == INPUT_PRESS_DOWN)
        {
            m_food_sel ^= 1u;
            tg_refresh();
        }
        else if(key == INPUT_PRESS_SHORT)
        {
            tg_feed(m_food_sel);
        }
        break;

    case TG_SCR_GAME:
        if(m_game.phase == 0)
        {
            if(key == INPUT_PRESS_UP)      { m_game.pick = 0; tg_refresh(); }
            else if(key == INPUT_PRESS_DOWN){ m_game.pick = 1; tg_refresh(); }
            else if(key == INPUT_PRESS_SHORT){ tg_game_confirm(); }
        }
        else if(key == INPUT_PRESS_SHORT)
        {
            tg_game_confirm();
        }
        break;

    case TG_SCR_STATUS:
        if(key == INPUT_PRESS_SHORT)
        {
            if(!TG_FLAG_GET(TG_FLAG_ALIVE))
            {
                /* 死亡后确认 = 重生（代数 +1） */
                uint32_t gen = m_st.gen + 1u;

                m_st = TG_STATE_DEFAULT;
                m_st.gen = gen;
                m_st.last_ts = tg_now_sec();
                tg_set_msg(tg_text("新生 · 第 N 代", "NEW LIFE"));
            }
            m_screen = TG_SCR_PET;
            tg_rebuild();
        }
        break;

    default:
        break;
    }
}

/*********************************************************************
 * 页面生命周期（ui_task）
 */

static void tg_page_create(lv_obj_t *root)
{
    lv_obj_t *body;

    m_screen = TG_SCR_PET;
    m_icon = 0;
    m_ui_screen = (uint8_t)m_screen;
    m_force_refresh = 1;
    m_last_min = -1;
    m_msg[0] = '\0';
    m_frame = 0;
    m_last_anim = -1;
    m_evolve_until_ms = 0;

    if(m_px == NULL)
    {
        /* 200×200 L8 帧缓冲（40KB）+ 160×160 精灵兜底（25.6KB），都放 PSRAM */
        m_px = (uint8_t *)heap_caps_malloc((size_t)TAMAGO_ANIM_W * TAMAGO_ANIM_H,
                                           MALLOC_CAP_SPIRAM);
        m_sprite = (uint8_t *)heap_caps_malloc((size_t)TAMAGO_SPRITE_W * TAMAGO_SPRITE_H,
                                               MALLOC_CAP_SPIRAM);
        if(m_px == NULL)
        {
            sys_loge(APP_TAMAGO_TAG, "frame buffer alloc failed");
        }
    }
    if(m_px != NULL)
    {
        memset(&m_dsc, 0, sizeof(m_dsc));
        m_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
        m_dsc.header.cf = LV_COLOR_FORMAT_L8;
        m_dsc.header.w = TAMAGO_ANIM_W;
        m_dsc.header.h = TAMAGO_ANIM_H;
        m_dsc.header.stride = TAMAGO_ANIM_W;
        m_dsc.data_size = (uint32_t)TAMAGO_ANIM_W * TAMAGO_ANIM_H;
        m_dsc.data = m_px;
        tg_draw_frame();
    }

    app_shell_build(root, tg_text("↑↓ 选择图标   确认键执行   双击返回   长按休眠",
                                  "UP/DOWN  ENTER 2X BACK HOLD SLEEP"),
                    tg_text("宠物", "PET"), &m_shell);

    body = lv_obj_create(root);
    lv_obj_remove_style_all(body);
    lv_obj_set_scrollable(body, false);
    lv_obj_set_size(body, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_top(body, SHELL_BODY_TOP, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(body, SHELL_BODY_BOTTOM, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(body, SHELL_BODY_PAD_X, LV_PART_MAIN);
    m_body = body;

    /* 进页面先结算一次“上次到现在”的欠账 */
    {
        uint32_t now = tg_now_sec();

        if(m_st.last_ts != 0u && now > m_st.last_ts)
        {
            tg_simulate(now - m_st.last_ts);
        }
        m_st.last_ts = now;
    }

    tg_rebuild();

    m_timer = lv_timer_create(tg_tick_cb, TG_TICK_MS, NULL);
    if(m_timer == NULL)
    {
        sys_loge(APP_TAMAGO_TAG, "create tick timer failed");
    }
    m_anim_timer = lv_timer_create(tg_anim_cb, TG_FRAME_MS, NULL);
    if(m_anim_timer == NULL)
    {
        sys_loge(APP_TAMAGO_TAG, "create anim timer failed");
    }

    app_shell_request_status();
    app_shell_start_tick(&m_shell, NULL);

    sys_logi(APP_TAMAGO_TAG, "create tamago page (stage=%u hunger=%u happy=%u)",
             (unsigned)m_st.stage, (unsigned)m_st.hunger, (unsigned)m_st.happy);
}

static void tg_page_destroy(void)
{
    /* 定时器不在 root 对象树里，必须自己删；否则会挂在已销毁的控件上 */
    if(m_timer != NULL)
    {
        lv_timer_delete(m_timer);
        m_timer = NULL;
    }
    if(m_anim_timer != NULL)
    {
        lv_timer_delete(m_anim_timer);
        m_anim_timer = NULL;
    }
    /* 先断开 LVGL 对 PSRAM 缓冲的引用，再释放 */
    if(m_img != NULL)
    {
        lv_image_set_src(m_img, NULL);
        m_img = NULL;
    }
    if(m_px != NULL)
    {
        heap_caps_free(m_px);
        m_px = NULL;
    }
    if(m_sprite != NULL)
    {
        heap_caps_free(m_sprite);
        m_sprite = NULL;
    }
    m_body = NULL;
    app_shell_release(&m_shell);
}

static void tg_page_on_msg(uint32_t cmd, const void *data, uint8_t len)
{
    if(cmd == TG_MSG_BACK)
    {
        if(m_screen != TG_SCR_PET)
        {
            m_screen = TG_SCR_PET;
            tg_rebuild();
        }
        return;
    }
    (void)app_shell_handle_msg(&m_shell, cmd, data, len);
}

static void tg_page_on_key(input_press_type_t key)
{
    tg_on_key(key);
}

/*********************************************************************
 * app_task 侧导航钩子
 *
 * 只读 m_ui_screen（单字节），不碰 LVGL；要让页面退回主界面，
 * 必须投递消息到 ui_task。
 */
static bool tg_app_back(void)
{
    if(m_ui_screen == (uint8_t)TG_SCR_PET)
    {
        return false;   /* 主界面：交给框架回主菜单 */
    }
    (void)ui_core_post(TG_MSG_BACK, NULL, 0);
    return true;
}

static bool tg_app_double(void)
{
    if(m_ui_screen == (uint8_t)TG_SCR_PET)
    {
        return false;   /* 主界面：双击回主菜单（框架默认） */
    }
    (void)ui_core_post(TG_MSG_BACK, NULL, 0);
    return true;
}

/*********************************************************************
 * GLOBAL VARIABLES
 */
static const app_ui_ops_t g_tamago_ui_ops = {
    .create  = tg_page_create,
    .destroy = tg_page_destroy,
    .on_msg  = tg_page_on_msg,
    .on_key  = tg_page_on_key,
};

const app_entry_t g_app_tamago_entry = {
    .id = APP_ID_TAMAGO,
    .name = "tamago",
    .data_dir = NULL,
    .keys = APP_KEY_UP | APP_KEY_DOWN | APP_KEY_SHORT,
    .tick_ms = 0,
    .events = NULL,
    .layer = APP_LAYER_UI,
    .ui_ops = &g_tamago_ui_ops,
    .on_event = app_shell_on_event,
    .state = &m_st,
    .state_size = (uint16_t)sizeof(m_st),
    .state_ver = 1,
    .state_default = &TG_STATE_DEFAULT,
    .on_back = tg_app_back,
    .on_double = tg_app_double,
};

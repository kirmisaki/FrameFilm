#ifndef __TAMAGO_FRAMES_H__
#define __TAMAGO_FRAMES_H__

/*********************************************************************
 * 拓麻歌子状态动画帧（由 tools/ui-mockup/video2frames.html 从
 * assets/app/tamago 下的 6 段 mp4 抽出：居中裁方 → 抖动 → 1-bit）
 *
 * 数据以 .bin 形式由 CMake 的 EMBED_FILES 嵌入 flash：
 *   200×200，5000 B/帧，1=黑 0=白，MSB 优先、行优先。
 *********************************************************************/

#include <stdint.h>

#define TAMAGO_ANIM_W               (240)
#define TAMAGO_ANIM_H               (240)
#define TAMAGO_ANIM_BYTES_FRAME     (((TAMAGO_ANIM_W) * (TAMAGO_ANIM_H) + 7) / 8)
#define TAMAGO_ANIM_FRAMES          (16)

typedef enum {
    TAMAGO_ANIM_IDLE = 0,
    TAMAGO_ANIM_HAPPY,
    TAMAGO_ANIM_EAT,
    TAMAGO_ANIM_SICK,
    TAMAGO_ANIM_SLEEP,
    TAMAGO_ANIM_EVOLVE,
    TAMAGO_ANIM_NUM,
} tamago_anim_t;

/* 每段动画的数据首地址（指向 flash 里的 1-bit 帧序列） */
extern const uint8_t *const tamago_anim_data[TAMAGO_ANIM_NUM];

#endif /* __TAMAGO_FRAMES_H__ */

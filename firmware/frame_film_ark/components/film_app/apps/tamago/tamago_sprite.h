#ifndef __TAMAGO_SPRITE_H__
#define __TAMAGO_SPRITE_H__

/*********************************************************************
 * 拓麻歌子 1-bit 精灵绘制（纯程序化，无外部素材）
 *
 * 在 40×40 逻辑格上作画，再按 4×4 放大到 160×160 的 L8 缓冲
 * （0x00 = 黑，0xFF = 白）。这样固件里没有图片解码与 flash 素材，
 * 也天然是电子纸的二值风格。
 *********************************************************************/

#include <stdint.h>

#define TAMAGO_SPRITE_W     (160)
#define TAMAGO_SPRITE_H     (160)

/* 表情 / 状态 */
typedef enum {
    TAMAGO_FACE_IDLE = 0,
    TAMAGO_FACE_HAPPY,
    TAMAGO_FACE_EAT,
    TAMAGO_FACE_SICK,
    TAMAGO_FACE_SLEEP,
    TAMAGO_FACE_EVOLVE,
    TAMAGO_FACE_DEAD,
} tamago_face_t;

/**
 * @brief 渲染一帧
 *
 * @param buf    目标缓冲（TAMAGO_SPRITE_W * TAMAGO_SPRITE_H，L8）
 * @param face   表情
 * @param stage  成长阶段 0..4（0 = 蛋）
 * @param frame  连续帧号（用于呼吸/眨眼/耳朵摆动，约 4 帧/秒）
 * @param poop   便便数量 0..3（>0 时在右下角画）
 */
void tamago_sprite_render(uint8_t *buf, uint8_t face, uint8_t stage,
                          uint32_t frame, uint8_t poop);

#endif /* __TAMAGO_SPRITE_H__ */

/*********************************************************************
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * FileName : /film_app/apps/tamago/tamago_frames.c
 * Description: 状态动画帧表（数据由 EMBED_FILES 嵌入，见 CMakeLists）
 *
 *********************************************************************/

#include "tamago_frames.h"

/* ESP-IDF EMBED_FILES 生成的符号：文件名部分参与符号名 */
extern const uint8_t frames_idle_bin_start[]   asm("_binary_frames_idle_bin_start");
extern const uint8_t frames_happy_bin_start[]  asm("_binary_frames_happy_bin_start");
extern const uint8_t frames_eat_bin_start[]    asm("_binary_frames_eat_bin_start");
extern const uint8_t frames_sick_bin_start[]   asm("_binary_frames_sick_bin_start");
extern const uint8_t frames_sleep_bin_start[]  asm("_binary_frames_sleep_bin_start");
extern const uint8_t frames_evolve_bin_start[] asm("_binary_frames_evolve_bin_start");

const uint8_t *const tamago_anim_data[TAMAGO_ANIM_NUM] = {
    frames_idle_bin_start,
    frames_happy_bin_start,
    frames_eat_bin_start,
    frames_sick_bin_start,
    frames_sleep_bin_start,
    frames_evolve_bin_start,
};

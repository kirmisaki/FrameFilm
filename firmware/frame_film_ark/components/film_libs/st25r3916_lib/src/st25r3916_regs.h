/*
 * SPDX-License-Identifier: MIT
 *
 * ST25R3916 register / command / bit definitions (space A and space B).
 * Trimmed to what this library uses; names follow the datasheet.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Register/command opcode trailers (first SPI byte). */
#define ST25R_OP_WRITE_REG 0x00u  /* 00xxxxxx */
#define ST25R_OP_READ_REG  0x40u  /* 01xxxxxx */
#define ST25R_OP_LOAD_FIFO 0x80u
#define ST25R_OP_LOAD_PT_A 0xA0u
#define ST25R_OP_LOAD_PT_F 0xA8u
#define ST25R_OP_LOAD_PT_TSN 0xACu
#define ST25R_OP_READ_PT   0xBFu
#define ST25R_OP_READ_FIFO 0x9Fu
#define ST25R_OP_SPACE_B   0xF8u /* | space B addr (0x00..0x3F) */
#define ST25R_REG_MASK     0x3Fu

/* Space A registers */
#define ST25R_REG_IO_CONF1            0x00
#define ST25R_REG_IO_CONF2            0x01
#define ST25R_REG_OP_CONTROL          0x02
#define ST25R_REG_MODE                0x03
#define ST25R_REG_BITRATE             0x04
#define ST25R_REG_ISO14443A           0x05
#define ST25R_REG_NFCIP1_PT           0x08
#define ST25R_REG_AUX_DEF             0x0A
#define ST25R_REG_RX_CONF1            0x0B
#define ST25R_REG_RX_CONF2            0x0C
#define ST25R_REG_RX_CONF3            0x0D
#define ST25R_REG_RX_CONF4            0x0E
#define ST25R_REG_MASK_RX_TIMER       0x0F
#define ST25R_REG_NO_RESP_TIMER1      0x10
#define ST25R_REG_NO_RESP_TIMER2      0x11
#define ST25R_REG_TIMER_EMV           0x12
#define ST25R_REG_MASK_MAIN_IRQ       0x16
#define ST25R_REG_MASK_TIMER_NFC_IRQ  0x17
#define ST25R_REG_MASK_ERR_WUP_IRQ    0x18
#define ST25R_REG_MASK_PT_IRQ         0x19
#define ST25R_REG_MAIN_IRQ            0x1A
#define ST25R_REG_TIMER_NFC_IRQ       0x1B
#define ST25R_REG_ERR_WUP_IRQ         0x1C
#define ST25R_REG_PT_IRQ              0x1D
#define ST25R_REG_FIFO_STATUS1        0x1E
#define ST25R_REG_FIFO_STATUS2        0x1F
#define ST25R_REG_COLLISION           0x20
#define ST25R_REG_PT_DISPLAY          0x21
#define ST25R_REG_NUM_TX_BYTES1       0x22
#define ST25R_REG_NUM_TX_BYTES2       0x23
#define ST25R_REG_BITRATE_DISPLAY     0x24
#define ST25R_REG_ANT_TUNE1           0x26
#define ST25R_REG_ANT_TUNE2           0x27
#define ST25R_REG_TX_DRIVER           0x28
#define ST25R_REG_PT_MOD              0x29
#define ST25R_REG_EFD_ACT_THRESHOLD   0x2A
#define ST25R_REG_EFD_DEACT_THRESHOLD 0x2B
#define ST25R_REG_AUX_DISPLAY         0x31
#define ST25R_REG_IC_IDENTITY         0x3F

/* Space B registers (send the 16 bit value ST25R_B(reg) as the address) */
#define ST25R_B(reg) (0xFB00u | ((reg) & ST25R_REG_MASK))
#define ST25R_REG_B_EMD_SUPPRESSION  0x05
#define ST25R_REG_B_AUX_MODULATION   0x28
#define ST25R_REG_B_RES_AM_MOD       0x2A
#define ST25R_REG_B_OVERSHOOT_1      0x30
#define ST25R_REG_B_OVERSHOOT_2      0x31
#define ST25R_REG_B_UNDERSHOOT_1     0x32
#define ST25R_REG_B_UNDERSHOOT_2     0x33

/* Direct commands */
#define ST25R_CMD_SET_DEFAULT          0xC1
#define ST25R_CMD_STOP_ALL_ACTIVITIES  0xC2
#define ST25R_CMD_TRANSMIT_WITH_CRC    0xC4
#define ST25R_CMD_TRANSMIT_WITHOUT_CRC 0xC5
#define ST25R_CMD_TRANSMIT_REQA        0xC6
#define ST25R_CMD_TRANSMIT_WUPA        0xC7
#define ST25R_CMD_NFC_INITIAL_FIELD_ON 0xC8
#define ST25R_CMD_GO_TO_SENSE          0xCD
#define ST25R_CMD_GO_TO_SLEEP          0xCE
#define ST25R_CMD_MASK_RECEIVE_DATA    0xD0
#define ST25R_CMD_UNMASK_RECEIVE_DATA  0xD1
#define ST25R_CMD_RESET_RX_GAIN        0xD5
#define ST25R_CMD_ADJUST_REGULATORS    0xD6
#define ST25R_CMD_CLEAR_FIFO           0xDB
#define ST25R_CMD_ENTER_TRANSPARENT    0xDC
#define ST25R_CMD_START_NO_RESP_TIMER  0xE3
#define ST25R_CMD_STOP_NO_RESP_TIMER   0xE8
#define ST25R_CMD_SPACE_B_ACCESS       0xFB
#define ST25R_CMD_TEST_ACCESS          0xFC

/* REG_OP_CONTROL bits */
#define ST25R_OP_EN        0x80
#define ST25R_OP_RX_EN     0x40
#define ST25R_OP_TX_EN     0x08
#define ST25R_OP_WU        0x04
#define ST25R_OP_EN_FD_MASK 0x03

/* REG_MODE bits */
#define ST25R_MODE_TARG        0x80
#define ST25R_MODE_OM_MASK     0x78
#define ST25R_MODE_OM_ISO14443A (0x01u << 3)
#define ST25R_MODE_BITRATE_DETECTION (0x09u << 3)
#define ST25R_MODE_TARG_BITRATE (ST25R_MODE_TARG | ST25R_MODE_BITRATE_DETECTION)
#define ST25R_MODE_LISTEN_NFC_A ((0x01u << 3) | ST25R_MODE_TARG)

/* REG_ISO14443A bits */
#define ST25R_ISO_NO_TX_PAR 0x80
#define ST25R_ISO_NO_RX_PAR 0x40
#define ST25R_ISO_NFC_F0   0x20
#define ST25R_ISO_ANTCL    0x01

/* REG_NFCIP1_PT bits */
#define ST25R_PT_D_AC_AP2P      0x08
#define ST25R_PT_D_212_424_1R   0x04
#define ST25R_PT_D_106_AC_A     0x01

/* REG_AUX_DEF bits */
#define ST25R_AUX_NO_CRC_RX 0x80
#define ST25R_AUX_DIS_CORR  0x04
#define ST25R_AUX_UID_LEN_MASK 0x30
#define ST25R_AUX_UID_7     0x10

/* REG_TIMER_EMV bits */
#define ST25R_TIMER_MRT_STEP 0x08
#define ST25R_TIMER_NRT_STEP 0x01

/* REG_IO_CONF2 bits */
#define ST25R_IO2_SUP3V      0x80
#define ST25R_IO2_AAT_EN     0x20
#define ST25R_IO2_IO_DRV_LVL 0x04
#define ST25R_IO2_MISO_PD1   0x08
#define ST25R_IO2_MISO_PD2   0x10

/* Interrupt bits, as 32 bit value: main<<24 | nfc/timer<<16 | error<<8 | passive */
#define ST25R_IRQ_OSC       (0x80u << 24)
#define ST25R_IRQ_WL        (0x40u << 24)
#define ST25R_IRQ_RXS       (0x20u << 24)
#define ST25R_IRQ_RXE       (0x10u << 24)
#define ST25R_IRQ_TXE       (0x08u << 24)
#define ST25R_IRQ_COL       (0x04u << 24)
#define ST25R_IRQ_NRE       (0x40u << 16)
#define ST25R_IRQ_EON       (0x10u << 16)
#define ST25R_IRQ_EOF       (0x08u << 16)
#define ST25R_IRQ_CAC       (0x04u << 16)
#define ST25R_IRQ_CAT       (0x02u << 16)
#define ST25R_IRQ_NFCT      (0x01u << 16)
#define ST25R_IRQ_CRC       (0x80u << 8)
#define ST25R_IRQ_PAR       (0x40u << 8)
#define ST25R_IRQ_ERR2      (0x20u << 8)
#define ST25R_IRQ_ERR1      (0x10u << 8)
#define ST25R_IRQ_ERROR_MASK (0xFFu << 8)
#define ST25R_IRQ_PPON2     (0x80u << 0)
#define ST25R_IRQ_SL_WL     (0x40u << 0)
#define ST25R_IRQ_APON      (0x20u << 0)
#define ST25R_IRQ_RXE_PTA   (0x10u << 0)
#define ST25R_IRQ_WU_F      (0x08u << 0)
#define ST25R_IRQ_WU_AX     (0x02u << 0)
#define ST25R_IRQ_WU_A      (0x01u << 0)

/* Passive target display states */
#define ST25R_PTA_POWER_OFF 0x00
#define ST25R_PTA_IDLE      0x01
#define ST25R_PTA_READY_L1  0x02
#define ST25R_PTA_READY_L2  0x03
#define ST25R_PTA_ACTIVE    0x05
#define ST25R_PTA_HALT      0x09

/* REG_AUX_DISPLAY bits */
#define ST25R_AUX_DISP_EFD_O  0x40
#define ST25R_AUX_DISP_TX_ON  0x20
#define ST25R_AUX_DISP_OSC_OK 0x10
#define ST25R_AUX_DISP_RX_ON  0x08

/* ISO14443-A command bytes */
#define ST25R_AC_REQA    0x26
#define ST25R_AC_WUPA    0x52
#define ST25R_AC_HLTA    0x50

/* FIFO depth */
#define ST25R_FIFO_DEPTH 512

/* Timing helpers (13.56 MHz carrier). */
static inline uint8_t st25r_calc_mrt(uint32_t us, bool step512)
{
    const uint32_t step = step512 ? (512u * 1000000u) : (64u * 1000000u);
    uint32_t mrt = (uint32_t)(((uint64_t)us * 13560000u + step - 1) / step);
    if (mrt < 4u) {
        mrt = 4u;
    }
    if (mrt > 0xFFu) {
        mrt = 0xFFu;
    }
    return (uint8_t)mrt;
}

static inline uint16_t st25r_calc_nrt(uint32_t ms, bool step4096)
{
    const uint32_t step = step4096 ? (4096u * 1000u) : (64u * 1000u);
    uint32_t nrt = (uint32_t)(((uint64_t)ms * 13560000u + step - 1) / step);
    if (nrt < 1u) {
        nrt = 1u;
    }
    if (nrt > 0xFFFFu) {
        nrt = 0xFFFFu;
    }
    return (uint16_t)nrt;
}

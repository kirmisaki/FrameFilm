#ifndef ARK_SIM_HAL_EPD_H
#define ARK_SIM_HAL_EPD_H
#include <stdint.h>
#include "hal_epd_abc9.h"
int hal_epd_display_abc9(const uint8_t *pixels, uint32_t size);
typedef void (*hal_epd_abc9_row_fn)(void *context, unsigned row, uint16_t *codes);
int hal_epd_display_abc9_rows(hal_epd_abc9_row_fn read_row, void *context);
void hal_epd_lock(void);
void hal_epd_unlock(void);
void hal_epd_display_init(void);
void hal_epd_display_8bpp_mode(const uint8_t *pixels, uint8_t mode);
void hal_epd_display_mono(const uint8_t *pixels);
void hal_epd_pwroff(void);
#define EPD_WIDTH 720
#define EPD_HEIGHT 480
#define EPD_PANEL_ID 0x02
void hal_epd_mono_session_begin(void);
void hal_epd_mono_session_end(void);
#endif

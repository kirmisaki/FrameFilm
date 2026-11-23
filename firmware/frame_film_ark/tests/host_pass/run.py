#!/usr/bin/env python3
"""Exercise the actual ABC9 HAL row packing/phase runner with panel I/O mocked."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'components/film_hal/src/hal_epd_370.c').read_text(encoding='utf-8')
start = source.index('static void epd_abc9_write_plane(')
end = source.index('\n#endif', start)
actual = source[start:end]
harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include "hal_epd_abc9.h"
#define EPD_WIDTH 720
#define EPD_HEIGHT 480
#define EPD_INPUT_LINE 360
#define DTM1 0x10
#define BUSY_TIMEOUT_INIT_MS 10
#define ESP_ERR_INVALID_ARG -1
#define ESP_OK 0
#define EPD_PROFILE_STAGE_BEGIN(x)
#define EPD_PROFILE_STAGE_END(x)
#define EPD_PROFILE_BEGIN(x)
#define EPD_PROFILE_END()
#define sys_logi(...)
typedef int esp_err_t;
typedef void (*hal_epd_abc9_row_fn)(void *, unsigned, uint16_t *);
static int m_spectra_state;
static int qual_phase1, qual_phase2, qual_phase3;
static unsigned initializations, clears, refreshes, rows_written, phase, invalid_code;
static unsigned code_at(unsigned x, unsigned y) { return (x * 37u + y * 91u) & 511u; }
static void epd_lock(void) { }
static void epd_unlock(void) { }
static void hal_epd_pwroff(void) { }
static void hal_epd_display_init(void) { ++initializations; }
static void epd_spectra_init(void) { }
static void epd_spectra_full_clear(void) { ++clears; }
static void epd_spectra_wave(int wave) { (void)wave; }
static void epd_spectra_refresh(int a, int b) { (void)a; (void)b; ++refreshes; }
static void epd_wait_busy(int timeout) { (void)timeout; }
static void EPD_W21_WriteCMD(unsigned cmd) { assert(cmd == DTM1); ++phase; }
static void EPD_W21_WriteDATA_Bulk(const uint8_t *line, unsigned length)
{
    assert(length == 360 && phase >= 1 && phase <= 3);
    unsigned y = rows_written++ % 480u, shift = 9u - phase * 3u;
    for(unsigned x = 0; x < 720; x += 2)
        assert(line[x / 2] == ((((code_at(x, y) >> shift) & 7u) << 4) |
                               ((code_at(x + 1, y) >> shift) & 7u)));
}
'''
checks = r'''
static void read_row(void *ctx, unsigned y, uint16_t *codes)
{
    (void)ctx;
    for(unsigned x = 0; x < 720; ++x) codes[x] = code_at(x, y);
    if(invalid_code && y == 479) codes[719] = 512;
}
static void reset(void)
{ initializations = clears = refreshes = rows_written = phase = 0; }
static void complete(void)
{ assert(initializations == 1 && clears == 1 && refreshes == 3 && rows_written == 480 * 3); }
int main(void)
{
    uint8_t *native = malloc(EPD_ABC9_BODY_BYTES);
    assert(native);
    for(unsigned y = 0; y < 480; ++y)
        for(unsigned x = 0; x < 720; ++x)
        {
            unsigned i = (y * 720u + x) * 2u, code = code_at(x, y);
            native[i] = code & 255u; native[i + 1] = code >> 8;
        }
    assert(hal_epd_display_abc9(native, EPD_ABC9_BODY_BYTES) == 0);
    complete(); reset();
    assert(hal_epd_display_abc9_rows(read_row, NULL) == 0);
    complete(); reset();
    invalid_code = 1;
    assert(hal_epd_display_abc9_rows(read_row, NULL) == ESP_ERR_INVALID_ARG);
    assert(initializations == 0 && clears == 0 && refreshes == 0 && rows_written == 0);
    native[EPD_ABC9_BODY_BYTES - 1] = 2;
    assert(hal_epd_display_abc9(native, EPD_ABC9_BODY_BYTES) == ESP_ERR_INVALID_ARG);
    assert(hal_epd_display_abc9(native, EPD_ABC9_BODY_BYTES - 2) == ESP_ERR_INVALID_ARG);
    assert(hal_epd_display_abc9_rows(NULL, NULL) == ESP_ERR_INVALID_ARG);
    assert(initializations == 0 && clears == 0 && refreshes == 0 && rows_written == 0);
    free(native);
    puts("actual ABC9 HAL native/row parity, all A/B/C plane bytes, pre-refresh rejection PASS");
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='ark-pass-hal-') as temp:
    path = Path(temp)
    (path / 'test.c').write_text(harness + actual + checks, encoding='utf-8')
    subprocess.run(['cc', '-std=c11', '-Os', '-Wall', '-Wextra', '-Werror',
                    '-I' + str(root / 'components/film_hal/inc'), str(path / 'test.c'),
                    '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)

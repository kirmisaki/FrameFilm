/* Host acceptance: shared pass page + actual firmware ui_display.c, HAL only mocked. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <SDL2/SDL.h>
#include "lvgl.h"
#include "app_pass.h"
#include "app_menu.h"
#include "../src/simulator.h"
#include "ui_display.h"
#include "hal_epd_abc9.h"
#include "hal_epd.h"
#include "hal_pass_profile.h"

int simulator_heap_fail_after = -1;
static uint8_t frame[EPD_ABC9_BODY_BYTES];
static uint8_t snapshot[720 * 480 / 8];
static int frame_color, frame_calls;

int64_t esp_timer_get_time(void) { return (int64_t)SDL_GetTicks() * 1000 + 1; }
void hal_epd_lock(void) { }
void hal_epd_unlock(void) { }
void hal_epd_display_init(void) { }
void hal_epd_pwroff(void) { }
void hal_epd_display_8bpp_mode(const uint8_t *pixels, uint8_t mode)
{
    assert(mode == 0); /* Real ColorFast, not native six-color 4bpp. */
    memcpy(frame, pixels, 720u * 480u);
    frame_color = 1;
    frame_calls++;
}
void hal_epd_display_mono(const uint8_t *pixels)
{
    memcpy(frame, pixels, sizeof(snapshot));
    frame_color = 0;
    frame_calls++;
}

int hal_epd_display_abc9(const uint8_t *pixels, uint32_t size)
{
    assert(size == EPD_ABC9_BODY_BYTES && hal_epd_abc9_codes_valid(pixels, size));
    memcpy(frame, pixels, size);
    frame_color = 2;
    frame_calls++;
    return 0;
}

int hal_epd_display_abc9_rows(hal_epd_abc9_row_fn read_row, void *context)
{
    uint16_t codes[720];
    for(unsigned y = 0; y < 480; ++y)
    {
        read_row(context, y, codes);
        for(unsigned x = 0; x < 720; ++x)
        {
            assert(codes[x] <= 511);
            frame[(y * 720u + x) * 2u] = codes[x] & 255u;
            frame[(y * 720u + x) * 2u + 1u] = codes[x] >> 8;
        }
    }
    frame_color = 2;
    frame_calls++;
    return 0;
}

static uint8_t source_code(int x, int y, int color)
{
    const uint8_t codes[] = {0, 1, 2, 3, 5, 6};
    return color ? codes[(x + 7 * y) % 6] : (uint8_t)((x / 7 + y / 11) & 1);
}

static void fixture(const char *path, int w, int h, int color)
{
    const uint8_t header[16] = {'F', 'F', 'U', 'I', color ? 2 : 1, color ? 2 : 1,
                              w & 255, w >> 8, h & 255, h >> 8};
    FILE *fp = fopen(path, "wb");
    assert(fp);
    assert(fwrite(header, 1, sizeof(header), fp) == sizeof(header));
    for(int y = 0; y < h; y++) {
        for(int x = 0; x < w; x += color ? 2 : 8) {
            uint8_t byte = 0;
            for(int i = 0; i < (color ? 2 : 8); i++) {
                uint8_t code = source_code(x + i, y, color);
                if(color) byte |= code << (i ? 0 : 4);
                else if(code == 0) byte |= 0x80u >> i;
            }
            assert(fputc(byte, fp) != EOF);
        }
    }
    assert(fclose(fp) == 0);
}

static void render(void)
{
    simulator_process_requests();
    ui_display_invalidate_all();
    lv_refr_now(lv_display_get_default());
}

static void check_pixels(int w, int h, int color)
{
    static const uint8_t fast[7] = {0, 21, 63, 47, 0, 32, 52};
    const int ox = w == 480 ? 0 : 20, oy = h == 720 ? 0 : 57;
    assert(frame_calls > 0 && frame_color == color);
    for(int y = 0; y < h; y++) {
        for(int x = 0; x < w; x++) {
            /* Independent reference: logical portrait -> physical landscape. */
            int dx = 719 - (y + oy), dy = x + ox;
            size_t index = (size_t)dy * 720 + dx;
            uint8_t code = source_code(x, y, color);
            uint8_t actual = color ? frame[index] : (frame[index / 8] >> (7 - index % 8)) & 1;
            assert(actual == (color ? fast[code] : code == 0));
        }
    }
    assert(ui_display_snapshot_mono(snapshot, sizeof(snapshot)) == (color ? -2 : 0));
    fprintf(stderr, "real display %dx%d %s: all card pixels/rotation PASS\n", w, h, color ? "ColorFast" : "I1/mono");
}

static void reload(const char *path)
{
    setenv("ARK_PASS_PROFILE", path, 1);
    g_app_pass_entry.ui_ops->on_key(INPUT_PRESS_SHORT);
    render();
}

static void abc9_fixture(const char *path)
{
    const uint8_t header[32] = {0, 0x8C, 0x0A, 0, 0xD0, 2, 0xE0, 1,
                                0, 0x7F, 1, 0, 'A', 'B', 'C', '9'};
    assert(hal_epd_abc9_header_valid(header));
    FILE *fp = fopen(path, "wb");
    assert(fp && fwrite(header, 1, sizeof(header), fp) == sizeof(header));
    for(size_t i = 0; i < 720u * 480u; ++i) {
        unsigned code = (i * 37u + i / 720u) & 511u;
        assert(fputc(code & 255u, fp) != EOF);
        assert(fputc(code >> 8, fp) != EOF);
    }
    assert(fclose(fp) == 0);
}

static void check_abc9(void)
{
    assert(frame_color == 2);
    for(size_t i = 0; i < 720u * 480u; ++i) {
        unsigned code = (i * 37u + i / 720u) & 511u;
        assert(frame[2 * i] == (code & 255u) && frame[2 * i + 1] == (code >> 8));
    }
    assert(ui_display_snapshot_mono(snapshot, sizeof(snapshot)) == -2);
}

static void check_native_profile(const char *path)
{
    abc9_fixture(path);
    int calls = frame_calls;
    reload(path);
    assert(frame_calls == calls + 1);
    check_abc9(); /* Every A/B/C code reaches HAL verbatim, no second rotation. */
    calls = frame_calls;
    render();
    const app_status_t status = {.bat_pct = 17, .bt_on = 1, .bt_conn = 1};
    g_app_pass_entry.ui_ops->on_msg(APP_UI_MSG_STATUS, &status, sizeof(status));
    render();
    assert(frame_calls == calls); /* No full refresh for LVGL/status invalidation. */
    assert(ui_display_set_abc9_card(NULL, EPD_ABC9_BODY_BYTES) == -1);
    assert(ui_display_set_abc9_card(frame, EPD_ABC9_BODY_BYTES - 2) == -1);
    render();
    assert(frame_calls == calls);
    check_abc9();
    reload("/missing-native-profile");
    assert(frame_calls == calls);
    check_abc9();
    /* Malformed header, format, geometry, count, size, marker and high bits. */
    const long offsets[] = {0, 4, 6, 9, 10, 11, 12, 32 + 691199};
    for(size_t i = 0; i < sizeof(offsets) / sizeof(offsets[0]); ++i) {
        abc9_fixture(path);
        FILE *fp = fopen(path, "r+b");
        assert(fp && fseek(fp, offsets[i], SEEK_SET) == 0);
        assert(fputc(0xFF, fp) != EOF && fclose(fp) == 0);
        reload(path);
        assert(frame_calls == calls);
        check_abc9();
    }
    abc9_fixture(path);
    assert(truncate(path, EPD_ABC9_FILE_BYTES - 1) == 0);
    reload(path);
    assert(frame_calls == calls);
    check_abc9();
    abc9_fixture(path);
    FILE *fp = fopen(path, "ab");
    assert(fp && fputc(0, fp) != EOF && fclose(fp) == 0);
    reload(path);
    assert(frame_calls == calls);
    check_abc9();
    abc9_fixture(path);
    simulator_heap_fail_after = 0;
    reload(path);
    simulator_heap_fail_after = -1;
    assert(frame_calls == calls);
    check_abc9();
    reload(path); /* Successful explicit refresh, exactly once. */
    assert(frame_calls == ++calls);
    render();
    assert(frame_calls == calls);
    ui_display_set_output(0);
    render();
    assert(frame_calls == calls);
    ui_display_set_output(1); /* Replay after output/panel resume. */
    render();
    assert(frame_calls == ++calls);
    assert(g_app_pass_entry.sleep_face == APP_SLEEP_FACE_KEEP);
    check_abc9();
    simulator_show(g_app_menu_entry.ui_ops, &g_app_menu_entry);
    render();
    assert(frame_color == 0);
    setenv("ARK_PASS_PROFILE", path, 1);
    simulator_show(g_app_pass_entry.ui_ops, &g_app_pass_entry);
    render();
    check_abc9();
    /* Native -> FFUI failure and successful transition must preserve geometry. */
    fixture(path, 440, 608, 1);
    simulator_heap_fail_after = 1;
    reload(path);
    simulator_heap_fail_after = -1;
    check_abc9();
    reload(path);
    check_pixels(440, 608, 1);
    abc9_fixture(path);
    reload(path);
    check_abc9();
    fixture(path, 480, 720, 0);
    reload(path);
    check_pixels(480, 720, 0);
    fputs("native ABC9 profile exact bytes/refresh/arbitration/rollback PASS\n", stderr);
}

static void fast_fixture(const char *path, int frame_count)
{
    const uint8_t header[32] = {0, 0x46, 5, 0, 0xD0, 2, 0xE0, 1,
                               0, 3, frame_count, 0};
    FILE *fp = fopen(path, "wb");
    assert(fp && fwrite(header, 1, 32, fp) == 32);
    for(unsigned i = 0; i < PASS_FAST_BODY_BYTES; ++i)
        assert(fputc((i * 17u + i / 720u) & 63u, fp) != EOF);
    assert(fclose(fp) == 0);
}

static void check_fast(void)
{
    assert(frame_color == 1);
    for(unsigned i = 0; i < PASS_FAST_BODY_BYTES; ++i)
        assert(frame[i] == ((i * 17u + i / 720u) & 63u));
    assert(ui_display_snapshot_mono(snapshot, sizeof(snapshot)) == -2);
}

static void check_fast_profile(const char *path)
{
    for(int count = 0; count <= 1; ++count)
    {
        fast_fixture(path, count);
        int calls = frame_calls;
        reload(path);
        assert(frame_calls == calls + 1);
        check_fast();
        render();
        const app_status_t status = {.bat_pct = 17, .bt_on = 1};
        g_app_pass_entry.ui_ops->on_msg(APP_UI_MSG_STATUS, &status, sizeof(status));
        render();
        assert(frame_calls == calls + 1);
    }
    int calls = frame_calls;
    const long offsets[] = {0, 4, 6, 8, 9, 10, 11, 32, 345631};
    for(unsigned i = 0; i < sizeof(offsets) / sizeof(offsets[0]); ++i)
    {
        fast_fixture(path, 1);
        FILE *fp = fopen(path, "r+b");
        assert(fp && fseek(fp, offsets[i], SEEK_SET) == 0);
        assert(fputc(0xFF, fp) != EOF && fclose(fp) == 0);
        reload(path);
        assert(frame_calls == calls);
        check_fast();
    }
    fast_fixture(path, 1);
    assert(truncate(path, 345631) == 0);
    reload(path);
    assert(frame_calls == calls);
    check_fast();
    fast_fixture(path, 1);
    simulator_heap_fail_after = 0;
    reload(path);
    simulator_heap_fail_after = -1;
    assert(frame_calls == calls);
    check_fast();
    ui_display_set_output(0); render();
    ui_display_set_output(1); render();
    assert(frame_calls == calls + 1);
    check_fast();
    fputs("native ColorFast exact physical bytes/validation/exclusive/resume PASS\n", stderr);
}

static unsigned compact_avatar_code(unsigned x, unsigned y, int abc9)
{ return (x * 7u + y * 19u) & (abc9 ? 511u : 63u); }

static void compact_fixture(const char *path, int abc9, int avatar)
{
    uint8_t header[16] = {'F','F','U','I',3,abc9 ? 0x7F : 3,0xB8,1,0x60,2,avatar};
    FILE *fp = fopen(path, "wb");
    assert(fp && fwrite(header, 1, 16, fp) == 16);
    for(unsigned y = 0; y < 608; ++y)
        for(unsigned x = 0; x < 440; x += 8)
        {
            uint8_t byte = 0;
            for(unsigned b = 0; b < 8; ++b)
                if(source_code(x + b, y, 0) == 0) byte |= 0x80u >> b;
            assert(fputc(byte, fp) != EOF);
        }
    if(avatar)
        for(unsigned y = 0; y < 160; ++y)
            for(unsigned x = 0; x < 128; ++x)
            {
                unsigned code = compact_avatar_code(x, y, abc9);
                assert(fputc(code & 255u, fp) != EOF);
                if(abc9) assert(fputc(code >> 8, fp) != EOF);
            }
    assert(fclose(fp) == 0);
}

static void check_compact(int abc9, int avatar)
{
    assert(frame_color == (abc9 ? 2 : 1));
    for(unsigned y = 0; y < 608; ++y)
        for(unsigned x = 0; x < 440; ++x)
        {
            size_t physical = (x + 20u) * 720u + 719u - (y + 57u);
            unsigned got = abc9 ? hal_pass_le16(frame + physical * 2u) : frame[physical];
            unsigned expected;
            if(avatar && x >= 20 && x < 148 && y >= 143 && y < 303)
                expected = compact_avatar_code(x - 20, y - 143, abc9);
            else expected = source_code(x, y, 0) == 0 ? (abc9 ? 387 : 0) : (abc9 ? 75 : 21);
            assert(got == expected);
        }
    /* Outside the compact rectangle, the LVGL status/footer remain present,
       with only the two explicit neutral codes and both polarities. */
    unsigned black = 0, white = 0;
    for(unsigned y = 0; y < 720; ++y)
        for(unsigned x = 0; x < 480; ++x)
        {
            if(x >= 20 && x < 460 && y >= 57 && y < 665) continue;
            size_t i = x * 720u + 719u - y;
            unsigned code = abc9 ? hal_pass_le16(frame + i * 2u) : frame[i];
            if(code == (abc9 ? 387u : 0u)) ++black;
            else { assert(code == (abc9 ? 75u : 21u)); ++white; }
        }
    assert(black > 0 && white > 0);
    assert(ui_display_snapshot_mono(snapshot, sizeof(snapshot)) == -2);
}

static void check_compact_profiles(const char *path)
{
    for(int abc9 = 0; abc9 <= 1; ++abc9)
        for(int avatar = 0; avatar <= 1; ++avatar)
        {
            compact_fixture(path, abc9, avatar);
            reload(path);
            check_compact(abc9, avatar);
            const long bad_offsets[] = {4, 5, 6, 8, 10, 11, 15};
            for(unsigned i = 0; i < sizeof(bad_offsets) / sizeof(bad_offsets[0]); ++i)
            {
                compact_fixture(path, abc9, avatar);
                FILE *fp = fopen(path, "r+b");
                assert(fp && fseek(fp, bad_offsets[i], SEEK_SET) == 0);
                assert(fputc(0xFF, fp) != EOF && fclose(fp) == 0);
                reload(path);
                check_compact(abc9, avatar);
            }
            if(avatar)
            {
                compact_fixture(path, abc9, avatar);
                FILE *fp = fopen(path, "r+b");
                assert(fp && fseek(fp, 16 + PASS_COMPACT_I1_BYTES + (abc9 ? 1 : 0), SEEK_SET) == 0);
                assert(fputc(0xFF, fp) != EOF && fclose(fp) == 0);
                reload(path);
                check_compact(abc9, avatar);
            }
            compact_fixture(path, abc9, avatar);
            simulator_heap_fail_after = 0;
            reload(path);
            simulator_heap_fail_after = -1;
            check_compact(abc9, avatar);
            simulator_show(g_app_menu_entry.ui_ops, &g_app_menu_entry);
            render();
            assert(frame_color == 0);
            setenv("ARK_PASS_PROFILE", path, 1);
            simulator_show(g_app_pass_entry.ui_ops, &g_app_pass_entry);
            render();
            check_compact(abc9, avatar);
        }
    /* I006 -> ColorFast scratch allocation failure must retain the I006 card. */
    compact_fixture(path, 1, 1); reload(path);
    compact_fixture(path, 0, 1);
    simulator_heap_fail_after = 1;
    reload(path);
    simulator_heap_fail_after = -1;
    check_compact(1, 1);
    reload(path); check_compact(0, 1);
    fputs("compact I006/ColorFast avatar/no-avatar/rotation/shell/rollback PASS\n", stderr);
}

int main(int argc, char **argv)
{
    char path[] = "/tmp/ark-pass-display-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);
    close(fd);
    lv_init();
    assert(ui_display_set_abc9_card(frame, EPD_ABC9_BODY_BYTES) == -1);
    assert(ui_display_acquire() == 0);
    ui_display_set_output(1);
    unsetenv("ARK_PASS_PROFILE");
    simulator_show(g_app_pass_entry.ui_ops, &g_app_pass_entry);
    const int cases[][3] = {
        {440,608,0}, {480,720,1}, {480,720,0}, {440,608,1},
        {480,720,1}, {440,608,0}, {480,720,0}, {440,608,1}
    };
    for(size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        int w = cases[i][0], h = cases[i][1], color = cases[i][2];
        fixture(path, w, h, color);
        reload(path);
        check_pixels(w, h, color);
        reload("/missing-ark-pass-display.bin");
        check_pixels(w, h, color);
        assert(g_app_pass_entry.sleep_face == APP_SLEEP_FACE_KEEP);
        /* Leave a color/full card for a different page: no stale ColorFast layer. */
        simulator_show(g_app_menu_entry.ui_ops, &g_app_menu_entry);
        render();
        assert(!frame_color);
        assert(ui_display_snapshot_mono(snapshot, sizeof(snapshot)) == 0);
        setenv("ARK_PASS_PROFILE", path, 1);
        simulator_show(g_app_pass_entry.ui_ops, &g_app_pass_entry);
        render();
        check_pixels(w, h, color);
    }
    /* Allocation failure while changing geometry must preserve the previous mono card. */
    fixture(path, 440, 608, 0);
    reload(path);
    fixture(path, 480, 720, 1);
    simulator_heap_fail_after = 1; /* Card succeeds, both ColorFast frame allocations fail. */
    reload(path);
    simulator_heap_fail_after = -1;
    check_pixels(440, 608, 0);
    reload(path); /* Retry converges without leaving the page. */
    check_pixels(480, 720, 1);
    fixture(path, 480, 720, 0);
    reload(path);
    fixture(path, 440, 608, 1);
    simulator_heap_fail_after = 1;
    reload(path);
    simulator_heap_fail_after = -1;
    check_pixels(480, 720, 0);
    reload(path);
    check_pixels(440, 608, 1);

    check_native_profile(path);
    check_fast_profile(path);
    check_compact_profiles(path);
    if(argc == 2) {
        FILE *fp = fopen(argv[1], "rb");
        PassProfileInfo_t info;
        assert(fp && hal_pass_profile_inspect(fp, &info));
        uint8_t *body = malloc(info.body_size);
        assert(body && fread(body, 1, info.body_size, fp) == info.body_size && fclose(fp) == 0);
        int calls = frame_calls;
        reload(argv[1]);
        assert(frame_calls == calls + 1);
        if(info.format == PASS_FMT_ABC9 || info.format == PASS_FMT_FAST)
        {
            assert(frame_color == (info.format == PASS_FMT_ABC9 ? 2 : 1));
            assert(memcmp(frame, body, info.body_size) == 0);
            render();
            assert(frame_calls == calls + 1);
        }
        else
        {
            int abc9 = info.format == PASS_FMT_COMPACT_ABC9;
            assert(abc9 || info.format == PASS_FMT_COMPACT_FAST);
            assert(frame_color == (abc9 ? 2 : 1));
            for(unsigned y = 0; y < 608; ++y)
                for(unsigned x = 0; x < 440; ++x)
                {
                    size_t i = (x + 20u) * 720u + 719u - (y + 57u);
                    unsigned code = abc9 ? hal_pass_le16(frame + i * 2u) : frame[i];
                    unsigned expected;
                    if(info.avatar_planes && x >= 20 && x < 148 && y >= 143 && y < 303)
                    {
                        size_t a = (y - 143u) * 128u + x - 20u;
                        expected = abc9 ? hal_pass_le16(body + 33440u + a * 2u) : body[33440u + a];
                    }
                    else
                    {
                        size_t b = y * 440u + x;
                        int black = (body[b / 8u] >> (7u - b % 8u)) & 1u;
                        expected = black ? (abc9 ? 387 : 0) : (abc9 ? 75 : 21);
                    }
                    assert(code == expected);
                }
        }
        free(body);
        fputs("external profile -> real firmware -> HAL code/byte parity PASS\n", stderr);
    }
    int calls_before_stop = frame_calls;
    simulator_show(NULL, NULL); /* KEEP teardown must not paint a mono frame. */
    assert(frame_calls == calls_before_stop);
    assert(ui_display_snapshot_mono(snapshot, sizeof(snapshot)) == 0);
    ui_display_release();
    assert(ui_display_snapshot_mono(snapshot, sizeof(snapshot)) == -1);
    unlink(path);
    fputs("real pass display/transition/rollback PASS\n", stderr);
    return 0;
}

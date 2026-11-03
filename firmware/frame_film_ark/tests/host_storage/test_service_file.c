/* Host exercise of the real Ark file-service message handler and filesystem helpers. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <assert.h>
#include <errno.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../../components/film_service/inc/service_file.h"

static char host_film_dir[64];
static char host_anim_dir[64];
static int fail_next_write;
static int fail_next_close;
static int fail_part_rename;

static size_t host_fwrite(const void *ptr, size_t size, size_t count, FILE *stream)
{
    if(fail_next_write)
    {
        fail_next_write = 0;
        return fwrite(ptr, size, count / 2, stream);
    }
    return fwrite(ptr, size, count, stream);
}

static int host_rename(const char *from, const char *to)
{
    size_t n = strlen(from);
    const char *suffix = ".ffupload.part";
    size_t suffix_len = strlen(suffix);
    if(fail_part_rename && n >= suffix_len && strcmp(from + n - suffix_len, suffix) == 0)
    {
        fail_part_rename = 0;
        errno = EIO;
        return -1;
    }
    /* FatFs rename does not replace an existing destination. */
    if(access(to, F_OK) == 0)
    {
        errno = EEXIST;
        return -1;
    }
    return rename(from, to);
}

static int host_fclose(FILE *stream)
{
    int result = fclose(stream);
    if(fail_next_close)
    {
        fail_next_close = 0;
        errno = EIO;
        return EOF;
    }
    return result;
}

/* Keep the production save/list code intact; only the SD mount path changes. */
#undef FILM_DIR
#undef ANIM_DIR
#define FILM_DIR host_film_dir
#define ANIM_DIR host_anim_dir
#define fwrite host_fwrite
#define fclose host_fclose
#define rename host_rename
#include "../../components/film_service/src/service_file.c"
#undef fwrite
#undef fclose
#undef rename

static jmp_buf task_exit;
static file_msg_t queued_msg;
static int has_queued_msg;
static int file_saved_events;
static uint8_t last_auto_load;

QueueHandle_t xQueueCreate(unsigned count, unsigned item_size)
{
    (void)count; (void)item_size;
    return (QueueHandle_t)1;
}

BaseType_t xQueueSend(QueueHandle_t queue, const void *item, TickType_t wait)
{
    (void)queue; (void)wait;
    assert(!has_queued_msg);
    memcpy(&queued_msg, item, sizeof(queued_msg));
    has_queued_msg = 1;
    return pdPASS;
}

BaseType_t xQueueSendFromISR(QueueHandle_t queue, const void *item, BaseType_t *woken)
{
    (void)woken;
    return xQueueSend(queue, item, 0);
}

BaseType_t xQueueReceive(QueueHandle_t queue, void *item, TickType_t wait)
{
    (void)queue; (void)wait;
    if(!has_queued_msg) longjmp(task_exit, 1);
    memcpy(item, &queued_msg, sizeof(queued_msg));
    has_queued_msg = 0;
    return pdPASS;
}

SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (SemaphoreHandle_t)2; }
SemaphoreHandle_t xSemaphoreCreateBinary(void) { return (SemaphoreHandle_t)3; }

BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t wait)
{
    (void)wait;
    if(semaphore == m_save_done)
    {
        assert(has_queued_msg);
        if(setjmp(task_exit) == 0) file_task_handle(NULL);
    }
    return pdPASS;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore)
{
    (void)semaphore;
    return pdPASS;
}

BaseType_t xTaskCreate(void (*task)(void *), const char *name, unsigned stack,
                       void *arg, unsigned priority, TaskHandle_t *handle)
{
    (void)task; (void)name; (void)stack; (void)arg; (void)priority;
    if(handle) *handle = (TaskHandle_t)4;
    return pdPASS;
}

void vTaskDelete(TaskHandle_t task) { (void)task; }
void vTaskDelay(TickType_t ticks) { (void)ticks; }
TimerHandle_t xTimerCreate(const char *name, TickType_t period, BaseType_t reload,
                           void *id, void (*callback)(TimerHandle_t))
{
    (void)name; (void)period; (void)reload; (void)id; (void)callback;
    return (TimerHandle_t)5;
}
BaseType_t xTimerStart(TimerHandle_t timer, TickType_t wait)
{
    (void)timer; (void)wait;
    return pdPASS;
}
int hal_sd_get_status(void) { return SD_MOUNT; }
int sys_event_publish(sys_event_id_t id, const void *payload, uint16_t len)
{
    if(id == SYS_EVT_FILE_SAVED)
    {
        assert(payload && len == 1);
        last_auto_load = *(const uint8_t *)payload;
        file_saved_events++;
    }
    return 0;
}

static void path_for(char *out, size_t size, const char *name, const char *suffix)
{
    int n = snprintf(out, size, "%s/%s%s", host_film_dir, name, suffix);
    assert(n > 0 && (size_t)n < size);
}

static void write_bytes(const char *path, const uint8_t *bytes, size_t size)
{
    FILE *file = fopen(path, "wb");
    assert(file);
    assert(fwrite(bytes, 1, size, file) == size);
    assert(fclose(file) == 0);
}

static void expect_bytes(const char *path, const uint8_t *bytes, size_t size)
{
    uint8_t *actual = malloc(size);
    assert(actual);
    FILE *file = fopen(path, "rb");
    assert(file);
    assert(fread(actual, 1, size, file) == size);
    assert(fgetc(file) == EOF);
    assert(fclose(file) == 0);
    assert(memcmp(actual, bytes, size) == 0);
    free(actual);
}

static void film_header(uint8_t *film, uint16_t frames)
{
    uint32_t body_size = (720u * 480u / 8u) * frames;
    memset(film, 0, 32);
    for(unsigned i = 0; i < 4; i++) film[i] = (uint8_t)(body_size >> (8 * i));
    film[4] = 720 & 0xff;
    film[5] = 720 >> 8;
    film[6] = 480 & 0xff;
    film[7] = 480 >> 8;
    film[9] = 1; /* MonoFast */
    film[10] = frames & 0xff;
    film[11] = frames >> 8;
}

static void expect_absent(const char *path)
{
    struct stat st;
    assert(stat(path, &st) != 0 && errno == ENOENT);
}

static int save_data(const uint8_t *bytes, size_t size)
{
    uint8_t *copy = malloc(size);
    assert(copy);
    memcpy(copy, bytes, size);
    return service_file_save_data(FILE_SAVE_BLE, copy, (uint32_t)size);
}

static void remove_traces(const char *name)
{
    char path[128];
    path_for(path, sizeof(path), name, ""); remove(path);
    path_for(path, sizeof(path), name, ".ffupload.part"); remove(path);
    path_for(path, sizeof(path), name, ".ffupload.bak"); remove(path);
}

static void test_abc9_upload(const uint8_t *old_film, size_t old_size)
{
    const size_t size = 32u + 720u * 480u * 2u;
    uint8_t *abc9 = calloc(size + 2, 1);
    assert(abc9);
    char target[128];
    path_for(target, sizeof(target), "abc9.film", "");
    for(unsigned scenario = 0; scenario < 11; ++scenario)
    {
        film_header(abc9, 1);
        uint32_t body = (uint32_t)size - 32;
        for(unsigned i = 0; i < 4; ++i) abc9[i] = (uint8_t)(body >> (8 * i));
        abc9[9] = 0x7F;
        memcpy(abc9 + 12, "ABC9", 4);
        for(size_t i = 32; i < size; i += 2)
        {
            unsigned code = ((i - 32) / 2) % 512;
            abc9[i] = (uint8_t)code;
            abc9[i + 1] = (uint8_t)(code >> 8);
        }
        size_t uploaded = size;
        switch(scenario)
        {
        case 1: abc9[12] = 'X'; break;
        case 2: abc9[4] ^= 1; break;
        case 3: abc9[6] ^= 1; break;
        case 4: abc9[0] ^= 1; break;
        case 5: abc9[10] = 0; break;
        case 6: abc9[10] = 2; break;
        case 7: abc9[33] = 2; break;
        case 8: abc9[size - 1] = 0x80; break;
        case 9: uploaded -= 2; break;
        case 10: uploaded += 2; break;
        }
        int accepted = 0;
#if defined(CONFIG_ARK_EPD_ABC_PREVIEW) || defined(CONFIG_ARK_EPD_ABC_DIAG)
        accepted = scenario == 0;
#endif
        write_bytes(target, old_film, old_size);
        int events_before = file_saved_events;
        assert(service_file_save_start(FILE_SAVE_BLE, "abc9.film", (uint32_t)uploaded) == 0);
        assert(save_data(abc9, uploaded) == 0);
        assert(service_file_save_stop(FILE_SAVE_BLE, 0) == (accepted ? 0 : -1));
        expect_bytes(target, accepted ? abc9 : old_film, accepted ? uploaded : old_size);
        assert(file_saved_events == events_before + accepted);
        remove_traces("abc9.film");
    }
    free(abc9);
}

int main(void)
{
    char root[] = "/tmp/framefilm-ark-storage-XXXXXX";
    assert(mkdtemp(root));
    assert(snprintf(host_film_dir, sizeof(host_film_dir), "%s/film", root) < (int)sizeof(host_film_dir));
    assert(snprintf(host_anim_dir, sizeof(host_anim_dir), "%s/animation", root) < (int)sizeof(host_anim_dir));
    assert(mkdir(host_film_dir, 0700) == 0);
    assert(mkdir(host_anim_dir, 0700) == 0);

    memset(&m_file_state, 0, sizeof(m_file_state));
    m_file_state.sd_mounted = 1;
    snprintf(m_file_active_dir, sizeof(m_file_active_dir), "%s", host_film_dir);
    m_save_call_mutex = xSemaphoreCreateMutex();
    m_save_done = xSemaphoreCreateBinary();
    m_file_msg_hdl = (QueueHandle_t)1;
    m_file_list_mutex = xSemaphoreCreateMutex();

    static uint8_t old_film[32 + 720 * 480 / 8];
    static uint8_t new_film[sizeof(old_film)];
    const uint32_t film_size = sizeof(old_film);
    memset(old_film, 0x31, sizeof(old_film));
    memset(new_film, 0x52, sizeof(new_film));
    film_header(old_film, 1);
    film_header(new_film, 1);
    char target[128], part[128], backup[128];

    /* A full transfer publishes the new file and removes transaction traces. */
    path_for(target, sizeof(target), "full.film", "");
    path_for(part, sizeof(part), "full.film", ".ffupload.part");
    assert(service_file_save_start(FILE_SAVE_BLE, "full.film", film_size) == 0);
    assert(save_data(new_film, film_size) == 0);
    assert(service_file_save_stop(FILE_SAVE_BLE, 0) == 0);
    expect_bytes(target, new_film, film_size);
    expect_absent(part);
    assert(file_saved_events == 1);
    remove_traces("full.film");

    /* Replacing a loaded file must invalidate its old buffer as well as commit on FatFs. */
    path_for(target, sizeof(target), "replace.film", "");
    path_for(backup, sizeof(backup), "replace.film", ".ffupload.bak");
    write_bytes(target, old_film, film_size);
    file_list_refresh_event();
    file_load_event(0);
    assert(service_file_get_load_complete() == FILE_LOAD_STATE_DONE);
    assert(memcmp(service_file_get_buffer(), old_film, film_size) == 0);
    assert(service_file_save_start(FILE_SAVE_BLE, "replace.film", film_size) == 0);
    assert(save_data(new_film, film_size) == 0);
    assert(service_file_save_stop(FILE_SAVE_BLE, 1) == 0);
    expect_bytes(target, new_film, film_size);
    expect_absent(backup);
    assert(last_auto_load == 1);
    assert(service_file_get_load_complete() == FILE_LOAD_STATE_NONE);
    file_load_event(service_file_get_current_id());
    assert(service_file_get_load_complete() == FILE_LOAD_STATE_DONE);
    assert(service_file_get_buffer_size() == film_size);
    assert(memcmp(service_file_get_buffer(), new_film, film_size) == 0);
    file_free_buffer();
    remove_traces("replace.film");

    /* Complete transport bytes with a truncated film body must not replace old data. */
    path_for(target, sizeof(target), "header.film", "");
    write_bytes(target, old_film, film_size);
    int before_events = file_saved_events;
    assert(service_file_save_start(FILE_SAVE_BLE, "header.film", 32) == 0);
    assert(save_data(new_film, 32) == 0);
    assert(service_file_save_stop(FILE_SAVE_BLE, 1) == -1);
    expect_bytes(target, old_film, film_size);
    assert(file_saved_events == before_events);
    remove_traces("header.film");

    /* Frame-count routing preserves the source image and avoids auto-loading it. */
    static uint8_t animation[32 + 2 * 720 * 480 / 8];
    memset(animation, 0x65, sizeof(animation));
    film_header(animation, 2);
    path_for(target, sizeof(target), "routed.film", "");
    write_bytes(target, old_film, film_size);
    assert(service_file_save_start(FILE_SAVE_BLE, "routed.film", sizeof(animation)) == 0);
    assert(save_data(animation, sizeof(animation)) == 0);
    assert(service_file_save_stop(FILE_SAVE_BLE, 1) == 0);
    expect_bytes(target, old_film, film_size);
    char animation_path[128];
    assert(snprintf(animation_path, sizeof(animation_path), "%s/routed.film", host_anim_dir) < (int)sizeof(animation_path));
    expect_bytes(animation_path, animation, sizeof(animation));
    assert(last_auto_load == 0);
    assert(remove(animation_path) == 0);
    remove_traces("routed.film");

    /* A short transfer leaves the previous visible file intact. */
    path_for(target, sizeof(target), "short.film", "");
    path_for(part, sizeof(part), "short.film", ".ffupload.part");
    write_bytes(target, old_film, film_size);
    assert(service_file_save_start(FILE_SAVE_BLE, "short.film", film_size) == 0);
    assert(save_data(new_film, 16) == 0);
    assert(service_file_save_stop(FILE_SAVE_BLE, 0) == -1);
    expect_bytes(target, old_film, film_size);
    expect_absent(part);
    assert(service_file_save_abort(FILE_SAVE_BLE) == 0); /* STOP 已丢弃后取消仍成功。 */
    remove_traces("short.film");

    /* A partial fwrite poisons this upload but leaves the previous file. */
    path_for(target, sizeof(target), "write.film", "");
    write_bytes(target, old_film, film_size);
    assert(service_file_save_start(FILE_SAVE_BLE, "write.film", film_size) == 0);
    fail_next_write = 1;
    assert(save_data(new_film, film_size) == -1);
    assert(service_file_save_stop(FILE_SAVE_BLE, 0) == -1);
    expect_bytes(target, old_film, film_size);
    remove_traces("write.film");

    /* A close error also prevents replacement, even with all bytes written. */
    path_for(target, sizeof(target), "close.film", "");
    write_bytes(target, old_film, film_size);
    assert(service_file_save_start(FILE_SAVE_BLE, "close.film", film_size) == 0);
    assert(save_data(new_film, film_size) == 0);
    fail_next_close = 1;
    assert(service_file_save_stop(FILE_SAVE_BLE, 0) == -1);
    expect_bytes(target, old_film, film_size);
    remove_traces("close.film");

    /* Abort removes the part and immediately permits a clean retry. */
    path_for(target, sizeof(target), "abort.film", "");
    path_for(part, sizeof(part), "abort.film", ".ffupload.part");
    assert(service_file_save_start(FILE_SAVE_BLE, "abort.film", film_size) == 0);
    assert(save_data(old_film, 16) == 0);
    assert(service_file_save_start(FILE_SAVE_WIFI, "other.film", film_size) == -1);
    assert(service_file_save_abort(FILE_SAVE_WIFI) == -1);
    file_clean_dir(host_film_dir);
    struct stat active_part;
    assert(stat(part, &active_part) == 0);
    assert(service_file_save_abort(FILE_SAVE_BLE) == 0);
    expect_absent(part);
    assert(service_file_save_start(FILE_SAVE_BLE, "abort.film", film_size) == 0);
    assert(save_data(new_film, film_size) == 0);
    assert(service_file_save_stop(FILE_SAVE_BLE, 0) == 0);
    expect_bytes(target, new_film, film_size);
    remove_traces("abort.film");

    /* Failure after old -> .bak restores the old public file. */
    path_for(target, sizeof(target), "rename.film", "");
    path_for(part, sizeof(part), "rename.film", ".ffupload.part");
    path_for(backup, sizeof(backup), "rename.film", ".ffupload.bak");
    write_bytes(target, old_film, film_size);
    assert(service_file_save_start(FILE_SAVE_BLE, "rename.film", film_size) == 0);
    assert(save_data(new_film, film_size) == 0);
    fail_part_rename = 1;
    assert(service_file_save_stop(FILE_SAVE_BLE, 0) == -1);
    expect_bytes(target, old_film, film_size);
    expect_absent(part);
    expect_absent(backup);
    remove_traces("rename.film");

    /* Simulated restart: orphan part is discarded, backup is recovered. */
    path_for(target, sizeof(target), "reboot.film", "");
    path_for(part, sizeof(part), "reboot.film", ".ffupload.part");
    path_for(backup, sizeof(backup), "reboot.film", ".ffupload.bak");
    write_bytes(backup, old_film, film_size);
    write_bytes(part, new_film, film_size);
    file_list_refresh_event();
    expect_bytes(target, old_film, film_size);
    expect_absent(part);
    expect_absent(backup);
    assert(service_file_get_count() == 1);
    char listed[256];
    assert(service_file_get_filename_safe(0, listed, sizeof(listed)) == 0);
    assert(strcmp(listed, "reboot.film") == 0);
    remove_traces("reboot.film");

    test_abc9_upload(old_film, film_size);

    free(m_file_state.file_list);
    assert(rmdir(host_film_dir) == 0);
    assert(rmdir(host_anim_dir) == 0);
    assert(rmdir(root) == 0);
    puts("Ark host storage tests passed");
    return 0;
}

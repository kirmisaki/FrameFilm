#include <assert.h>
#include <stdio.h>

/* Exercise the production monitor state machine, including its static handlers. */
#include "../../components/film_service/src/service_monitor.c"

ServiceParam_Def_t g_service_param;
static bool direct_busy;
static bool wake_pressed;
static int cancel_count;
static int wifi_deinit_count;
static int timer_stop_count;
static int peripheral_deinit_count;
static int sleep_count;
static int timer_wakeup_count;
static bool app_ready;
static bool ble_file_closed;
static int app_prepare_count;
static int ble_prepare_count;
static bool wakeup_prepare_ok;
static int wakeup_prepare_count;

static bool prepare_app(void)
{
    ++app_prepare_count;
    return app_ready;
}

static void reset_case(void)
{
    g_service_param = (ServiceParam_Def_t){0};
    m_monitor_state = (monitor_state_t){0};
    m_monitor_state.wakeup_ticks = MONITOR_AUTO_SLEEP_TICK_COUNT;
    m_sleep_pending = false;
    direct_busy = false;
    wake_pressed = false;
    cancel_count = wifi_deinit_count = timer_stop_count = 0;
    peripheral_deinit_count = sleep_count = timer_wakeup_count = 0;
    app_ready = ble_file_closed = false;
    app_prepare_count = ble_prepare_count = 0;
    wakeup_prepare_ok = true;
    wakeup_prepare_count = 0;
    service_monitor_set_sleep_prepare_cb(prepare_app);
}

static void assert_no_teardown(void)
{
    assert(wifi_deinit_count == 0);
    assert(timer_stop_count == 0);
    assert(peripheral_deinit_count == 0);
    assert(sleep_count == 0);
}

static void test_disabled_auto_sleep(void)
{
    reset_case();
    for (int i = 0; i < MONITOR_AUTO_SLEEP_TICK_COUNT + 100; ++i)
        monitor_auto_sleep_manage_event();
    assert(!service_monitor_sleep_pending());
    assert(m_monitor_state.sleep_counter == 0);
    assert_no_teardown();
}

static void test_manual_sleep_waits_for_transfer_and_release(void)
{
    reset_case();
    g_service_param.sleep.sleep_mode = 0;
    g_service_param.sleep.sleep_auto = 1;
    g_service_param.sleep.sleep_time = 10;
    direct_busy = true;
    wake_pressed = true;
    monitor_manual_sleep_event();
    assert(service_monitor_sleep_pending());
    assert(timer_wakeup_count == 1);
    assert(cancel_count == 1);
    assert_no_teardown();

    monitor_auto_sleep_manage_event(); /* transfer is still restoring */
    assert_no_teardown();

    direct_busy = false; /* cancellation and session restore have completed */
    /* 75 checks at 200 ms cover 15 s of the wake-pin guard. */
    for (int i = 0; i < 75; ++i)
        monitor_auto_sleep_manage_event();
    assert_no_teardown();

    wake_pressed = false;
    monitor_auto_sleep_manage_event();
    assert(app_prepare_count == 1);
    assert(ble_prepare_count == 0);
    assert_no_teardown(); /* app/UI 尚未确认停屏，不得关闭 EPD。 */

    app_ready = true;
    monitor_auto_sleep_manage_event();
    assert(ble_prepare_count == 1);
    assert_no_teardown(); /* BLE 暂存文件仍打开，不得卸载 SD。 */

    ble_file_closed = true;
    monitor_auto_sleep_manage_event();
    assert(wifi_deinit_count == 1);
    assert(timer_stop_count == 1);
    assert(peripheral_deinit_count == 6);
    assert(sleep_count == 1);
}

static void test_auto_sleep_requires_both_barriers(void)
{
    reset_case();
    g_service_param.sleep.sleep_mode = 1;
    m_monitor_state.wakeup_ticks = 1;
    monitor_auto_sleep_manage_event();
    assert(service_monitor_sleep_pending());
    assert(app_prepare_count == 1);
    assert_no_teardown();
    app_ready = true;
    monitor_auto_sleep_manage_event();
    assert_no_teardown();
    ble_file_closed = true;
    monitor_auto_sleep_manage_event();
    assert(sleep_count == 1);
}

static void test_sleep_defers_when_wakeup_source_unavailable(void)
{
    reset_case();
    g_service_param.sleep.sleep_mode = 0;
    g_service_param.sleep.sleep_auto = 1;
    g_service_param.sleep.sleep_time = 10;
    app_ready = true;
    ble_file_closed = true;
    wakeup_prepare_ok = false;   /* ext0 配置失败：本次不许入睡 */

    monitor_manual_sleep_event();
    assert(service_monitor_sleep_pending());
    assert(wakeup_prepare_count == 1);
    /* 关键：唤醒源没配好，一步都别拆（否则会卡在"外设已卸却睡不着"） */
    assert(app_prepare_count == 0);
    assert(ble_prepare_count == 0);
    assert_no_teardown();

    wakeup_prepare_ok = true;    /* 恢复后由心跳重试走完正常路径 */
    monitor_auto_sleep_manage_event();
    assert(app_prepare_count == 1);
    assert(ble_prepare_count == 1);
    assert(sleep_count == 1);
}

static void test_auto_sleep_timeout_thresholds(void)
{
    const uint32_t seconds[] = {30, 120, 1800};

    for (unsigned i = 0; i < sizeof(seconds) / sizeof(seconds[0]); ++i)
    {
        reset_case();
        g_service_param.sleep.sleep_mode = 1;
        m_monitor_state.sleep_counter = MONITOR_AUTO_SLEEP_TICK_COUNT - 1;
        service_monitor_set_auto_sleep_timeout(seconds[i]);
        const uint32_t expected_ticks = seconds[i] * 5u;
        assert(m_monitor_state.wakeup_ticks == expected_ticks);
        assert(m_monitor_state.sleep_counter == 0);

        for (uint32_t tick = 1; tick < expected_ticks; ++tick)
            monitor_auto_sleep_manage_event();
        assert(m_monitor_state.sleep_counter == expected_ticks - 1);
        assert(!service_monitor_sleep_pending());
        assert_no_teardown();

        monitor_auto_sleep_manage_event();
        assert(service_monitor_sleep_pending());
        assert(app_prepare_count == 1);
        assert_no_teardown(); /* The app stop barrier still applies at timeout. */
    }
}

static void test_auto_sleep_timeout_clamps(void)
{
    const struct
    {
        uint32_t requested;
        uint32_t effective;
    } cases[] = {
        {0, 10},
        {9, 10},
        {10, 10},
        {3600, 3600},
        {3601, 3600},
        {UINT32_MAX, 3600},
    };

    reset_case();
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
    {
        m_monitor_state.sleep_counter = 123;
        service_monitor_set_auto_sleep_timeout(cases[i].requested);
        assert(m_monitor_state.wakeup_ticks == cases[i].effective * 5u);
        assert(m_monitor_state.sleep_counter == 0);
        assert(!service_monitor_sleep_pending());
        assert_no_teardown();
    }
}

static void test_timeout_change_keeps_auto_sleep_disabled(void)
{
    reset_case();
    g_service_param.sleep.sleep_mode = 1;
    service_monitor_set_auto_sleep_timeout(30);
    monitor_auto_sleep_manage_event();
    assert(m_monitor_state.sleep_counter == 1);

    g_service_param.sleep.sleep_mode = 0;
    service_monitor_set_auto_sleep_timeout(120);
    assert(m_monitor_state.sleep_counter == 0);
    for (uint32_t tick = 0; tick < 120u * 5u + 100u; ++tick)
        monitor_auto_sleep_manage_event();
    assert(m_monitor_state.sleep_counter == 0);
    assert(!service_monitor_sleep_pending());
    assert(app_prepare_count == 0);
    assert_no_teardown();
}

int main(void)
{
    test_disabled_auto_sleep();
    test_auto_sleep_timeout_thresholds();
    test_auto_sleep_timeout_clamps();
    test_timeout_change_keeps_auto_sleep_disabled();
    test_manual_sleep_waits_for_transfer_and_release();
    test_auto_sleep_requires_both_barriers();
    test_sleep_defers_when_wakeup_source_unavailable();
    puts("host_sleep: 7 tests passed");
    return 0;
}

esp_sleep_wakeup_cause_t esp_sleep_get_wakeup_cause(void) { return ESP_SLEEP_WAKEUP_UNDEFINED; }
BaseType_t xTaskCreate(void (*task)(void *), const char *name, unsigned stack,
                       void *arg, unsigned priority, TaskHandle_t *handle)
{ (void)task; (void)name; (void)stack; (void)arg; (void)priority; (void)handle; return pdPASS; }
QueueHandle_t xQueueCreate(unsigned count, unsigned item_size)
{ (void)count; (void)item_size; return (QueueHandle_t)1; }
BaseType_t xQueueSend(QueueHandle_t queue, const void *item, TickType_t wait)
{ (void)queue; (void)item; (void)wait; return pdPASS; }
BaseType_t xQueueSendFromISR(QueueHandle_t queue, const void *item, BaseType_t *woken)
{ (void)queue; (void)item; (void)woken; return pdPASS; }
BaseType_t xQueueReceive(QueueHandle_t queue, void *item, TickType_t wait)
{ (void)queue; (void)item; (void)wait; return pdPASS; }
TimerHandle_t xTimerCreate(const char *name, TickType_t period, BaseType_t auto_reload,
                           void *id, void (*callback)(TimerHandle_t))
{ (void)name; (void)period; (void)auto_reload; (void)id; (void)callback; return (TimerHandle_t)1; }
BaseType_t xTimerStart(TimerHandle_t timer, TickType_t wait)
{ (void)timer; (void)wait; return pdPASS; }
BaseType_t xTimerStop(TimerHandle_t timer, TickType_t wait)
{ (void)timer; (void)wait; ++timer_stop_count; return pdPASS; }
void hal_input_register_cb(int press, void (*cb)(void)) { (void)press; (void)cb; }
void hal_led_breath_stop(void) {}
void hal_led_set_color(uint32_t color) { (void)color; }
void hal_led_breath_start(uint32_t color, int min, int max, int period)
{ (void)color; (void)min; (void)max; (void)period; }
int hal_bat_get_level(void) { return 100; }
void hal_pwr_set_timer_wakeup(uint16_t minutes) { (void)minutes; ++timer_wakeup_count; }
bool hal_pwr_wake_condition_met(void) { return wake_pressed; }
void hal_led_deinit(void) { ++peripheral_deinit_count; }
void hal_bat_deinit(void) { ++peripheral_deinit_count; }
void hal_sd_deinit(void) { ++peripheral_deinit_count; }
void hal_epd_deinit(void) { ++peripheral_deinit_count; }
void hal_input_deinit(void) { ++peripheral_deinit_count; }
void hal_pwr_enter_sleep(void) { ++sleep_count; }
bool hal_pwr_wakeup_prepare(void) { ++wakeup_prepare_count; return wakeup_prepare_ok; }
bool service_ble_gatts_get_connect(void) { return false; }
void service_ble_gatt_server_uninit(void) { ++peripheral_deinit_count; }
bool service_ble_prepare_sleep(void) { ++ble_prepare_count; return ble_file_closed; }
bool service_wifi_direct_busy(void) { return direct_busy; }
void service_wifi_direct_cancel(void) { ++cancel_count; }
void service_wifi_deinit(void) { ++wifi_deinit_count; }

/**
 * NFC HAL：ST25R3916 封装（仅 n16r8 编译）
 */
#include <string.h>

#include "esp_log.h"

#include "sys_log.h"
#include "hal_nfc.h"
#include "st25r3916_nfc.h"

#if (SYS_NFC_ENABLE == 1)

static st25r_handle_t m_nfc = NULL;
static bool m_sleep_armed = false;

static void nfc_log_err(const char *what, esp_err_t err)
{
    sys_loge(HAL_NFC_TAG, "%s: %s", what, esp_err_to_name(err));
}

esp_err_t hal_nfc_init(void)
{
    if(m_nfc != NULL)
    {
        return ESP_OK;
    }

    st25r_config_t cfg = ST25R_CONFIG_DEFAULT();
    cfg.host = HAL_NFC_SPI_HOST;
    cfg.pin_sclk = HAL_NFC_PIN_SCLK;
    cfg.pin_mosi = HAL_NFC_PIN_MOSI;
    cfg.pin_miso = HAL_NFC_PIN_MISO;
    cfg.pin_cs = HAL_NFC_PIN_CS;
    cfg.pin_irq = HAL_NFC_PIN_IRQ;
    cfg.clock_hz = HAL_NFC_SPI_CLOCK_HZ;

    esp_err_t err = st25r_init(&cfg, &m_nfc);
    if(err != ESP_OK)
    {
        nfc_log_err("st25r_init failed", err);
        m_nfc = NULL;
        return err;
    }

    /* 板上 IRQ 没有外部上下拉：给一个内部下拉，避免休眠时悬空。
       deep sleep 期间 RTC 域是否保持该下拉取决于芯片/配置，硬件仍建议补 10k。 */
    (void)gpio_set_pull_mode(HAL_NFC_PIN_IRQ, GPIO_PULLDOWN_ONLY);

    m_sleep_armed = false;
    sys_logi(HAL_NFC_TAG, "NFC ready (host=%d, %d Hz, IRQ=%d)", (int)HAL_NFC_SPI_HOST,
             (int)HAL_NFC_SPI_CLOCK_HZ, (int)HAL_NFC_PIN_IRQ);
    return ESP_OK;
}

void hal_nfc_deinit(void)
{
    hal_nfc_sleep_disarm();
    if(m_nfc != NULL)
    {
        (void)st25r_deinit(m_nfc);
        m_nfc = NULL;
    }
    m_sleep_armed = false;
}

bool hal_nfc_ready(void)
{
    return m_nfc != NULL;
}

esp_err_t hal_nfc_chip_info(uint8_t *ic_type, uint8_t *rev)
{
    if(m_nfc == NULL) return ESP_ERR_INVALID_STATE;
    return st25r_chip_info(m_nfc, ic_type, rev);
}

esp_err_t hal_nfc_reader_begin(void)
{
    if(m_nfc == NULL) return ESP_ERR_INVALID_STATE;
    (void)st25r_picc_end(m_nfc);
    return st25r_reader_begin(m_nfc);
}

esp_err_t hal_nfc_reader_end(void)
{
    if(m_nfc == NULL) return ESP_ERR_INVALID_STATE;
    return st25r_reader_end(m_nfc);
}

esp_err_t hal_nfc_reader_poll(hal_nfc_card_t *card, uint32_t timeout_ms)
{
    if(m_nfc == NULL || card == NULL) return ESP_ERR_INVALID_STATE;

    st25r_card_t raw;
    memset(&raw, 0, sizeof(raw));
    esp_err_t err = st25r_reader_poll(m_nfc, &raw, timeout_ms);
    if(err != ESP_OK)
    {
        return err;
    }

    memset(card, 0, sizeof(*card));
    card->kind = HAL_NFC_KIND_M1_UID;
    card->data_format = HAL_NFC_DATA_NONE;
    card->atqa[0] = (uint8_t)(raw.atqa & 0xFF);
    card->atqa[1] = (uint8_t)(raw.atqa >> 8);
    card->sak = raw.sak;
    card->uid_len = raw.uid_len;
    memcpy(card->uid, raw.uid, raw.uid_len > sizeof(card->uid) ? sizeof(card->uid) : raw.uid_len);
    card->mem = NULL;
    card->mem_len = 0;
    return ESP_OK;
}

esp_err_t hal_nfc_reader_transceive(const uint8_t *tx, uint16_t tx_len,
                                    uint8_t *rx, uint16_t *rx_len, uint32_t timeout_ms)
{
    if(m_nfc == NULL) return ESP_ERR_INVALID_STATE;
    return st25r_reader_transceive(m_nfc, tx, tx_len, rx, rx_len, timeout_ms);
}

esp_err_t hal_nfc_reader_halt(void)
{
    if(m_nfc == NULL) return ESP_ERR_INVALID_STATE;
    return st25r_reader_halt(m_nfc);
}

static bool ntag_type_of(hal_nfc_kind_t kind, st25r_ntag_type_t *type)
{
    switch(kind)
    {
    case HAL_NFC_KIND_NTAG213: *type = ST25R_NTAG_213; return true;
    case HAL_NFC_KIND_NTAG215: *type = ST25R_NTAG_215; return true;
    case HAL_NFC_KIND_NTAG216: *type = ST25R_NTAG_216; return true;
    default: return false;
    }
}

esp_err_t hal_nfc_emu_start(const hal_nfc_card_t *card)
{
    if(m_nfc == NULL || card == NULL) return ESP_ERR_INVALID_STATE;

    (void)st25r_reader_end(m_nfc);
    /* 必须走 ntag_end：picc_end 不清 h->ntag.active，残留会让下一次 ntag_begin 直接
       返回 ESP_ERR_INVALID_STATE（表现为模拟一次之后就再也打不开） */
    (void)st25r_ntag_end(m_nfc);

    st25r_ntag_type_t ntag_type;
    if(card->data_format == HAL_NFC_DATA_NTAG_PAGES && ntag_type_of(card->kind, &ntag_type))
    {
        st25r_ntag_config_t cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.type = ntag_type;
        memcpy(cfg.uid, card->uid, sizeof(cfg.uid));
        cfg.memory = NULL;
        cfg.writable = false;
        cfg.callback = NULL;
        cfg.task_stack_size = HAL_NFC_PICC_TASK_STACK;
        cfg.task_priority = HAL_NFC_PICC_TASK_PRIO;
        cfg.task_core = HAL_NFC_PICC_TASK_CORE;

        esp_err_t err = st25r_ntag_begin(m_nfc, &cfg);
        if(err != ESP_OK) return err;

        /* 库的 begin 会重建页 0..3 与配置页；这里只恢复用户页 4..last_user。 */
        uint8_t last_user;
        switch(card->kind)
        {
        case HAL_NFC_KIND_NTAG213: last_user = 39;  break;
        case HAL_NFC_KIND_NTAG215: last_user = 129; break;
        case HAL_NFC_KIND_NTAG216: last_user = 225; break;
        default: last_user = 0; break;
        }
        if(card->mem != NULL && last_user >= 4)
        {
            for(uint16_t page = 4; page <= last_user; page = (uint16_t)(page + 4))
            {
                uint16_t remain = (uint16_t)(last_user - page + 1);
                uint8_t chunk = (uint8_t)(remain >= 4 ? 4 : remain);
                err = st25r_ntag_write_memory(m_nfc, (uint8_t)page,
                                              card->mem + page * 4, chunk);
                if(err != ESP_OK)
                {
                    (void)st25r_picc_end(m_nfc);
                    return err;
                }
            }
        }
        return ESP_OK;
    }

    /* M1 UID / M1 完整块镜像：暂按 ID 卡模拟（完整扇区等 Crypto-1）。 */
    st25r_picc_config_t picc;
    memset(&picc, 0, sizeof(picc));
    picc.card.tech = ST25R_TECH_NFC_A;
    picc.card.atqa = (uint16_t)(card->atqa[0] | ((uint16_t)card->atqa[1] << 8));
    picc.card.sak = card->sak;
    picc.card.uid_len = card->uid_len;
    memcpy(picc.card.uid, card->uid, card->uid_len > sizeof(picc.card.uid) ? sizeof(picc.card.uid) : card->uid_len);
    picc.callback = NULL;
    picc.task_stack_size = HAL_NFC_PICC_TASK_STACK;
    picc.task_priority = HAL_NFC_PICC_TASK_PRIO;
    picc.task_core = HAL_NFC_PICC_TASK_CORE;
    return st25r_picc_begin(m_nfc, &picc);
}

esp_err_t hal_nfc_emu_stop(void)
{
    if(m_nfc == NULL) return ESP_ERR_INVALID_STATE;
    /* 同 emu_start：用 ntag_end 顺带清掉 active，避免残留状态卡死后续模拟 */
    return st25r_ntag_end(m_nfc);
}

esp_err_t hal_nfc_emu_set_card(const hal_nfc_card_t *card)
{
    if(m_nfc == NULL || card == NULL) return ESP_ERR_INVALID_STATE;
    /* 换型号/换镜像统一走 stop+start，避免库内 PT 状态残留。 */
    return hal_nfc_emu_start(card);
}

bool hal_nfc_emu_running(void)
{
    return (m_nfc != NULL) && st25r_picc_is_running(m_nfc);
}

bool hal_nfc_irq_active(void)
{
    return gpio_get_level(HAL_NFC_PIN_IRQ) != 0;
}

esp_err_t hal_nfc_sleep_arm(void)
{
    if(m_nfc == NULL) return ESP_ERR_INVALID_STATE;
    esp_err_t err = st25r_wakeup_enable(m_nfc, ST25R_WAKE_DEEP_SLEEP);
    m_sleep_armed = (err == ESP_OK);
    return err;
}

void hal_nfc_sleep_disarm(void)
{
    if(m_nfc != NULL && m_sleep_armed)
    {
        (void)st25r_wakeup_disable(m_nfc, ST25R_WAKE_DEEP_SLEEP);
    }
    m_sleep_armed = false;
}

bool hal_nfc_sleep_armed(void)
{
    return m_sleep_armed;
}

#endif /* SYS_NFC_ENABLE */

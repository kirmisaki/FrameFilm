#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/unistd.h>

#include "esp_heap_caps.h"

#include "sys_log.h"
#include "app_wallet_store.h"

#define WALLET_TAG          "wallet_store"
#define WALLET_HDR_BYTES    32
#define WALLET_REC_BYTES    60

static wallet_card_t m_cards[WALLET_MAX_CARDS];
static int  m_count = 0;
static int  m_current = 0;
static int  m_next_id = 1;
static bool m_loaded = false;

/**************************** CRC32 ****************************/
static uint32_t crc32_begin(void)
{
    return 0xFFFFFFFFu;
}

static uint32_t crc32_feed(uint32_t c, const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    while(len--)
    {
        c ^= *p++;
        for(int i = 0; i < 8; i++)
        {
            c = (c >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(c & 1u)));
        }
    }
    return c;
}

static uint32_t crc32_end(uint32_t c)
{
    return c ^ 0xFFFFFFFFu;
}

/**************************** helpers ****************************/
static void *wallet_alloc(size_t size)
{
    void *p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    if(p == NULL) p = malloc(size);
    return p;
}

static void card_mem_free(wallet_card_t *c)
{
    if(c->mem != NULL)
    {
        heap_caps_free(c->mem);
        c->mem = NULL;
    }
    c->mem_len = 0;
}

static void store_reset(void)
{
    for(int i = 0; i < WALLET_MAX_CARDS; i++)
    {
        card_mem_free(&m_cards[i]);
        memset(&m_cards[i], 0, sizeof(m_cards[i]));
    }
    m_count = 0;
    m_current = 0;
    m_next_id = 1;
    m_loaded = true;
}

static bool kind_valid(uint8_t kind, uint8_t format)
{
    switch(kind)
    {
    case HAL_NFC_KIND_M1_UID:  return format == HAL_NFC_DATA_NONE;
    case HAL_NFC_KIND_NTAG213:
    case HAL_NFC_KIND_NTAG215:
    case HAL_NFC_KIND_NTAG216: return format == HAL_NFC_DATA_NTAG_PAGES;
    case HAL_NFC_KIND_M1_1K:
    case HAL_NFC_KIND_M1_2K:
    case HAL_NFC_KIND_M1_4K:   return format == HAL_NFC_DATA_M1_BLOCKS;
    default:                   return false;
    }
}

static uint16_t kind_mem_len(uint8_t kind)
{
    switch(kind)
    {
    case HAL_NFC_KIND_NTAG213: return 180;
    case HAL_NFC_KIND_NTAG215: return 540;
    case HAL_NFC_KIND_NTAG216: return 924;
    case HAL_NFC_KIND_M1_1K:   return 1024;
    case HAL_NFC_KIND_M1_2K:   return 2048;
    case HAL_NFC_KIND_M1_4K:   return 4096;
    default:                   return 0;
    }
}

static void card_default_name(wallet_card_t *c)
{
    uint8_t a = c->uid_len > 2 ? c->uid[c->uid_len - 2] : 0;
    uint8_t b = c->uid_len > 1 ? c->uid[c->uid_len - 1] : 0;
    const char *prefix = "CARD";

    switch(c->kind)
    {
    case HAL_NFC_KIND_M1_UID:  prefix = "M1"; break;
    case HAL_NFC_KIND_NTAG213: prefix = "NTAG213"; break;
    case HAL_NFC_KIND_NTAG215: prefix = "NTAG215"; break;
    case HAL_NFC_KIND_NTAG216: prefix = "NTAG216"; break;
    case HAL_NFC_KIND_M1_1K:   prefix = "M1-1K"; break;
    case HAL_NFC_KIND_M1_2K:   prefix = "M1-2K"; break;
    case HAL_NFC_KIND_M1_4K:   prefix = "M1-4K"; break;
    default: break;
    }
    snprintf(c->name, sizeof(c->name), "%s-%02X%02X", prefix, a, b);
    c->name_len = (uint8_t)strlen(c->name);
}

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static uint16_t get16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void rec_pack(uint8_t *b, const wallet_card_t *c)
{
    memset(b, 0, WALLET_REC_BYTES);
    b[0] = c->id;
    b[1] = c->kind;
    b[2] = c->uid_len;
    b[3] = c->sak;
    b[4] = c->atqa[0];
    b[5] = c->atqa[1];
    memcpy(&b[6], c->uid, sizeof(c->uid));
    b[16] = c->name_len > 23 ? 23 : c->name_len;
    memcpy(&b[17], c->name, 23);
    b[40] = c->data_format;
    b[41] = c->data_ver;
    b[42] = c->flags;
    b[43] = 0;
    put16(&b[44], c->mem_len);
    put16(&b[46], c->cover_len);
    put32(&b[48], c->cover_crc32);
}

static int rec_unpack(const uint8_t *b, wallet_card_t *c)
{
    memset(c, 0, sizeof(*c));
    c->id = b[0];
    c->kind = b[1];
    c->uid_len = b[2];
    c->sak = b[3];
    c->atqa[0] = b[4];
    c->atqa[1] = b[5];
    memcpy(c->uid, &b[6], sizeof(c->uid));
    c->name_len = b[16] > 23 ? 23 : b[16];
    memcpy(c->name, &b[17], 23);
    c->name[23] = '\0';
    c->data_format = b[40];
    c->data_ver = b[41];
    c->flags = b[42];
    c->mem_len = get16(&b[44]);
    c->cover_len = get16(&b[46]);
    c->cover_crc32 = get32(&b[48]);
    c->mem = NULL;

    if(c->uid_len > sizeof(c->uid)) return -1;
    if(c->mem_len > HAL_NFC_MAX_CARD_BYTES) return -1;
    if(!kind_valid(c->kind, c->data_format)) return -1;
    if(c->data_format != HAL_NFC_DATA_NONE && c->mem_len != kind_mem_len(c->kind)) return -1;
    if(c->name[0] == '\0' || c->name_len == 0) card_default_name(c);
    return 0;
}

static int cover_path(uint8_t id, char *out, size_t out_len)
{
    int n = snprintf(out, out_len, "%s/cover_%u.bin", WALLET_DIR, (unsigned)id);
    return (n > 0 && (size_t)n < out_len) ? 0 : -1;
}

/**************************** public ****************************/
bool wallet_store_loaded(void)
{
    return m_loaded;
}

int wallet_store_count(void)
{
    return m_count;
}

int wallet_store_current(void)
{
    return m_current;
}

wallet_card_t *wallet_store_get(int idx)
{
    if(idx < 0 || idx >= m_count) return NULL;
    return &m_cards[idx];
}

int wallet_store_find(uint8_t kind, const uint8_t *uid, uint8_t uid_len)
{
    if(uid == NULL || uid_len == 0) return -1;
    for(int i = 0; i < m_count; i++)
    {
        if(m_cards[i].kind == kind && m_cards[i].uid_len == uid_len &&
           memcmp(m_cards[i].uid, uid, uid_len) == 0)
        {
            return i;
        }
    }
    return -1;
}

int wallet_store_load(void)
{
    store_reset();
    (void)mkdir(WALLET_DIR, 0777);

    FILE *f = fopen(WALLET_FILE, "rb");
    if(f == NULL)
    {
        return 0;   /* 空卡包 */
    }

    uint8_t hdr[WALLET_HDR_BYTES];
    if(fread(hdr, 1, sizeof(hdr), f) != sizeof(hdr) ||
       memcmp(hdr, "NFW1", 4) != 0 || hdr[4] != 1)
    {
        fclose(f);
        sys_logw(WALLET_TAG, "wallet.bin invalid header");
        return -1;
    }

    uint8_t count = hdr[5];
    if(count > WALLET_MAX_CARDS)
    {
        fclose(f);
        return -1;
    }

    uint32_t crc = crc32_begin();
    int ok = 1;
    for(uint8_t i = 0; i < count; i++)
    {
        uint8_t rbuf[WALLET_REC_BYTES];
        wallet_card_t c;

        if(fread(rbuf, 1, sizeof(rbuf), f) != sizeof(rbuf) ||
           rec_unpack(rbuf, &c) != 0)
        {
            ok = 0;
            break;
        }
        crc = crc32_feed(crc, rbuf, sizeof(rbuf));

        if(c.mem_len > 0)
        {
            c.mem = (uint8_t *)wallet_alloc(c.mem_len);
            if(c.mem == NULL || fread(c.mem, 1, c.mem_len, f) != c.mem_len)
            {
                if(c.mem) heap_caps_free(c.mem);
                ok = 0;
                break;
            }
            crc = crc32_feed(crc, c.mem, c.mem_len);
        }
        m_cards[m_count++] = c;
    }

    if(ok && crc32_end(crc) != get32(&hdr[8]))
    {
        ok = 0;
    }
    fclose(f);

    if(!ok)
    {
        store_reset();
        sys_logw(WALLET_TAG, "wallet.bin crc/record invalid");
        return -1;
    }

    m_current = hdr[6] < m_count ? hdr[6] : (m_count ? 0 : 0);
    m_next_id = hdr[7] ? hdr[7] : 1;
    sys_logi(WALLET_TAG, "loaded %d cards, current=%d", m_count, m_current);
    return 0;
}

void wallet_store_unload(void)
{
    for(int i = 0; i < WALLET_MAX_CARDS; i++)
    {
        card_mem_free(&m_cards[i]);
    }
    m_count = 0;
    m_current = 0;
    m_loaded = false;
}

int wallet_store_save(void)
{
    if(!m_loaded) return -1;
    (void)mkdir(WALLET_DIR, 0777);

    uint32_t crc = crc32_begin();
    for(int i = 0; i < m_count; i++)
    {
        uint8_t rbuf[WALLET_REC_BYTES];
        rec_pack(rbuf, &m_cards[i]);
        crc = crc32_feed(crc, rbuf, sizeof(rbuf));
        if(m_cards[i].mem_len > 0 && m_cards[i].mem != NULL)
        {
            crc = crc32_feed(crc, m_cards[i].mem, m_cards[i].mem_len);
        }
    }

    uint8_t hdr[WALLET_HDR_BYTES];
    memset(hdr, 0, sizeof(hdr));
    memcpy(hdr, "NFW1", 4);
    hdr[4] = 1;
    hdr[5] = (uint8_t)m_count;
    hdr[6] = (uint8_t)(m_count ? m_current : 0);
    hdr[7] = (uint8_t)m_next_id;
    put32(&hdr[8], crc32_end(crc));

    FILE *f = fopen(WALLET_FILE ".tmp", "wb");
    if(f == NULL)
    {
        sys_loge(WALLET_TAG, "open tmp failed");
        return -1;
    }
    int ok = fwrite(hdr, 1, sizeof(hdr), f) == sizeof(hdr);
    for(int i = 0; ok && i < m_count; i++)
    {
        uint8_t rbuf[WALLET_REC_BYTES];
        rec_pack(rbuf, &m_cards[i]);
        ok = fwrite(rbuf, 1, sizeof(rbuf), f) == sizeof(rbuf);
        if(ok && m_cards[i].mem_len > 0 && m_cards[i].mem != NULL)
        {
            ok = fwrite(m_cards[i].mem, 1, m_cards[i].mem_len, f) == m_cards[i].mem_len;
        }
    }
    fclose(f);

    if(!ok)
    {
        remove(WALLET_FILE ".tmp");
        return -1;
    }

    (void)remove(WALLET_FILE ".bak");
    (void)rename(WALLET_FILE, WALLET_FILE ".bak");
    if(rename(WALLET_FILE ".tmp", WALLET_FILE) != 0)
    {
        sys_loge(WALLET_TAG, "rename tmp failed");
        return -1;
    }
    return 0;
}

int wallet_store_upsert(const wallet_card_t *card, int *out_idx)
{
    if(card == NULL || !m_loaded) return -1;
    if(card->uid_len > sizeof(card->uid)) return -1;
    if(card->mem_len > HAL_NFC_MAX_CARD_BYTES) return -1;
    if(!kind_valid(card->kind, card->data_format)) return -1;
    if(card->data_format != HAL_NFC_DATA_NONE &&
       card->mem_len != kind_mem_len(card->kind)) return -1;

    int idx = wallet_store_find(card->kind, card->uid, card->uid_len);
    if(idx < 0)
    {
        if(m_count >= WALLET_MAX_CARDS) return -2;
        idx = m_count++;
        memset(&m_cards[idx], 0, sizeof(m_cards[idx]));
        m_cards[idx].id = (uint8_t)m_next_id;
        m_next_id = (m_next_id >= 255) ? 1 : (m_next_id + 1);
    }

    wallet_card_t *dst = &m_cards[idx];
    uint8_t keep_id = dst->id;
    uint8_t keep_flags = dst->flags;
    uint16_t keep_cover_len = dst->cover_len;
    uint32_t keep_cover_crc = dst->cover_crc32;
    uint8_t keep_name_len = dst->name_len;
    char keep_name[24];
    memcpy(keep_name, dst->name, sizeof(keep_name));

    card_mem_free(dst);

    dst->kind = card->kind;
    dst->data_format = card->data_format;
    dst->data_ver = card->data_ver ? card->data_ver : WALLET_DATA_VER;
    dst->uid_len = card->uid_len;
    dst->sak = card->sak;
    dst->atqa[0] = card->atqa[0];
    dst->atqa[1] = card->atqa[1];
    memcpy(dst->uid, card->uid, sizeof(dst->uid));
    dst->flags = keep_flags | (card->flags & WALLET_FLAG_COVER);
    dst->cover_len = keep_cover_len;
    dst->cover_crc32 = keep_cover_crc;
    dst->mem_len = card->mem_len;

    if(card->name_len > 0 && card->name[0] != '\0')
    {
        dst->name_len = card->name_len > 23 ? 23 : card->name_len;
        memcpy(dst->name, card->name, 23);
        dst->name[dst->name_len] = '\0';
    }
    else if(keep_name_len > 0)
    {
        dst->name_len = keep_name_len;
        memcpy(dst->name, keep_name, sizeof(dst->name));
    }
    else
    {
        card_default_name(dst);
    }

    if(dst->mem_len > 0 && card->mem != NULL)
    {
        dst->mem = (uint8_t *)wallet_alloc(dst->mem_len);
        if(dst->mem == NULL)
        {
            dst->mem_len = 0;
            return -3;
        }
        memcpy(dst->mem, card->mem, dst->mem_len);
    }

    if(out_idx) *out_idx = idx;
    return 0;
}

int wallet_store_select(int idx)
{
    if(idx < 0 || idx >= m_count) return -1;
    m_current = idx;
    return 0;
}

int wallet_store_rename(int idx, const char *name, int len)
{
    if(idx < 0 || idx >= m_count || name == NULL || len <= 0 || len > 23) return -1;
    memcpy(m_cards[idx].name, name, len);
    m_cards[idx].name[len] = '\0';
    m_cards[idx].name_len = (uint8_t)len;
    return 0;
}

int wallet_store_delete(int idx)
{
    if(idx < 0 || idx >= m_count) return -1;
    (void)wallet_store_cover_delete(idx);
    card_mem_free(&m_cards[idx]);
    for(int i = idx; i < m_count - 1; i++)
    {
        m_cards[i] = m_cards[i + 1];
    }
    m_count--;
    memset(&m_cards[m_count], 0, sizeof(m_cards[m_count]));
    if(m_count == 0) m_current = 0;
    else if(m_current >= m_count) m_current = m_count - 1;
    return 0;
}

int wallet_store_cover_load(int idx, uint8_t **payload, uint32_t *crc)
{
    if(idx < 0 || idx >= m_count || payload == NULL) return -1;
    *payload = NULL;

    char path[64];
    if(cover_path(m_cards[idx].id, path, sizeof(path)) != 0) return -1;

    FILE *f = fopen(path, "rb");
    if(f == NULL) return -1;

    uint8_t hdr[WALLET_COVER_HDR];
    if(fread(hdr, 1, sizeof(hdr), f) != sizeof(hdr) ||
       memcmp(hdr, "FFUI", 4) != 0 || hdr[4] != 1 || hdr[5] != 1 ||
       get16(&hdr[6]) != WALLET_COVER_W || get16(&hdr[8]) != WALLET_COVER_H)
    {
        fclose(f);
        return -1;
    }

    uint8_t *buf = (uint8_t *)wallet_alloc(WALLET_COVER_PAYLOAD);
    if(buf == NULL)
    {
        fclose(f);
        return -1;
    }
    if(fread(buf, 1, WALLET_COVER_PAYLOAD, f) != WALLET_COVER_PAYLOAD)
    {
        heap_caps_free(buf);
        fclose(f);
        return -1;
    }
    fclose(f);

    uint32_t c = crc32_end(crc32_feed(crc32_begin(), buf, WALLET_COVER_PAYLOAD));
    if(m_cards[idx].flags & WALLET_FLAG_COVER)
    {
        if(m_cards[idx].cover_crc32 != c)
        {
            heap_caps_free(buf);
            return -1;
        }
    }
    if(crc) *crc = c;
    *payload = buf;
    return 0;
}

int wallet_store_cover_save(int idx, const uint8_t *payload, uint32_t crc)
{
    if(idx < 0 || idx >= m_count || payload == NULL) return -1;

    char path[64];
    if(cover_path(m_cards[idx].id, path, sizeof(path)) != 0) return -1;

    char tmp[72];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);

    FILE *f = fopen(tmp, "wb");
    if(f == NULL) return -1;

    uint8_t hdr[WALLET_COVER_HDR];
    memset(hdr, 0, sizeof(hdr));
    memcpy(hdr, "FFUI", 4);
    hdr[4] = 1;
    hdr[5] = 1;
    put16(&hdr[6], WALLET_COVER_W);
    put16(&hdr[8], WALLET_COVER_H);

    int ok = fwrite(hdr, 1, sizeof(hdr), f) == sizeof(hdr) &&
             fwrite(payload, 1, WALLET_COVER_PAYLOAD, f) == WALLET_COVER_PAYLOAD;
    fclose(f);

    if(!ok)
    {
        remove(tmp);
        return -1;
    }
    (void)remove(path);
    if(rename(tmp, path) != 0)
    {
        remove(tmp);
        return -1;
    }

    m_cards[idx].flags |= WALLET_FLAG_COVER;
    m_cards[idx].cover_len = WALLET_COVER_FILE_BYTES;
    m_cards[idx].cover_crc32 = crc;
    return 0;
}

int wallet_store_cover_delete(int idx)
{
    if(idx < 0 || idx >= m_count) return -1;
    char path[64];
    if(cover_path(m_cards[idx].id, path, sizeof(path)) == 0)
    {
        (void)remove(path);
    }
    m_cards[idx].flags &= (uint8_t)~WALLET_FLAG_COVER;
    m_cards[idx].cover_len = 0;
    m_cards[idx].cover_crc32 = 0;
    return 0;
}

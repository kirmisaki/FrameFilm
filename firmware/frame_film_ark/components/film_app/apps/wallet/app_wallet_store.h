#ifndef __APP_WALLET_STORE_H__
#define __APP_WALLET_STORE_H__

#include <stdbool.h>
#include <stdint.h>

#include "hal_nfc.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WALLET_DIR                  "/sdcard/app/nfc"
#define WALLET_FILE                 WALLET_DIR "/wallet.bin"
#define WALLET_MAX_CARDS            HAL_NFC_MAX_CARDS

#define WALLET_COVER_W              440
#define WALLET_COVER_H              200
#define WALLET_COVER_PAYLOAD        (WALLET_COVER_W * WALLET_COVER_H / 8)   /* 11000 */
#define WALLET_COVER_HDR            16
#define WALLET_COVER_FILE_BYTES     (WALLET_COVER_HDR + WALLET_COVER_PAYLOAD)

#define WALLET_FLAG_COVER           0x01
#define WALLET_DATA_VER             1

typedef struct {
    uint8_t  id;
    uint8_t  kind;              /* hal_nfc_kind_t */
    uint8_t  data_format;       /* hal_nfc_data_format_t */
    uint8_t  data_ver;
    uint8_t  flags;
    uint8_t  uid_len;
    uint8_t  sak;
    uint8_t  atqa[2];
    uint8_t  uid[10];
    uint8_t  name_len;
    char     name[24];          /* NUL terminated for UI */
    uint16_t mem_len;
    uint16_t cover_len;
    uint32_t cover_crc32;
    uint8_t *mem;               /* PSRAM */
} wallet_card_t;

int  wallet_store_load(void);
void wallet_store_unload(void);
bool wallet_store_loaded(void);

int  wallet_store_count(void);
int  wallet_store_current(void);
wallet_card_t *wallet_store_get(int idx);
int  wallet_store_find(uint8_t kind, const uint8_t *uid, uint8_t uid_len);

int  wallet_store_save(void);
int  wallet_store_upsert(const wallet_card_t *card, int *out_idx);
int  wallet_store_select(int idx);
int  wallet_store_rename(int idx, const char *name, int len);
int  wallet_store_delete(int idx);

int  wallet_store_cover_load(int idx, uint8_t **payload, uint32_t *crc);
int  wallet_store_cover_save(int idx, const uint8_t *payload, uint32_t crc);
int  wallet_store_cover_delete(int idx);

#ifdef __cplusplus
}
#endif

#endif /* __APP_WALLET_STORE_H__ */

#ifndef MY_PROJECT_2X_STORAGE_SYNC_H
#define MY_PROJECT_2X_STORAGE_SYNC_H

#include <stdbool.h>
#include <stdint.h>

#include "errcode.h"
#include "../shared_protocol/shared_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NV_ID_BS2X_TAG_ID 0x3001u

typedef errcode_t (*storage_sync_adv_refresh_cb_t)(const shared_proto_adv_field_t *field);

typedef struct {
    storage_sync_adv_refresh_cb_t refresh_adv_cb;
} storage_sync_adapter_t;

errcode_t storage_sync_init(uint16_t tag_id,
                            uint16_t qty,
                            uint8_t battery,
                            const storage_sync_adapter_t *adapter);

errcode_t storage_sync_set_qty(uint16_t qty);
errcode_t storage_sync_set_find_status(bool active);
errcode_t storage_sync_set_tag_id(uint16_t tag_id);
errcode_t storage_sync_clear_tag_id(void);
errcode_t storage_sync_publish(void);

uint16_t storage_sync_get_qty(void);
uint16_t storage_sync_get_tag_id(void);
void storage_sync_get_field(shared_proto_adv_field_t *field);

#ifdef __cplusplus
}
#endif

#endif /* MY_PROJECT_2X_STORAGE_SYNC_H */

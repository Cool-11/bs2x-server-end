#include "storage_sync.h"

#include "common_def.h"
#include "soc_osal.h"

#define STORAGE_SYNC_LOG "[BS2x_SYNC]"
#define STORAGE_SYNC_STATUS_NORMAL 0x00u
#define STORAGE_SYNC_STATUS_FINDING 0x01u

typedef struct {
    bool inited;
    storage_sync_adapter_t adapter;
    shared_proto_adv_field_t field;
} storage_sync_ctx_t;

static storage_sync_ctx_t g_sync = {0};

errcode_t storage_sync_init(uint16_t tag_id,
                            uint16_t qty,
                            uint8_t battery,
                            const storage_sync_adapter_t *adapter)
{
    osal_printk("%s Entering storage_sync_init\r\n", STORAGE_SYNC_LOG);

    if (adapter == NULL || adapter->refresh_adv_cb == NULL) {
        osal_printk("%s adapter invalid\r\n", STORAGE_SYNC_LOG);
        return ERRCODE_INVALID_PARAM;
    }

    g_sync.adapter = *adapter;
    g_sync.field.magic = SHARED_PROTO_MAGIC;
    g_sync.field.tag_id = tag_id;
    g_sync.field.qty = qty;
    g_sync.field.status = STORAGE_SYNC_STATUS_NORMAL;
    g_sync.field.battery = battery;
    g_sync.field.seq = 0;
    g_sync.inited = true;

    osal_printk("%s init ok tag:%u qty:%u battery:%u\r\n",
                STORAGE_SYNC_LOG,
                g_sync.field.tag_id,
                g_sync.field.qty,
                g_sync.field.battery);
    return ERRCODE_SUCC;
}

errcode_t storage_sync_set_qty(uint16_t qty)
{
    if (!g_sync.inited) {
        return ERRCODE_FAIL;
    }

    g_sync.field.qty = qty;
    g_sync.field.seq++;

    osal_printk("%s Qty updated to %u, refreshing Adv Payload (seq=%u)\r\n",
                STORAGE_SYNC_LOG,
                g_sync.field.qty,
                g_sync.field.seq);
    return ERRCODE_SUCC;
}

errcode_t storage_sync_set_find_status(bool active)
{
    if (!g_sync.inited) {
        return ERRCODE_FAIL;
    }

    g_sync.field.status = active ? STORAGE_SYNC_STATUS_FINDING : STORAGE_SYNC_STATUS_NORMAL;
    osal_printk("%s status changed to 0x%02x\r\n", STORAGE_SYNC_LOG, g_sync.field.status);
    return ERRCODE_SUCC;
}

errcode_t storage_sync_publish(void)
{
    if (!g_sync.inited) {
        return ERRCODE_FAIL;
    }

    if (!shared_proto_adv_field_is_valid(&g_sync.field)) {
        osal_printk("%s field invalid\r\n", STORAGE_SYNC_LOG);
        return ERRCODE_INVALID_PARAM;
    }

    errcode_t ret = g_sync.adapter.refresh_adv_cb(&g_sync.field);
    if (ret != ERRCODE_SUCC) {
        osal_printk("%s publish failed ret=0x%x\r\n", STORAGE_SYNC_LOG, ret);
        return ret;
    }

    osal_printk("%s publish ok qty=%u status=0x%02x seq=%u\r\n",
                STORAGE_SYNC_LOG,
                g_sync.field.qty,
                g_sync.field.status,
                g_sync.field.seq);
    return ERRCODE_SUCC;
}

uint16_t storage_sync_get_qty(void)
{
    return g_sync.field.qty;
}

void storage_sync_get_field(shared_proto_adv_field_t *field)
{
    if (field == NULL) {
        return;
    }

    *field = g_sync.field;
}

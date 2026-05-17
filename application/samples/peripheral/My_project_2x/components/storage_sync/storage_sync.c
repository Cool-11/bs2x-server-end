#include "storage_sync.h"

#include "common_def.h"
#include "nv.h"
#include "soc_osal.h"

#define STORAGE_SYNC_LOG "[BS2x_SYNC]"

typedef struct {
    bool inited;
    storage_sync_adapter_t adapter;
    shared_proto_adv_field_t field;
} storage_sync_ctx_t;

static storage_sync_ctx_t g_sync = {0};

static errcode_t storage_sync_load_tag_id_nv(uint16_t *tag_id)
{
    uint16_t read_len = 0;
    errcode_t ret = uapi_nv_read(NV_ID_BS2X_TAG_ID, sizeof(uint16_t), &read_len, (uint8_t *)tag_id);
    if (ret != ERRCODE_SUCC || read_len != sizeof(uint16_t)) {
        osal_printk("%s[BP] NV read tag_id FAIL ret:0x%x len:%u\r\n", STORAGE_SYNC_LOG, ret, read_len);
        return ERRCODE_FAIL;
    }
    osal_printk("%s[BP] NV read tag_id OK val:%u\r\n", STORAGE_SYNC_LOG, *tag_id);
    return ERRCODE_SUCC;
}

static errcode_t storage_sync_save_tag_id_nv(uint16_t tag_id)
{
    errcode_t ret = uapi_nv_write(NV_ID_BS2X_TAG_ID, (const uint8_t *)&tag_id, sizeof(uint16_t));
    if (ret != ERRCODE_SUCC) {
        osal_printk("%s[BP] NV write tag_id FAIL ret:0x%x val:%u\r\n", STORAGE_SYNC_LOG, ret, tag_id);
        return ret;
    }
    osal_printk("%s[BP] NV write tag_id OK val:%u\r\n", STORAGE_SYNC_LOG, tag_id);
    return ERRCODE_SUCC;
}

errcode_t storage_sync_init(uint16_t tag_id,
                            uint16_t qty,
                            uint8_t battery,
                            const storage_sync_adapter_t *adapter)
{
    osal_printk("%s[BP] init enter tag_id:%u qty:%u battery:%u\r\n", STORAGE_SYNC_LOG, tag_id, qty, battery);

    if (adapter == NULL || adapter->refresh_adv_cb == NULL) {
        osal_printk("%s[BP] init FAIL adapter invalid\r\n", STORAGE_SYNC_LOG);
        return ERRCODE_INVALID_PARAM;
    }

    g_sync.adapter = *adapter;

    uint16_t nv_tag_id = 0;
    if (storage_sync_load_tag_id_nv(&nv_tag_id) == ERRCODE_SUCC) {
        g_sync.field.tag_id = nv_tag_id;
        osal_printk("%s[BP] restored tag_id=%u from NV\r\n", STORAGE_SYNC_LOG, nv_tag_id);
    } else {
        g_sync.field.tag_id = tag_id;
        osal_printk("%s[BP] use default tag_id=%u (NV empty)\r\n", STORAGE_SYNC_LOG, tag_id);
    }

    g_sync.field.magic = SHARED_PROTO_MAGIC;
    g_sync.field.qty = qty;
    g_sync.field.status = SHARED_PROTO_STATUS_NORMAL;
    g_sync.field.battery = battery;
    g_sync.field.seq = 0;
    g_sync.inited = true;

    osal_printk("%s[BP] init OK tag:%u qty:%u battery:%u\r\n",
                STORAGE_SYNC_LOG,
                g_sync.field.tag_id,
                g_sync.field.qty,
                g_sync.field.battery);
    return ERRCODE_SUCC;
}

errcode_t storage_sync_set_qty(uint16_t qty)
{
    if (!g_sync.inited) {
        osal_printk("%s[BP] set_qty FAIL not inited\r\n", STORAGE_SYNC_LOG);
        return ERRCODE_FAIL;
    }

    g_sync.field.qty = qty;
    g_sync.field.seq++;

    if (qty == 0) {
        g_sync.field.status = SHARED_PROTO_STATUS_OUTSTOCK;
        osal_printk("%s[BP] set_qty=0 OUTSTOCK status=0x%02x seq=%u\r\n",
                    STORAGE_SYNC_LOG, g_sync.field.status, g_sync.field.seq);
    } else {
        if (g_sync.field.status == SHARED_PROTO_STATUS_OUTSTOCK) {
            g_sync.field.status = SHARED_PROTO_STATUS_NORMAL;
        }
        osal_printk("%s[BP] set_qty=%u status=0x%02x seq=%u\r\n",
                    STORAGE_SYNC_LOG, qty, g_sync.field.status, g_sync.field.seq);
    }
    return ERRCODE_SUCC;
}

errcode_t storage_sync_set_find_status(bool active)
{
    if (!g_sync.inited) {
        osal_printk("%s[BP] set_find_status FAIL not inited\r\n", STORAGE_SYNC_LOG);
        return ERRCODE_FAIL;
    }

    if (active) {
        g_sync.field.status = SHARED_PROTO_STATUS_FINDING;
    } else {
        g_sync.field.status = (g_sync.field.qty == 0) ? SHARED_PROTO_STATUS_OUTSTOCK : SHARED_PROTO_STATUS_NORMAL;
    }
    g_sync.field.seq++;
    osal_printk("%s[BP] find_status=%s restore_to=0x%02x seq=%u\r\n",
                STORAGE_SYNC_LOG, active ? "FINDING" : "RESTORE", g_sync.field.status, g_sync.field.seq);
    return ERRCODE_SUCC;
}

errcode_t storage_sync_set_tag_id(uint16_t tag_id)
{
    if (!g_sync.inited) {
        osal_printk("%s[BP] set_tag_id FAIL not inited\r\n", STORAGE_SYNC_LOG);
        return ERRCODE_FAIL;
    }

    /* 重复绑定相同 tag_id，跳过 NV 写入延长寿命 */
    if (g_sync.field.tag_id == tag_id) {
        osal_printk("%s[BP] set_tag_id=%u same, skip NV write\r\n", STORAGE_SYNC_LOG, tag_id);
        return ERRCODE_SUCC;
    }

    /* 已绑定其他 tag_id，必须先解绑才能重新绑定 */
    if (g_sync.field.tag_id != 0) {
        osal_printk("%s[BP] set_tag_id FAIL already bound to %u, unbind first\r\n",
                    STORAGE_SYNC_LOG, g_sync.field.tag_id);
        return ERRCODE_FAIL;
    }

    errcode_t ret = storage_sync_save_tag_id_nv(tag_id);
    if (ret != ERRCODE_SUCC) {
        return ret;
    }

    g_sync.field.tag_id = tag_id;
    g_sync.field.seq++;
    osal_printk("%s[BP] set_tag_id=%u seq=%u\r\n", STORAGE_SYNC_LOG, g_sync.field.tag_id, g_sync.field.seq);
    return ERRCODE_SUCC;
}

errcode_t storage_sync_clear_tag_id(void)
{
    if (!g_sync.inited) {
        osal_printk("%s[BP] clear_tag_id FAIL not inited\r\n", STORAGE_SYNC_LOG);
        return ERRCODE_FAIL;
    }

    uint16_t old_tag_id = g_sync.field.tag_id;

    errcode_t ret = storage_sync_save_tag_id_nv(0);
    if (ret != ERRCODE_SUCC) {
        osal_printk("%s[BP] clear_tag_id NV FAIL ret:0x%x\r\n", STORAGE_SYNC_LOG, ret);
        return ret;
    }

    g_sync.field.tag_id = 0;
    g_sync.field.qty = 0;
    g_sync.field.status = SHARED_PROTO_STATUS_NORMAL;
    g_sync.field.seq++;
    osal_printk("%s[BP] clear_tag_id OK old=%u new=0 qty=0 seq=%u\r\n",
                STORAGE_SYNC_LOG, old_tag_id, g_sync.field.seq);
    return ERRCODE_SUCC;
}

errcode_t storage_sync_publish(void)
{
    if (!g_sync.inited) {
        osal_printk("%s[BP] publish FAIL not inited\r\n", STORAGE_SYNC_LOG);
        return ERRCODE_FAIL;
    }

    if (!shared_proto_adv_field_is_valid(&g_sync.field)) {
        osal_printk("%s[BP] publish FAIL field invalid magic:0x%08X\r\n",
                    STORAGE_SYNC_LOG, g_sync.field.magic);
        return ERRCODE_INVALID_PARAM;
    }

    errcode_t ret = g_sync.adapter.refresh_adv_cb(&g_sync.field);
    if (ret != ERRCODE_SUCC) {
        osal_printk("%s[BP] publish FAIL refresh_adv_cb ret:0x%x\r\n", STORAGE_SYNC_LOG, ret);
        return ret;
    }

    osal_printk("%s[BP] publish OK tag:%u qty:%u status:0x%02x bat:%u seq:%u\r\n",
                STORAGE_SYNC_LOG,
                g_sync.field.tag_id,
                g_sync.field.qty,
                g_sync.field.status,
                g_sync.field.battery,
                g_sync.field.seq);
    return ERRCODE_SUCC;
}

uint16_t storage_sync_get_qty(void)
{
    return g_sync.field.qty;
}

uint16_t storage_sync_get_tag_id(void)
{
    return g_sync.field.tag_id;
}

void storage_sync_get_field(shared_proto_adv_field_t *field)
{
    if (field == NULL) {
        return;
    }

    *field = g_sync.field;
}

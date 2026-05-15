#include "shared_protocol.h"

#include "common_def.h"
#include "soc_osal.h"

#define SHARED_PROTO_LOG "[BS2x_PROTO]"
#define SHARED_PROTO_UPDATE_CMD_LEN 3u
#define SHARED_PROTO_BIND_CMD_LEN 3u

static void proto_write_u16_be(uint8_t *buf, uint16_t val)
{
    buf[0] = (uint8_t)(val >> 8);
    buf[1] = (uint8_t)(val & 0xFF);
}

static void proto_write_u32_be(uint8_t *buf, uint32_t val)
{
    buf[0] = (uint8_t)(val >> 24);
    buf[1] = (uint8_t)((val >> 16) & 0xFF);
    buf[2] = (uint8_t)((val >> 8) & 0xFF);
    buf[3] = (uint8_t)(val & 0xFF);
}

static uint16_t proto_read_u16_be(const uint8_t *buf)
{
    return ((uint16_t)buf[0] << 8) | (uint16_t)buf[1];
}

static uint32_t proto_read_u32_be(const uint8_t *buf)
{
    return ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
           ((uint32_t)buf[2] << 8) | (uint32_t)buf[3];
}

uint16_t shared_proto_serialize_adv_field(const shared_proto_adv_field_t *field,
                                          uint8_t *buf, uint16_t buf_len)
{
    if (field == NULL || buf == NULL) {
        osal_printk("%s[BP] serialize_adv FAIL param NULL\r\n", SHARED_PROTO_LOG);
        return 0;
    }
    if (buf_len < SHARED_PROTO_ADV_SERIALIZED_LEN) {
        osal_printk("%s[BP] serialize_adv FAIL buf_len:%u < %u\r\n",
                    SHARED_PROTO_LOG, buf_len, SHARED_PROTO_ADV_SERIALIZED_LEN);
        return 0;
    }

    proto_write_u32_be(&buf[0], field->magic);
    proto_write_u16_be(&buf[4], field->tag_id);
    proto_write_u16_be(&buf[6], field->qty);
    buf[8] = field->status;
    buf[9] = field->battery;
    proto_write_u16_be(&buf[10], field->seq);

    osal_printk("%s[BP] serialize_adv OK first4:0x%02X 0x%02X 0x%02X 0x%02X\r\n",
                SHARED_PROTO_LOG, buf[0], buf[1], buf[2], buf[3]);
    return SHARED_PROTO_ADV_SERIALIZED_LEN;
}

uint16_t shared_proto_serialize_inventory_rsp(const shared_proto_inventory_rsp_t *rsp,
                                              uint8_t *buf, uint16_t buf_len)
{
    if (rsp == NULL || buf == NULL) {
        return 0;
    }
    if (buf_len < SHARED_PROTO_INVENTORY_RSP_SERIALIZED_LEN) {
        return 0;
    }

    buf[0] = rsp->cmd;
    proto_write_u16_be(&buf[1], rsp->tag_id);
    proto_write_u16_be(&buf[3], rsp->qty);
    buf[5] = rsp->status;
    buf[6] = rsp->battery;
    proto_write_u16_be(&buf[7], rsp->seq);

    osal_printk("%s[BP] serialize_inv_rsp cmd:0x%02X tag:%u qty:%u\r\n",
                SHARED_PROTO_LOG, rsp->cmd, rsp->tag_id, rsp->qty);
    return SHARED_PROTO_INVENTORY_RSP_SERIALIZED_LEN;
}

uint16_t shared_proto_serialize_bind_rsp(const shared_proto_bind_rsp_t *rsp,
                                         uint8_t *buf, uint16_t buf_len)
{
    if (rsp == NULL || buf == NULL) {
        return 0;
    }
    if (buf_len < SHARED_PROTO_BIND_RSP_SERIALIZED_LEN) {
        return 0;
    }

    buf[0] = rsp->cmd;
    proto_write_u16_be(&buf[1], rsp->tag_id);

    osal_printk("%s[BP] serialize_bind_rsp cmd:0x%02X tag:%u\r\n",
                SHARED_PROTO_LOG, rsp->cmd, rsp->tag_id);
    return SHARED_PROTO_BIND_RSP_SERIALIZED_LEN;
}

bool shared_proto_deserialize_adv_field(const uint8_t *buf, uint16_t len,
                                        shared_proto_adv_field_t *field)
{
    if (buf == NULL || field == NULL) {
        return false;
    }
    if (len < SHARED_PROTO_ADV_SERIALIZED_LEN) {
        osal_printk("%s[BP] deserialize_adv FAIL len:%u < %u\r\n",
                    SHARED_PROTO_LOG, len, SHARED_PROTO_ADV_SERIALIZED_LEN);
        return false;
    }

    field->magic = proto_read_u32_be(&buf[0]);
    field->tag_id = proto_read_u16_be(&buf[4]);
    field->qty = proto_read_u16_be(&buf[6]);
    field->status = buf[8];
    field->battery = buf[9];
    field->seq = proto_read_u16_be(&buf[10]);

    osal_printk("%s[BP] deserialize_adv magic:0x%08X tag:%u qty:%u\r\n",
                SHARED_PROTO_LOG, field->magic, field->tag_id, field->qty);
    return true;
}

bool shared_proto_adv_field_is_valid(const shared_proto_adv_field_t *field)
{
    if (field == NULL) {
        osal_printk("%s[BP] adv_field_is_valid FAIL field=NULL\r\n", SHARED_PROTO_LOG);
        return false;
    }

    bool valid = field->magic == SHARED_PROTO_MAGIC;
    osal_printk("%s[BP] adv_field_is_valid magic:0x%08X expect:0x%08X result:%s\r\n",
                SHARED_PROTO_LOG, field->magic, SHARED_PROTO_MAGIC, valid ? "OK" : "FAIL");
    return valid;
}

bool shared_proto_parse_unicast_cmd(const uint8_t *data, uint16_t len, shared_proto_unicast_cmd_t *cmd)
{
    if (data == NULL || cmd == NULL || len == 0) {
        osal_printk("%s[BP] parse FAIL data=%s cmd=%s len=%u\r\n",
                    SHARED_PROTO_LOG,
                    data ? "OK" : "NULL",
                    cmd ? "OK" : "NULL",
                    len);
        return false;
    }

    /* 去除尾部 CR(0x0D) LF(0x0A)，兼容串口工具自动追加换行 */
    while (len > 0 && (data[len - 1] == 0x0D || data[len - 1] == 0x0A)) {
        len--;
    }

    if (len == 0) {
        osal_printk("%s[BP] parse FAIL len=0 after strip CR/LF\r\n", SHARED_PROTO_LOG);
        return false;
    }

    osal_printk("%s[BP] parse enter cmd:0x%02X len:%u raw:", SHARED_PROTO_LOG, data[0], len);
    for (uint16_t i = 0; i < len && i < 8; i++) {
        osal_printk(" %02X", data[i]);
    }
    osal_printk("\r\n");

    cmd->action = SHARED_PROTO_ACTION_NONE;
    cmd->qty = 0;
    cmd->tag_id = 0;

    if (data[0] == SHARED_PROTO_CMD_STOP_FIND && len == 1) {
        cmd->action = SHARED_PROTO_ACTION_STOP_FIND;
        osal_printk("%s[BP] parse OK action=STOP_FIND(0x00)\r\n", SHARED_PROTO_LOG);
        return true;
    }

    if (data[0] == SHARED_PROTO_CMD_FIND_ME && len == 1) {
        cmd->action = SHARED_PROTO_ACTION_FIND_ME;
        osal_printk("%s[BP] parse OK action=FIND_ME(0x01)\r\n", SHARED_PROTO_LOG);
        return true;
    }

    if (data[0] == SHARED_PROTO_CMD_INVENTORY && len == 1) {
        cmd->action = SHARED_PROTO_ACTION_INVENTORY;
        osal_printk("%s[BP] parse OK action=INVENTORY(0x02)\r\n", SHARED_PROTO_LOG);
        return true;
    }

    if (data[0] == SHARED_PROTO_CMD_UPDATE_QTY && len >= SHARED_PROTO_UPDATE_CMD_LEN) {
        cmd->action = SHARED_PROTO_ACTION_UPDATE_QTY;
        cmd->qty = ((uint16_t)data[1] << 8) | (uint16_t)data[2];
        osal_printk("%s[BP] parse OK action=UPDATE_QTY(0x10) qty=%u\r\n", SHARED_PROTO_LOG, cmd->qty);
        return true;
    }

    if (data[0] == SHARED_PROTO_CMD_BIND_TAG && len >= SHARED_PROTO_BIND_CMD_LEN) {
        cmd->action = SHARED_PROTO_ACTION_BIND_TAG;
        cmd->tag_id = ((uint16_t)data[1] << 8) | (uint16_t)data[2];
        osal_printk("%s[BP] parse OK action=BIND_TAG(0x20) tag_id=%u\r\n", SHARED_PROTO_LOG, cmd->tag_id);
        return true;
    }

    if (data[0] == SHARED_PROTO_CMD_UNBIND_TAG && len == 1) {
        cmd->action = SHARED_PROTO_ACTION_UNBIND_TAG;
        osal_printk("%s[BP] parse OK action=UNBIND_TAG(0x21)\r\n", SHARED_PROTO_LOG);
        return true;
    }

    osal_printk("%s[BP] parse FAIL unknown cmd:0x%02X or len mismatch(len=%u)\r\n",
                SHARED_PROTO_LOG, data[0], len);
    return false;
}

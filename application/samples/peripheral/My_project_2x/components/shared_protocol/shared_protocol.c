#include "shared_protocol.h"

#include "common_def.h"
#include "soc_osal.h"

#define SHARED_PROTO_LOG "[BS2x_PROTO]"
#define SHARED_PROTO_UPDATE_CMD_LEN 3u
#define SHARED_PROTO_BIND_CMD_LEN 3u

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

    osal_printk("%s[BP] parse FAIL unknown cmd:0x%02X or len mismatch(len=%u)\r\n",
                SHARED_PROTO_LOG, data[0], len);
    return false;
}

#include "shared_protocol.h"

#define SHARED_PROTO_UPDATE_CMD_LEN 3u

bool shared_proto_adv_field_is_valid(const shared_proto_adv_field_t *field)
{
    if (field == NULL) {
        return false;
    }

    return field->magic == SHARED_PROTO_MAGIC;
}

bool shared_proto_parse_unicast_cmd(const uint8_t *data, uint16_t len, shared_proto_unicast_cmd_t *cmd)
{
    if (data == NULL || cmd == NULL || len == 0) {
        return false;
    }

    cmd->action = SHARED_PROTO_ACTION_NONE;
    cmd->qty = 0;

    if (data[0] == SHARED_PROTO_CMD_FIND_ME && len == 1) {
        cmd->action = SHARED_PROTO_ACTION_FIND_ME;
        return true;
    }

    if (data[0] == SHARED_PROTO_CMD_UPDATE_QTY && len >= SHARED_PROTO_UPDATE_CMD_LEN) {
        cmd->action = SHARED_PROTO_ACTION_UPDATE_QTY;
        cmd->qty = (uint16_t)data[1] | ((uint16_t)data[2] << 8);
        return true;
    }

    return false;
}

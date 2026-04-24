#ifndef MY_PROJECT_2X_SHARED_PROTOCOL_H
#define MY_PROJECT_2X_SHARED_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SHARED_PROTO_MAGIC 0x534C4550u
#define SHARED_PROTO_ADV_FIELD_LEN 12u

#define SHARED_PROTO_CMD_FIND_ME 0x01u
#define SHARED_PROTO_CMD_UPDATE_QTY 0x55u

#pragma pack(push, 1)
typedef struct {
    uint32_t magic;
    uint16_t tag_id;
    uint16_t qty;
    uint8_t status;
    uint8_t battery;
    uint16_t seq;
} shared_proto_adv_field_t;
#pragma pack(pop)

_Static_assert(sizeof(shared_proto_adv_field_t) == SHARED_PROTO_ADV_FIELD_LEN,
               "shared_proto_adv_field_t must be 12 bytes");

typedef enum {
    SHARED_PROTO_ACTION_NONE = 0,
    SHARED_PROTO_ACTION_FIND_ME,
    SHARED_PROTO_ACTION_UPDATE_QTY,
} shared_proto_action_t;

typedef struct {
    shared_proto_action_t action;
    uint16_t qty;
} shared_proto_unicast_cmd_t;

bool shared_proto_adv_field_is_valid(const shared_proto_adv_field_t *field);
bool shared_proto_parse_unicast_cmd(const uint8_t *data, uint16_t len, shared_proto_unicast_cmd_t *cmd);

#ifdef __cplusplus
}
#endif

#endif /* MY_PROJECT_2X_SHARED_PROTOCOL_H */

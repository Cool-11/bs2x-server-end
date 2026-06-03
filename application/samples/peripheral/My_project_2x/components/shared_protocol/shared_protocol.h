#ifndef MY_PROJECT_2X_SHARED_PROTOCOL_H
#define MY_PROJECT_2X_SHARED_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SHARED_PROTO_MAGIC 0xAABBCCDDu
#define SHARED_PROTO_ADV_FIELD_LEN 12u

#define SHARED_PROTO_CMD_STOP_FIND 0x00u
#define SHARED_PROTO_CMD_FIND_ME 0x01u
#define SHARED_PROTO_CMD_INVENTORY 0x02u
#define SHARED_PROTO_CMD_UPDATE_QTY 0x10u
#define SHARED_PROTO_CMD_BIND_TAG 0x20u
#define SHARED_PROTO_CMD_UNBIND_TAG 0x21u

/* status字段语义（与WS63端对齐） */
#define SHARED_PROTO_STATUS_IDLE         0x00u  /* 空闲：默认/寻物恢复 */
#define SHARED_PROTO_STATUS_FINDING      0x01u  /* 寻物中：收到0x01命令 */
#define SHARED_PROTO_STATUS_IN_USE       0x02u  /* 使用中：tag_id≠0且有库存 */
#define SHARED_PROTO_STATUS_UNBOUND      0x03u  /* 未配网：tag_id==0 */
#define SHARED_PROTO_STATUS_LOW_BATTERY  0x04u  /* 低电量：battery≤10%（覆盖原状态） */

/* 向后兼容别名 */
#define SHARED_PROTO_STATUS_NORMAL   SHARED_PROTO_STATUS_IDLE
#define SHARED_PROTO_STATUS_OUTSTOCK SHARED_PROTO_STATUS_IN_USE

#define SHARED_PROTO_RSP_INVENTORY 0x82u
#define SHARED_PROTO_RSP_BIND_OK 0xA0u
#define SHARED_PROTO_RSP_UNBIND_OK 0xA1u
#define SHARED_PROTO_RSP_BIND_FAIL 0xAFu

#define SHARED_PROTO_INVENTORY_RSP_LEN 9u
#define SHARED_PROTO_BIND_OK_RSP_LEN 3u
#define SHARED_PROTO_BIND_FAIL_RSP_LEN 3u

#define SHARED_PROTO_ADV_SERIALIZED_LEN 12u
#define SHARED_PROTO_INVENTORY_RSP_SERIALIZED_LEN 9u
#define SHARED_PROTO_BIND_RSP_SERIALIZED_LEN 3u

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

#pragma pack(push, 1)
typedef struct {
    uint8_t cmd;
    uint16_t tag_id;
    uint16_t qty;
    uint8_t status;
    uint8_t battery;
    uint16_t seq;
} shared_proto_inventory_rsp_t;
#pragma pack(pop)

_Static_assert(sizeof(shared_proto_inventory_rsp_t) == SHARED_PROTO_INVENTORY_RSP_LEN,
               "shared_proto_inventory_rsp_t must be 9 bytes");

#pragma pack(push, 1)
typedef struct {
    uint8_t cmd;
    uint16_t tag_id;
} shared_proto_bind_rsp_t;
#pragma pack(pop)

_Static_assert(sizeof(shared_proto_bind_rsp_t) == SHARED_PROTO_BIND_OK_RSP_LEN,
               "shared_proto_bind_rsp_t must be 3 bytes");

/* 解绑响应复用 bind_rsp 结构体（cmd + tag_id），此处加语义别名 */
typedef shared_proto_bind_rsp_t shared_proto_unbind_rsp_t;

typedef enum {
    SHARED_PROTO_ACTION_NONE = 0,
    SHARED_PROTO_ACTION_STOP_FIND,
    SHARED_PROTO_ACTION_FIND_ME,
    SHARED_PROTO_ACTION_INVENTORY,
    SHARED_PROTO_ACTION_UPDATE_QTY,
    SHARED_PROTO_ACTION_BIND_TAG,
    SHARED_PROTO_ACTION_UNBIND_TAG,
} shared_proto_action_t;

typedef struct {
    shared_proto_action_t action;
    uint16_t qty;
    uint16_t tag_id;
} shared_proto_unicast_cmd_t;

bool shared_proto_adv_field_is_valid(const shared_proto_adv_field_t *field);
bool shared_proto_parse_unicast_cmd(const uint8_t *data, uint16_t len, shared_proto_unicast_cmd_t *cmd);

uint16_t shared_proto_serialize_adv_field(const shared_proto_adv_field_t *field,
                                          uint8_t *buf, uint16_t buf_len);
uint16_t shared_proto_serialize_inventory_rsp(const shared_proto_inventory_rsp_t *rsp,
                                              uint8_t *buf, uint16_t buf_len);
uint16_t shared_proto_serialize_bind_rsp(const shared_proto_bind_rsp_t *rsp,
                                         uint8_t *buf, uint16_t buf_len);
bool shared_proto_deserialize_adv_field(const uint8_t *buf, uint16_t len,
                                        shared_proto_adv_field_t *field);

#ifdef __cplusplus
}
#endif

#endif /* MY_PROJECT_2X_SHARED_PROTOCOL_H */

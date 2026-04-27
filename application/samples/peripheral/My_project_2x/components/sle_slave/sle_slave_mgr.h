#ifndef MY_PROJECT_2X_SLE_SLAVE_MGR_H
#define MY_PROJECT_2X_SLE_SLAVE_MGR_H

#include <stdint.h>
#include <stdbool.h>

#include "errcode.h"
#include "shared_protocol.h"
#include "sle_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CONFIG_MY_PROJECT_2X_MAX_CONNECTIONS
#define CONFIG_MY_PROJECT_2X_MAX_CONNECTIONS 4
#endif

typedef void (*sle_slave_cmd_cb_t)(const shared_proto_unicast_cmd_t *cmd);
typedef void (*sle_slave_conn_cb_t)(uint16_t conn_id, bool connected);

typedef struct {
    sle_slave_cmd_cb_t on_unicast_cmd;
    sle_slave_conn_cb_t on_conn_state_changed;
} sle_slave_callbacks_t;

errcode_t sle_slave_init(const sle_slave_callbacks_t *cb);

errcode_t sle_slave_start(void);
errcode_t sle_slave_stop(void);

errcode_t sle_slave_refresh_adv_payload(const shared_proto_adv_field_t *field);

uint16_t sle_slave_get_conn_id(void);
bool sle_slave_is_connected(void);
uint8_t sle_slave_get_active_conn_count(void);

errcode_t sle_slave_broadcast_notify_all(const uint8_t *data, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* MY_PROJECT_2X_SLE_SLAVE_MGR_H */

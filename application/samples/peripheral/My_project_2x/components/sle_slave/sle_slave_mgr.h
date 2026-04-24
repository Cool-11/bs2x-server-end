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

typedef void (*sle_slave_cmd_cb_t)(const shared_proto_unicast_cmd_t *cmd);

typedef struct {
    sle_slave_cmd_cb_t on_unicast_cmd;
} sle_slave_callbacks_t;

errcode_t sle_slave_init(const sle_slave_callbacks_t *cb);

errcode_t sle_slave_start(void);
errcode_t sle_slave_stop(void);

errcode_t sle_slave_refresh_adv_payload(const shared_proto_adv_field_t *field);

uint16_t sle_slave_get_conn_id(void);
bool sle_slave_is_connected(void);

#ifdef __cplusplus
}
#endif

#endif /* MY_PROJECT_2X_SLE_SLAVE_MGR_H */

# WS63 Client Dual-Connection Porting Guide

This note provides a minimal migration pattern for a WS63 client to connect to two BS21e servers.

## 1) Why this pattern

- Connect in `seek_disable_cb` after `sle_stop_seek()` to avoid scan/connect overlap races.
- Keep a fixed target address list for deterministic dual connection.
- Re-start scan only when connected count is less than 2.

## 2) Required state

```c
#define TARGET_SERVER_NUM 2

static sle_addr_t g_target_addr[TARGET_SERVER_NUM] = {
    {.type = 0, .addr = {0xAA,0xBB,0xCC,0xDD,0xEE,0x01}},
    {.type = 0, .addr = {0xAA,0xBB,0xCC,0xDD,0xEE,0x02}},
};

static uint8_t g_is_connected[TARGET_SERVER_NUM] = {0};
static uint16_t g_conn_id[TARGET_SERVER_NUM] = {0};
static uint8_t g_connected_num = 0;
static uint8_t g_pending_connect_idx = TARGET_SERVER_NUM;
```

## 3) Scan -> connect chain

```c
static uint8_t find_unconnected_target(const uint8_t *addr)
{
    for (uint8_t i = 0; i < TARGET_SERVER_NUM; i++) {
        if (g_is_connected[i]) {
            continue;
        }
        if (memcmp(addr, g_target_addr[i].addr, SLE_ADDR_LEN) == 0) {
            return i;
        }
    }
    return TARGET_SERVER_NUM;
}

static void ws63_seek_result_cb(sle_seek_result_info_t *r)
{
    if (r == NULL) {
        return;
    }
    uint8_t idx = find_unconnected_target(r->addr.addr);
    if (idx < TARGET_SERVER_NUM) {
        g_pending_connect_idx = idx;
        sle_stop_seek();
    }
}

static void ws63_seek_disable_cb(errcode_t status)
{
    (void)status;
    if (g_pending_connect_idx < TARGET_SERVER_NUM) {
        sle_connect_remote_device(&g_target_addr[g_pending_connect_idx]);
    }
}
```

## 4) Connection state callback

```c
static uint8_t find_connected_index(const uint8_t *addr)
{
    for (uint8_t i = 0; i < TARGET_SERVER_NUM; i++) {
        if (!g_is_connected[i]) {
            continue;
        }
        if (memcmp(addr, g_target_addr[i].addr, SLE_ADDR_LEN) == 0) {
            return i;
        }
    }
    return TARGET_SERVER_NUM;
}

static void ws63_conn_state_cb(uint16_t conn_id,
                               const sle_addr_t *addr,
                               sle_acb_state_t state,
                               sle_pair_state_t pair_state,
                               sle_disc_reason_t reason)
{
    (void)pair_state;
    (void)reason;
    if (addr == NULL) {
        return;
    }

    if (state == SLE_ACB_STATE_CONNECTED) {
        uint8_t idx = find_unconnected_target(addr->addr);
        if (idx < TARGET_SERVER_NUM) {
            g_is_connected[idx] = 1;
            g_conn_id[idx] = conn_id;
            g_connected_num++;
            g_pending_connect_idx = TARGET_SERVER_NUM;
            sle_pair_remote_device(addr);
        }
        if (g_connected_num < TARGET_SERVER_NUM) {
            sle_multi_conn_start_scan();
        }
    } else if (state == SLE_ACB_STATE_DISCONNECTED) {
        uint8_t idx = find_connected_index(addr->addr);
        if (idx < TARGET_SERVER_NUM) {
            g_is_connected[idx] = 0;
            g_conn_id[idx] = 0;
            if (g_connected_num > 0) {
                g_connected_num--;
            }
        }
        if (g_connected_num < TARGET_SERVER_NUM) {
            sle_multi_conn_start_scan();
        }
    }
}
```

## 5) Registration order

1. `sle_announce_seek_register_callbacks()`
2. `sle_connection_register_callbacks()`
3. `ssapc_register_client()` and `ssapc_register_callbacks()`
4. `sle_set_seek_param()` + `sle_start_seek()`

## 6) Practical notes

- Read target MACs from your two BS21e boards and fill `g_target_addr`.
- Keep scan interval/window moderate first (100/50) until stable.
- After dual connection is stable, then tune interval/latency for power and throughput.

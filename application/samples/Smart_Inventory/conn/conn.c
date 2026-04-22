/**
 *******************************************************************************
 * @file conn.c
 * @brief Smart Inventory Tag - Connection Management Module Implementation
 *
 *  Description:
 *   本模块负责星闪SLE连接管理(Connection Manager)和GATT服务创建。
 *   包括：GATT服务注册、连接管理、Notify/Indication数据发送。
 *
 * @version V1.0
 * @date 2024
 *******************************************************************************
 */
#include <stdio.h>
#include <string.h>
#include "conn.h"
#include "common.h"
#include "data.h"
#include "callback.h"
#include "sle_common.h"
#include "sle_connection_manager.h"
#include "sle_ssap_server.h"

#define BD_ADDR_LEN 6

static uint8_t g_server_id = 0;
static uint16_t g_service_handle = 0;
static uint16_t g_property_handle = 0;
static uint16_t g_cmd_property_handle = 0;

static conn_state_t g_conn_state = CONN_STATE_DISCONNECTED;
static conn_info_t g_conn_info = {
    .conn_id = 0,
    .peer_addr = {0},
    .state = CONN_STATE_DISCONNECTED,
    .mtu_size = CONN_MTU_SIZE_DEFAULT,
    .connection_handle = 0
};

static void (*g_connect_callback)(void) = NULL;
static void (*g_disconnect_callback)(void) = NULL;

static const uint8_t g_inventory_service_uuid[] = {
    0x22, 0x22
};

static const uint8_t g_inventory_data_uuid[] = {
    0x23, 0x23
};

static const uint8_t g_command_uuid[] = {
    0x24, 0x24
};

static uint8_t g_inventory_data_value[DATA_PACKET_LEN_CONNECTED] = {0};

static errcode_t conn_service_add(void)
{
    errcode_t ret;
    sle_uuid_t app_uuid = {0};

    app_uuid.len = sizeof(g_inventory_service_uuid);
    if (memcpy_s(app_uuid.uuid, app_uuid.len, g_inventory_service_uuid, sizeof(g_inventory_service_uuid)) != EOK) {
        LOGE("Service UUID memcpy failed");
        return ERR_COMMON_INVALID_PARAM;
    }

    ret = ssaps_register_server(&app_uuid, &g_server_id);
    if (ret != 0) {
        LOGE("Register server failed: 0x%08X", ret);
        return ret;
    }
    LOGI("Server registered, server_id=%u", g_server_id);

    ssaps_service_info_t service_info = {0};
    service_info.type = SSAP_SERVICE_TYPE_PRIMARY;

    ret = ssaps_add_service_sync(g_server_id, &service_info, &g_service_handle);
    if (ret != 0) {
        LOGE("Add service failed: 0x%08X", ret);
        ssaps_unregister_server(g_server_id);
        return ret;
    }
    LOGI("Service added, handle=%u", g_service_handle);

    return 0;
}

static errcode_t conn_property_add(void)
{
    errcode_t ret;

    ssaps_property_info_t property = {0};
    property.permissions = SSAP_PERMISSION_READ | SSAP_PERMISSION_WRITE|SSAP_PERMISSION_NOTIFY;
    (void)memcpy_s(property.uuid.uuid, sizeof(g_inventory_data_uuid),
                   g_inventory_data_uuid, sizeof(g_inventory_data_uuid));
    property.uuid.len = sizeof(g_inventory_data_uuid);
    property.operate_indication = SSAP_OPERATE_INDICATION_BIT_NOTIFY;

    (void)memcpy_s(g_inventory_data_value, sizeof(g_inventory_data_value),
                   (const uint8_t[]){0xAA, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x00, 0x00, 0x00, 0x00, 0x00, 0x55},
                   DATA_PACKET_LEN_CONNECTED);
    property.value = g_inventory_data_value;
    property.value_len = sizeof(g_inventory_data_value);

    ret = ssaps_add_property_sync(g_server_id, g_service_handle, &property, &g_property_handle);
    if (ret != 0) {
        LOGE("Add property (inventory data) failed: 0x%08X", ret);
        return ret;
    }
    LOGI("Property (inventory data) added, handle=%u", g_property_handle);

    ssaps_property_info_t cmd_property = {0};
    cmd_property.permissions = SSAP_PERMISSION_WRITE;
    (void)memcpy_s(cmd_property.uuid.uuid, sizeof(g_command_uuid),
                   g_command_uuid, sizeof(g_command_uuid));
    cmd_property.uuid.len = sizeof(g_command_uuid);
    cmd_property.value = (uint8_t *)"\x00";
    cmd_property.value_len = 1;

    ret = ssaps_add_property_sync(g_server_id, g_service_handle, &cmd_property, &g_cmd_property_handle);
    if (ret != 0) {
        LOGW("Add property (command) failed: 0x%08X (optional)", ret);
    } else {
        LOGI("Property (command) added, handle=%u", g_cmd_property_handle);
    }

    return 0;
}

static errcode_t conn_start_service(void)
{
    errcode_t ret = ssaps_start_service(g_server_id, g_service_handle);
    if (ret != 0) {
        LOGE("Start service failed: 0x%08X", ret);
        return ret;
    }
    LOGI("Service started");
    return 0;
}

errcode_t conn_init(void)
{
    LOGI("Conn module initializing...");
    g_conn_state = CONN_STATE_DISCONNECTED;
    (void)memset_s(&g_conn_info, sizeof(g_conn_info), 0, sizeof(g_conn_info));
    LOGI("Conn module initialized");
    return 0;
}

errcode_t conn_register_server(void)
{
    errcode_t ret;

    ret = conn_service_add();
    if (ret != 0) {
        return ret;
    }

    ret = conn_property_add();
    if (ret != 0) {
        ssaps_unregister_server(g_server_id);
        return ret;
    }

    ret = conn_start_service();
    if (ret != 0) {
        ssaps_unregister_server(g_server_id);
        return ret;
    }

    LOGI("GATT server registered successfully");
    LOGI("  server_id=%u, service_handle=%u, property_handle=%u",
         g_server_id, g_service_handle, g_property_handle);

    ssap_exchange_info_t info = {0};
    info.mtu_size = 251;
    ssaps_set_info(g_server_id, &info);
    LOGI("MTU set to 251");

    return 0;
}

errcode_t conn_disconnect(void)
{
    if (g_conn_info.conn_id == 0) {
        LOGW("No active connection to disconnect");
        return 0;
    }

    errcode_t ret = sle_disconnect(g_conn_info.connection_handle);
    if (ret != 0) {
        LOGE("Disconnect failed: 0x%08X", ret);
        return ret;
    }

    LOGI("Disconnect request sent, conn_id=%u", g_conn_info.conn_id);
    return 0;
}

errcode_t conn_send_notify(const uint8_t *data, uint16_t len)
{
    if (data == NULL || len == 0) {
        return ERR_COMMON_NULL_PTR;
    }

    if (g_conn_info.conn_id == 0) {
        LOGE("Not connected, cannot send notify");
        return ERR_COMMON_INVALID_PARAM;
    }

    ssaps_ntf_ind_t param = {0};
    param.handle = g_property_handle;
    param.type = SSAP_PROPERTY_TYPE_VALUE;
    param.value = (uint8_t *)data;
    param.value_len = len;

    errcode_t ret = ssaps_notify_indicate(g_server_id, g_conn_info.conn_id, &param);
    if (ret != 0) {
        LOGE("Send notify failed: 0x%08X", ret);
        return ret;
    }

    char hex_str[64] = {0};
    uint16_t print_len = len > 16 ? 16 : len;
    common_hex_to_string(data, print_len, hex_str, sizeof(hex_str));
    LOGI("Notify sent: len=%u, data(%s)=%s", len, len > 16 ? "partial" : "full", hex_str);
    return 0;
}

errcode_t conn_send_indication(const uint8_t *data, uint16_t len)
{
    if (data == NULL || len == 0) {
        return ERR_COMMON_NULL_PTR;
    }

    if (g_conn_info.conn_id == 0) {
        LOGE("Not connected, cannot send indication");
        return ERR_COMMON_INVALID_PARAM;
    }

    ssaps_ntf_ind_t param = {0};
    param.handle = g_property_handle;
    param.type = SSAP_PROPERTY_TYPE_VALUE;
    param.value = (uint8_t *)data;
    param.value_len = len;

    errcode_t ret = ssaps_notify_indicate(g_server_id, g_conn_info.conn_id, &param);
    if (ret != 0) {
        LOGE("Send indication failed: 0x%08X", ret);
        return ret;
    }

    LOGI("Indication sent: len=%u", len);
    return 0;
}

errcode_t conn_update_param(uint16_t min_interval, uint16_t max_interval,
                            uint16_t latency, uint16_t timeout)
{
    if (g_conn_info.connection_handle == 0) {
        LOGE("No connection to update param");
        return ERR_COMMON_INVALID_PARAM;
    }

    sle_connection_param_update_t param = {0};
    param.conn_handle = g_conn_info.connection_handle;
    param.conn_interval_min = min_interval;
    param.conn_interval_max = max_interval;
    param.conn_max_latency = latency;
    param.conn_supervision_timeout = timeout;

    errcode_t ret = sle_connection_param_update(&param);
    if (ret != 0) {
        LOGE("Connection param update failed: 0x%08X", ret);
        return ret;
    }

    LOGI("Connection param update requested: interval=[%d,%d], latency=%d, timeout=%d",
         min_interval, max_interval, latency, timeout);
    return 0;
}

conn_state_t conn_get_state(void)
{
    return g_conn_state;
}

uint8_t conn_is_connected(void)
{
    return (g_conn_state == CONN_STATE_CONNECTED) ? 1 : 0;
}

conn_info_t *conn_get_info(void)
{
    return &g_conn_info;
}

uint16_t conn_get_conn_id(void)
{
    return g_conn_info.conn_id;
}

void conn_set_connect_callback(void (*callback)(void))
{
    g_connect_callback = callback;
}

void conn_set_disconnect_callback(void (*callback)(void))
{
    g_disconnect_callback = callback;
}

void conn_update_state(conn_state_t new_state, uint16_t conn_id, const uint8_t *peer_addr)
{
    conn_state_t old_state = g_conn_state;
    g_conn_state = new_state;
    g_conn_info.state = new_state;

    if (conn_id != 0) {
        g_conn_info.conn_id = conn_id;
    }

    if (peer_addr != NULL) {
        (void)memcpy_s(g_conn_info.peer_addr, sizeof(g_conn_info.peer_addr), peer_addr, BD_ADDR_LEN);
    }

    if (new_state == CONN_STATE_CONNECTED && old_state != CONN_STATE_CONNECTED) {
        if (g_connect_callback != NULL) {
            g_connect_callback();
        }
    } else if (new_state == CONN_STATE_DISCONNECTED && old_state == CONN_STATE_CONNECTED) {
        g_conn_info.conn_id = 0;
        (void)memset_s(g_conn_info.peer_addr, sizeof(g_conn_info.peer_addr), 0, BD_ADDR_LEN);
        if (g_disconnect_callback != NULL) {
            g_disconnect_callback();
        }
    }
}

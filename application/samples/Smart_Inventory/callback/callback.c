/**
 *******************************************************************************
 * @file callback.c
 * @brief Smart Inventory Tag - Callback Functions Module Implementation
 *
 *  Description:
 *   本模块负责定义和注册所有SLE和PM相关的回调函数。
 *   包括：设备管理回调、广播回调、连接回调、GATT服务回调。
 *
 *  设计特点:
 *   - 完全解耦设计：每个回调函数独立，互不影响
 *   - 回调注册机制：通过函数指针注册，灵活可配置
 *   - 中间层设计：不直接处理业务逻辑，仅做路由分发
 *
 * @version V1.0
 * @date 2024
 *******************************************************************************
 */
#include <stdio.h>
#include <string.h>
#include "callback.h"
#include "common.h"
#include "data.h"
#include "adv.h"
#include "conn.h"
#include "sle_common.h"
#include "sle_device_manager.h"
#include "sle_device_discovery.h"
#include "sle_connection_manager.h"
#include "sle_ssap_server.h"

#define ENABLE_SLE_STATUS_OK  1
#define ENABLE_SLE_STATUS_NOT_OK  0

static uint8_t g_sle_enable_status = ENABLE_SLE_STATUS_NOT_OK;

static callback_gatt_read_t g_gatt_read_callback = NULL;
static callback_gatt_write_t g_gatt_write_callback = NULL;

uint8_t callback_get_sle_enable_status(void)
{
    return g_sle_enable_status;
}

void callback_sle_power_on_handler(uint8_t status)
{
    (void)status;
    LOGI("SLE Power On Handler called");
}

void callback_sle_enable_handler(uint8_t status)
{
    if (status == 0) {
        g_sle_enable_status = ENABLE_SLE_STATUS_OK;
        LOGI("SLE Enable success");
    } else {
        g_sle_enable_status = ENABLE_SLE_STATUS_NOT_OK;
        LOGE("SLE Enable failed, status=0x%02X", status);
    }
}

void callback_sle_disable_handler(uint8_t status)
{
    (void)status;
    g_sle_enable_status = ENABLE_SLE_STATUS_NOT_OK;
    LOGI("SLE Disabled");
}

void callback_adv_enable_handler(uint32_t announce_id, errcode_t status)
{
    if (status == 0) {
        LOGI("Adv enabled successfully. announce_id=%u", announce_id);
    } else {
        LOGE("Adv enable failed. announce_id=%u, status=0x%08X", announce_id, status);
    }
}

void callback_adv_disable_handler(uint32_t announce_id, errcode_t status)
{
    if (status == 0) {
        LOGI("Adv disabled successfully. announce_id=%u", announce_id);
    } else {
        LOGW("Adv disable reported. announce_id=%u, status=0x%08X", announce_id, status);
    }
}

void callback_adv_terminal_handler(uint32_t announce_id)
{
    LOGI("Adv terminal. announce_id=%u", announce_id);
}

void callback_seek_result_handler(sle_seek_result_info_t *seek_result)
{
    if (seek_result == NULL) {
        return;
    }

    char addr_str[32] = {0};
    data_mac_to_string(seek_result->addr.addr, addr_str);
    LOGI("Seek result: addr=%s, rssi=%d, announce_id=%u",
         addr_str, seek_result->rssi, seek_result->announce_id);
}

void callback_conn_state_changed_handler(uint16_t conn_id, const sle_addr_t *addr,
    sle_acb_state_t conn_state, sle_pair_state_t pair_state, sle_disc_reason_t disc_reason)
{
    char addr_str[32] = {0};
    if (addr != NULL) {
        data_mac_to_string(addr->addr, addr_str);
    }

    LOGI("Conn state changed: conn_id=%u, addr=%s", conn_id, addr_str);
    LOGI("  conn_state=%d, pair_state=%d, disc_reason=0x%04X",
         conn_state, pair_state, disc_reason);

    switch (conn_state) {
        case SLE_ACB_STATE_CONNECTED:
            LOGI("  -> Connected");
            if (g_sle_enable_status == ENABLE_SLE_STATUS_OK) {
                adv_goods_data_t mock_data = {0};
                (void)data_get_mock_goods_data(&mock_data);
                LOGI("[Mock] Connection established. Sending dummy data to Gateway...");
                (void)conn_send_notify((const uint8_t *)&mock_data, sizeof(mock_data));
            }
            break;
        case SLE_ACB_STATE_DISCONNECTED:
            LOGI("  -> Disconnected, restarting adv...");
            adv_start(0);
            break;
        case SLE_ACB_STATE_CONNECTING:
            LOGI("  -> Connecting");
            break;
        default:
            LOGI("  -> Unknown state: %d", conn_state);
            break;
    }
}

void callback_pair_complete_handler(uint16_t conn_id, const sle_addr_t *addr, errcode_t status)
{
    char addr_str[32] = {0};
    if (addr != NULL) {
        data_mac_to_string(addr->addr, addr_str);
    }

    if (status == 0) {
        LOGI("Pair complete: conn_id=%u, addr=%s", conn_id, addr_str);
    } else {
        LOGE("Pair failed: conn_id=%u, addr=%s, status=0x%08X", conn_id, addr_str, status);
    }
}

void callback_conn_update_complete_handler(uint16_t conn_id, errcode_t status,
    const sle_connection_param_update_evt_t *param)
{
    if (status == 0) {
        LOGI("Conn update complete: conn_id=%u", conn_id);
        if (param != NULL) {
            LOGI("  interval=%d(%.2fms), latency=%d, timeout=%d(%.2fms)",
                 param->conn_interval, param->conn_interval * 1.25f,
                 param->conn_max_latency,
                 param->conn_supervision_timeout, param->conn_supervision_timeout * 10.0f);
        }
    } else {
        LOGW("Conn update failed: conn_id=%u, status=0x%08X", conn_id, status);
    }
}

void callback_phy_update_complete_handler(uint16_t conn_id, errcode_t status, const sle_set_phy_t *param)
{
    if (status == 0) {
        LOGI("PHY update complete: conn_id=%u", conn_id);
        if (param != NULL) {
            LOGI("  tx_phy=%d, rx_phy=%d", param->tx_phy, param->rx_phy);
        }
    } else {
        LOGW("PHY update failed: conn_id=%u, status=0x%08X", conn_id, status);
    }
}

void callback_gatt_read_request_handler(uint8_t server_id, uint16_t conn_id,
    const ssaps_req_read_cb_t *read_cb_para, errcode_t status)
{
    LOGI("GATT Read request: server_id=%u, conn_id=%u, handle=%u",
         server_id, conn_id, read_cb_para->handle);

    if (g_gatt_read_callback != NULL) {
        g_gatt_read_callback(server_id, conn_id, read_cb_para, status);
    }
}

void callback_gatt_write_request_handler(uint8_t server_id, uint16_t conn_id,
    const ssaps_req_write_cb_t *write_cb_para, errcode_t status)
{
    LOGI("GATT Write request: server_id=%u, conn_id=%u, handle=%u, len=%u",
         server_id, conn_id, write_cb_para->handle, write_cb_para->len);

    if (write_cb_para->len > 0 && write_cb_para->data != NULL) {
        char hex_str[64] = {0};
        uint16_t print_len = write_cb_para->len > 16 ? 16 : write_cb_para->len;
        common_hex_to_string(write_cb_para->data, print_len, hex_str, sizeof(hex_str));
        LOGI("  data(%s...): %s", write_cb_para->len > 16 ? "partial" : "full", hex_str);
    }

    if (g_gatt_write_callback != NULL) {
        g_gatt_write_callback(server_id, conn_id, write_cb_para, status);
    }
}

void callback_gatt_mtu_changed_handler(uint8_t server_id, uint16_t conn_id,
    const ssap_exchange_info_t *mtu_size, errcode_t status)
{
    if (status == 0) {
        LOGI("MTU changed: server_id=%u, conn_id=%u, mtu=%u",
             server_id, conn_id, mtu_size->mtu);
    } else {
        LOGW("MTU change failed: server_id=%u, conn_id=%u, status=0x%08X",
             server_id, conn_id, status);
    }
}

void callback_gatt_notify_confirm_handler(uint8_t server_id, uint16_t conn_id,
    uint16_t handle, errcode_t status)
{
    if (status == 0) {
        LOGI("Notify confirm: server_id=%u, conn_id=%u, handle=%u", server_id, conn_id, handle);
    } else {
        LOGW("Notify confirm failed: server_id=%u, conn_id=%u, handle=%u, status=0x%08X",
             server_id, conn_id, handle, status);
    }
}

errcode_t callback_register_sle_dev_manager(void)
{
    sle_dev_manager_callbacks_t dev_mgr_cbk = {0};

    dev_mgr_cbk.sle_power_on_cb = callback_sle_power_on_handler;
    dev_mgr_cbk.sle_enable_cb = callback_sle_enable_handler;
    dev_mgr_cbk.sle_disable_cb = callback_sle_disable_handler;

    errcode_t ret = sle_dev_manager_register_callbacks(&dev_mgr_cbk);
    if (ret != 0) {
        LOGE("SLE Device Manager register failed: 0x%08X", ret);
        return ret;
    }

    LOGI("SLE Device Manager callbacks registered");
    return 0;
}

errcode_t callback_register_sle_announce(void)
{
    sle_announce_seek_callbacks_t seek_cbks = {0};

    seek_cbks.announce_enable_cb = callback_adv_enable_handler;
    seek_cbks.announce_disable_cb = callback_adv_disable_handler;
    seek_cbks.announce_terminal_cb = callback_adv_terminal_handler;
    seek_cbks.seek_result_cb = callback_seek_result_handler;

    errcode_t ret = sle_announce_seek_register_callbacks(&seek_cbks);
    if (ret != 0) {
        LOGE("SLE Announce callbacks register failed: 0x%08X", ret);
        return ret;
    }

    LOGI("SLE Announce callbacks registered");
    return 0;
}

errcode_t callback_register_sle_connection(void)
{
    sle_connection_callbacks_t conn_cbks = {0};

    conn_cbks.connect_state_changed_cb = callback_conn_state_changed_handler;
    conn_cbks.pair_complete_cb = callback_pair_complete_handler;
    conn_cbks.conn_update_complete_cb = callback_conn_update_complete_handler;
    conn_cbks.phy_update_complete_cb = callback_phy_update_complete_handler;

    errcode_t ret = sle_connection_register_callbacks(&conn_cbks);
    if (ret != 0) {
        LOGE("SLE Connection callbacks register failed: 0x%08X", ret);
        return ret;
    }

    LOGI("SLE Connection callbacks registered");
    return 0;
}

errcode_t callback_register_sle_gatt_service(callback_gatt_read_t read_cb, callback_gatt_write_t write_cb)
{
    g_gatt_read_callback = read_cb;
    g_gatt_write_callback = write_cb;

    sle_ssaps_server_callbacks_t ssaps_cbk = {0};

    ssaps_cbk.exchange_mtu_cb = callback_gatt_mtu_changed_handler;
    ssaps_cbk.attribute_read_cb = callback_gatt_read_request_handler;
    ssaps_cbk.attribute_write_cb = callback_gatt_write_request_handler;
    ssaps_cbk.notify_confirm_cb = callback_gatt_notify_confirm_handler;

    errcode_t ret = sle_ssaps_register_callbacks(&ssaps_cbk);
    if (ret != 0) {
        LOGE("SLE GATT Service callbacks register failed: 0x%08X", ret);
        return ret;
    }

    LOGI("SLE GATT Service callbacks registered");
    return 0;
}

errcode_t callback_register_all(void)
{
    errcode_t ret;

    ret = callback_register_sle_dev_manager();
    if (ret != 0) {
        return ret;
    }

    ret = callback_register_sle_announce();
    if (ret != 0) {
        return ret;
    }

    ret = callback_register_sle_connection();
    if (ret != 0) {
        return ret;
    }

    LOGI("All SLE callbacks registered successfully");
    return 0;
}

/**
 *******************************************************************************
 * @file adv.c
 * @brief Smart Inventory Tag - Broadcast Configuration Module Implementation
 *
 *  Description:
 *   本模块负责星闪SLE广播(Advertise)相关配置和管理。
 *   包括：广播参数配置、广播数据填充、广播启动/停止控制。
 *
 * @version V1.0
 * @date 2024
 *******************************************************************************
 */
#include <stdio.h>
#include <string.h>
#include "adv.h"
#include "common.h"
#include "data.h"
#include "callback.h"
#include "sle_common.h"
#include "sle_device_discovery.h"

#define BD_ADDR_LEN 6

static adv_state_t g_adv_state = ADV_STATE_IDLE;
static uint8_t g_adv_enable = 0;
static uint8_t g_local_mac[BD_ADDR_LEN] = {0};
static adv_param_t g_adv_param = {
    .announce_handle = ADV_HANDLE_DEFAULT,
    .announce_mode = SLE_ANNOUNCE_MODE_CONNECTABLE_SCANABLE,
    .announce_gt_role = SLE_ANNOUNCE_ROLE_T_CAN_NEGO,
    .announce_level = SLE_ANNOUNCE_LEVEL_NORMAL,
    .announce_interval_min = ADV_INTERVAL_MIN_DEFAULT,
    .announce_interval_max = ADV_INTERVAL_MAX_DEFAULT,
    .announce_tx_power = ADV_TX_POWER_DEFAULT,
    .conn_interval_min = ADV_CONN_INTV_MIN_DEFAULT,
    .conn_interval_max = ADV_CONN_INTV_MAX_DEFAULT,
    .conn_max_latency = ADV_CONN_MAX_LATENCY,
    .conn_supervision_timeout = ADV_CONN_SUPERVISION_TIMEOUT
};

static uint8_t g_adv_data[ADV_DATA_LEN_MAX] = {0};
static uint8_t g_adv_rsp_data[ADV_DATA_LEN_MAX] = {0};

static void adv_set_local_addr(void)
{
    sle_addr_t sle_addr = {0};
    sle_addr.type = 0;
    if (memcpy_s(sle_addr.addr, BD_ADDR_LEN, g_local_mac, BD_ADDR_LEN) != EOK) {
        LOGE("MAC address memcpy failed");
    } else {
        LOGI("Local MAC set: %02X:%02X:%02X:%02X:%02X:%02X",
             g_local_mac[0], g_local_mac[1], g_local_mac[2],
             g_local_mac[3], g_local_mac[4], g_local_mac[5]);
    }
    sle_set_local_addr(&sle_addr);
}

static int adv_set_default_param(void)
{
    adv_set_local_addr();

    sle_announce_param_t param = {0};
    param.announce_handle = g_adv_param.announce_handle;
    param.announce_mode = g_adv_param.announce_mode;
    param.announce_gt_role = g_adv_param.announce_gt_role;
    param.announce_level = g_adv_param.announce_level;
    param.announce_channel_map = SLE_ADV_CHANNEL_MAP_DEFAULT;
    param.announce_interval_min = g_adv_param.announce_interval_min;
    param.announce_interval_max = g_adv_param.announce_interval_max;
    param.conn_interval_min = g_adv_param.conn_interval_min;
    param.conn_interval_max = g_adv_param.conn_interval_max;
    param.conn_max_latency = g_adv_param.conn_max_latency;
    param.conn_supervision_timeout = g_adv_param.conn_supervision_timeout;

    errcode_t ret = sle_set_announce_param(param.announce_handle, &param);
    if (ret != 0) {
        LOGE("Set announce param failed: 0x%08X", ret);
        return ret;
    }

    LOGI("Announce param set: mode=%d, interval=[%d,%d], tx_power=%d",
         param.announce_mode, param.announce_interval_min, param.announce_interval_max,
         g_adv_param.announce_tx_power);
    return 0;
}

static int adv_set_broadcast_data(void)
{
    inventory_info_t info = {0};
    data_get_inventory_info(&info);

    uint8_t packet_data[DATA_PACKET_LEN_BROADCAST] = {0};
    uint16_t packet_len = DATA_PACKET_LEN_BROADCAST;
    errcode_t ret = data_pack_broadcast(&info, packet_data, &packet_len);
    if (ret != 0) {
        LOGE("Pack broadcast data failed: 0x%08X", ret);
        return ret;
    }

    g_adv_data[0] = 0x01;
    g_adv_data[1] = 0x01;
    g_adv_data[2] = 0x01;

    g_adv_data[3] = 0x05;
    g_adv_data[4] = 0x04;
    g_adv_data[5] = 0x0B;
    g_adv_data[6] = 0x06;
    g_adv_data[7] = 0x09;
    g_adv_data[8] = 0x06;

    g_adv_data[9] = 0x03;
    g_adv_data[10] = 0x12;
    g_adv_data[11] = 0x09;

    uint16_t data_offset = 12;

    uint8_t device_name[] = "Spark_Tag";
    uint8_t name_len = strlen((char*)device_name);
    g_adv_data[data_offset++] = name_len + 1;
    g_adv_data[data_offset++] = 0x09;
    (void)memcpy_s(&g_adv_data[data_offset], name_len, device_name, name_len);
    data_offset += name_len;

    if (packet_len > 0 && packet_len <= (ADV_DATA_LEN_MAX - data_offset - 3)) {
        g_adv_data[data_offset++] = 0xFF;
        g_adv_data[data_offset++] = 0xFF;
        g_adv_data[data_offset++] = (uint8_t)packet_len;
        (void)memcpy_s(&g_adv_data[data_offset], packet_len, packet_data, packet_len);
        data_offset += packet_len;
    }

    uint16_t adv_data_len = data_offset;
    uint16_t adv_rsp_data_len = 0;

    char hex_str[128] = {0};
    uint16_t print_len = adv_data_len > 32 ? 32 : adv_data_len;
    common_hex_to_string(g_adv_data, print_len, hex_str, sizeof(hex_str));
    LOGD("Adv data(%d): %s%s", adv_data_len, hex_str, adv_data_len > 32 ? "..." : "");

    sle_announce_data_t announce_data = {0};
    announce_data.announce_data = g_adv_data;
    announce_data.announce_data_len = adv_data_len;
    announce_data.seek_rsp_data = g_adv_rsp_data;
    announce_data.seek_rsp_data_len = adv_rsp_data_len;

    ret = sle_set_announce_data(g_adv_param.announce_handle, &announce_data);
    if (ret != 0) {
        LOGE("Set announce data failed: 0x%08X", ret);
        return ret;
    }

    LOGI("Broadcast data set, adv_data_len=%d, adv_rsp_data_len=%d",
         adv_data_len, adv_rsp_data_len);
    return 0;
}

errcode_t adv_init(void)
{
    LOGI("Adv module initializing...");

    data_get_mac(g_local_mac);
    if (g_local_mac[0] == 0 && g_local_mac[1] == 0 &&
        g_local_mac[2] == 0 && g_local_mac[3] == 0 &&
        g_local_mac[4] == 0 && g_local_mac[5] == 0) {
        g_local_mac[0] = 0x11;
        g_local_mac[1] = 0x22;
        g_local_mac[2] = 0x33;
        g_local_mac[3] = 0x44;
        g_local_mac[4] = 0x55;
        g_local_mac[5] = 0x66;
        LOGW("Using default MAC: %02X:%02X:%02X:%02X:%02X:%02X",
             g_local_mac[0], g_local_mac[1], g_local_mac[2],
             g_local_mac[3], g_local_mac[4], g_local_mac[5]);
    }

    g_adv_state = ADV_STATE_CONFIGURED;
    LOGI("Adv module initialized");
    return 0;
}

errcode_t adv_start(uint32_t duration_ms)
{
    if (g_adv_state == ADV_STATE_RUNNING) {
        LOGW("Adv already running");
        return 0;
    }

    if (callback_get_sle_enable_status() != 1) {
        LOGE("SLE not enabled, cannot start adv");
        return ERR_COMMON_INVALID_PARAM;
    }

    errcode_t ret;

    ret = adv_set_default_param();
    if (ret != 0) {
        return ret;
    }

    ret = adv_set_broadcast_data();
    if (ret != 0) {
        return ret;
    }

    ret = sle_start_announce(g_adv_param.announce_handle, 0);
    if (ret != 0) {
        LOGE("Start announce failed: 0x%08X", ret);
        return ret;
    }

    g_adv_state = ADV_STATE_RUNNING;
    g_adv_enable = 1;

    LOGI("Adv started, duration=%ums", duration_ms);
    return 0;
}

errcode_t adv_stop(void)
{
    if (g_adv_state != ADV_STATE_RUNNING) {
        LOGW("Adv not running, nothing to stop");
        return 0;
    }

    errcode_t ret = sle_stop_announce(g_adv_param.announce_handle);
    if (ret != 0) {
        LOGE("Stop announce failed: 0x%08X", ret);
        return ret;
    }

    g_adv_state = ADV_STATE_STOPPED;
    g_adv_enable = 0;

    LOGI("Adv stopped");
    return 0;
}

errcode_t adv_restart(void)
{
    LOGI("Adv restarting...");
    errcode_t ret = adv_stop();
    if (ret != 0) {
        LOGW("Stop before restart failed: 0x%08X", ret);
    }

    common_msleep(ADV_RESTART_INTERVAL_MS);

    return adv_start(ADV_DEFAULT_DURATION_MS);
}

errcode_t adv_update_broadcast_data(const uint8_t *data, uint16_t len)
{
    if (data == NULL || len == 0 || len > ADV_DATA_LEN_MAX) {
        return ERR_COMMON_INVALID_PARAM;
    }

    (void)memcpy_s(g_adv_data, sizeof(g_adv_data), data, len);

    sle_announce_data_t announce_data = {0};
    announce_data.announce_data = g_adv_data;
    announce_data.announce_data_len = len;
    announce_data.seek_rsp_data = g_adv_rsp_data;
    announce_data.seek_rsp_data_len = 0;

    errcode_t ret = sle_set_announce_data(g_adv_param.announce_handle, &announce_data);
    if (ret != 0) {
        LOGE("Update broadcast data failed: 0x%08X", ret);
        return ret;
    }

    LOGI("Broadcast data updated, len=%d", len);
    return 0;
}

adv_state_t adv_get_state(void)
{
    return g_adv_state;
}

errcode_t adv_set_enable(uint8_t enable)
{
    g_adv_enable = enable ? 1 : 0;
    return 0;
}

uint8_t adv_is_enabled(void)
{
    return g_adv_enable;
}

errcode_t adv_get_local_mac(uint8_t *mac_out)
{
    if (mac_out == NULL) {
        return ERR_COMMON_NULL_PTR;
    }
    (void)memcpy_s(mac_out, BD_ADDR_LEN, g_local_mac, BD_ADDR_LEN);
    return 0;
}

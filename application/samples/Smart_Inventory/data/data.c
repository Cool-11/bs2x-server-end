/**
 *******************************************************************************
 * @file data.c
 * @brief Smart Inventory Tag - Data Processing Module Implementation
 *
 *  Description:
 *   本模块负责业务数据的打包、解析和管理。
 *   实现 MAC + 电池 + 货物ID 的数据封装和CRC校验。
 *
 * @version V1.0
 * @date 2024
 *******************************************************************************
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "data.h"
#include "common.h"

#define DATA_HEADER_VALUE    0xAA
#define DATA_FOOTER_VALUE    0x55

static inventory_info_t g_inventory_info = {
    .mac = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66},
    .battery = 100,
    .goods_id = 1,
    .timestamp = 0
};

static uint32_t g_mock_goods_id_counter = 0x12345678;

static uint8_t g_mock_battery_values[] = {95, 92, 88, 90, 87, 85, 83, 80, 78, 75};
static uint8_t g_mock_battery_index = 0;

errcode_t data_init(void)
{
    g_inventory_info.timestamp = common_get_tick_ms();
    g_mock_goods_id_counter = 0x12345678;
    g_mock_battery_index = 0;
    LOGI("Data module initialized");
    LOGI("  MAC: %02X:%02X:%02X:%02X:%02X:%02X",
         g_inventory_info.mac[0], g_inventory_info.mac[1],
         g_inventory_info.mac[2], g_inventory_info.mac[3],
         g_inventory_info.mac[4], g_inventory_info.mac[5]);
    LOGI("  Battery: %d%%", g_inventory_info.battery);
    LOGI("  GoodsID: 0x%08X", g_inventory_info.goods_id);
    return 0;
}

uint16_t data_calculate_crc16(const uint8_t *data, uint16_t len)
{
    uint16_t crc = CRC16_INIT;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ CRC16_POLYNOMIAL;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

errcode_t data_pack_broadcast(const inventory_info_t *info, uint8_t *out, uint16_t *out_len)
{
    if (info == NULL || out == NULL || out_len == NULL) {
        return ERR_COMMON_NULL_PTR;
    }

    if (*out_len < DATA_PACKET_LEN_BROADCAST) {
        *out_len = DATA_PACKET_LEN_BROADCAST;
        return ERR_COMMON_NO_MEMORY;
    }

    broadcast_data_t packet;
    (void)memcpy_s(packet.mac, sizeof(packet.mac), info->mac, 6);
    packet.battery = info->battery;
    packet.goods_id[0] = (info->goods_id >> 24) & 0xFF;
    packet.goods_id[1] = (info->goods_id >> 16) & 0xFF;
    packet.goods_id[2] = (info->goods_id >> 8) & 0xFF;
    packet.goods_id[3] = info->goods_id & 0xFF;

    uint16_t crc = data_calculate_crc16((const uint8_t *)&packet,
                                         sizeof(broadcast_data_t) - sizeof(uint16_t));

    uint8_t *p = out;
    (void)memcpy_s(p, 6, packet.mac, 6);
    p += 6;
    *p++ = packet.battery;
    (void)memcpy_s(p, 4, packet.goods_id, 4);
    p += 4;
    *p++ = (crc >> 8) & 0xFF;
    *p++ = crc & 0xFF;

    *out_len = DATA_PACKET_LEN_BROADCAST;
    return 0;
}

errcode_t data_pack_connected(const inventory_info_t *info, uint8_t *out, uint16_t *out_len)
{
    if (info == NULL || out == NULL || out_len == NULL) {
        return ERR_COMMON_NULL_PTR;
    }

    if (*out_len < DATA_PACKET_LEN_CONNECTED) {
        *out_len = DATA_PACKET_LEN_CONNECTED;
        return ERR_COMMON_NO_MEMORY;
    }

    connected_data_t packet;
    packet.header = DATA_HEADER_VALUE;
    (void)memcpy_s(packet.mac, sizeof(packet.mac), info->mac, 6);
    packet.battery = info->battery;
    packet.goods_id[0] = (info->goods_id >> 24) & 0xFF;
    packet.goods_id[1] = (info->goods_id >> 16) & 0xFF;
    packet.goods_id[2] = (info->goods_id >> 8) & 0xFF;
    packet.goods_id[3] = info->goods_id & 0xFF;

    uint16_t crc = data_calculate_crc16((const uint8_t *)&packet + 1,
                                         sizeof(connected_data_t) - 2 - sizeof(uint16_t));
    packet.crc = crc;
    packet.footer = DATA_FOOTER_VALUE;

    (void)memcpy_s(out, DATA_PACKET_LEN_CONNECTED, (const uint8_t *)&packet, sizeof(packet));
    *out_len = DATA_PACKET_LEN_CONNECTED;
    return 0;
}

errcode_t data_parse_broadcast(const uint8_t *in, uint16_t in_len, data_parse_result_t *result)
{
    if (in == NULL || result == NULL) {
        return ERR_COMMON_NULL_PTR;
    }

    (void)memset_s(result, sizeof(data_parse_result_t), 0, sizeof(data_parse_result_t));

    if (in_len < DATA_PACKET_LEN_BROADCAST) {
        LOGW("Broadcast data too short: %d < %d", in_len, DATA_PACKET_LEN_BROADCAST);
        return ERR_COMMON_INVALID_PARAM;
    }

    (void)memcpy_s(result->mac, 6, in + DATA_OFFSET_MAC, 6);
    result->battery = in[DATA_OFFSET_BATTERY];
    result->goods_id = ((uint32_t)in[DATA_OFFSET_GOODS_ID] << 24) |
                       ((uint32_t)in[DATA_OFFSET_GOODS_ID + 1] << 16) |
                       ((uint32_t)in[DATA_OFFSET_GOODS_ID + 2] << 8) |
                       ((uint32_t)in[DATA_OFFSET_GOODS_ID + 3]);

    uint16_t expected_crc = ((uint16_t)in[DATA_OFFSET_CRC] << 8) | in[DATA_OFFSET_CRC + 1];
    result->expected_crc = expected_crc;

    uint16_t calc_crc = data_calculate_crc16(in, DATA_OFFSET_CRC);
    result->actual_crc = calc_crc;

    result->is_valid = (expected_crc == calc_crc) ? 1 : 0;
    return 0;
}

errcode_t data_parse_connected(const uint8_t *in, uint16_t in_len, data_parse_result_t *result)
{
    if (in == NULL || result == NULL) {
        return ERR_COMMON_NULL_PTR;
    }

    (void)memset_s(result, sizeof(data_parse_result_t), 0, sizeof(data_parse_result_t));

    if (in_len < DATA_PACKET_LEN_CONNECTED) {
        LOGW("Connected data too short: %d < %d", in_len, DATA_PACKET_LEN_CONNECTED);
        return ERR_COMMON_INVALID_PARAM;
    }

    if (in[0] != DATA_HEADER_VALUE) {
        LOGW("Invalid header: 0x%02X != 0x%02X", in[0], DATA_HEADER_VALUE);
        return ERR_COMMON_INVALID_PARAM;
    }

    if (in[in_len - 1] != DATA_FOOTER_VALUE) {
        LOGW("Invalid footer: 0x%02X != 0x%02X", in[in_len - 1], DATA_FOOTER_VALUE);
        return ERR_COMMON_INVALID_PARAM;
    }

    (void)memcpy_s(result->mac, 6, in + 1, 6);
    result->battery = in[7];
    result->goods_id = ((uint32_t)in[8] << 24) |
                       ((uint32_t)in[9] << 16) |
                       ((uint32_t)in[10] << 8) |
                       ((uint32_t)in[11]);

    uint16_t expected_crc = ((uint16_t)in[12] << 8) | in[13];
    result->expected_crc = expected_crc;

    uint16_t calc_crc = data_calculate_crc16(in + 1, 12);
    result->actual_crc = calc_crc;

    result->is_valid = (expected_crc == calc_crc) ? 1 : 0;
    return 0;
}

errcode_t data_get_inventory_info(inventory_info_t *info)
{
    if (info == NULL) {
        return ERR_COMMON_NULL_PTR;
    }

    g_inventory_info.timestamp = common_get_tick_ms();
    (void)memcpy_s(info, sizeof(inventory_info_t), &g_inventory_info, sizeof(inventory_info_t));
    return 0;
}

errcode_t data_get_mac(uint8_t *mac)
{
    if (mac == NULL) {
        return ERR_COMMON_NULL_PTR;
    }
    (void)memcpy_s(mac, 6, g_inventory_info.mac, 6);
    return 0;
}

errcode_t data_get_battery(uint8_t *battery)
{
    if (battery == NULL) {
        return ERR_COMMON_NULL_PTR;
    }
    *battery = g_inventory_info.battery;
    return 0;
}

errcode_t data_get_goods_id(uint32_t *goods_id)
{
    if (goods_id == NULL) {
        return ERR_COMMON_NULL_PTR;
    }
    *goods_id = g_inventory_info.goods_id;
    return 0;
}

errcode_t data_set_goods_id(uint32_t goods_id)
{
    if (goods_id < GOODS_ID_MIN || goods_id > GOODS_ID_MAX) {
        return ERR_COMMON_INVALID_PARAM;
    }
    g_inventory_info.goods_id = goods_id;
    LOGI("Goods ID updated: 0x%08X", goods_id);
    return 0;
}

errcode_t data_set_battery(uint8_t battery)
{
    if (battery > BATTERY_LEVEL_MAX) {
        return ERR_COMMON_INVALID_PARAM;
    }
    g_inventory_info.battery = battery;
    LOGI("Battery level updated: %d%%", battery);
    return 0;
}

uint8_t data_generate_mock_battery(void)
{
    g_mock_battery_index = (g_mock_battery_index + 1) % ARRAY_SIZE(g_mock_battery_values);
    uint8_t battery = g_mock_battery_values[g_mock_battery_index];
    g_inventory_info.battery = battery;
    return battery;
}

uint32_t data_generate_mock_goods_id(void)
{
    g_mock_goods_id_counter++;
    if (g_mock_goods_id_counter > GOODS_ID_MAX) {
        g_mock_goods_id_counter = GOODS_ID_MIN + 1;
    }
    g_inventory_info.goods_id = g_mock_goods_id_counter;
    return g_mock_goods_id_counter;
}

uint16_t data_get_packet_len(data_type_t type)
{
    switch (type) {
        case DATA_TYPE_BROADCAST:
            return DATA_PACKET_LEN_BROADCAST;
        case DATA_TYPE_NOTIFY:
        case DATA_TYPE_INDICATION:
            return DATA_PACKET_LEN_CONNECTED;
        default:
            return 0;
    }
}

uint8_t data_verify_crc16(const uint8_t *data, uint16_t len)
{
    if (data == NULL || len < sizeof(uint16_t)) {
        return 0;
    }
    uint16_t expected = ((uint16_t)data[len - 2] << 8) | data[len - 1];
    uint16_t calculated = data_calculate_crc16(data, len - sizeof(uint16_t));
    return (expected == calculated) ? 1 : 0;
}

errcode_t data_mac_to_string(const uint8_t *mac, char *str_out)
{
    if (mac == NULL || str_out == NULL) {
        return ERR_COMMON_NULL_PTR;
    }
    (void)snprintf(str_out, 14, "%02X:%02X:%02X:%02X:%02X:%02X",
                   mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return 0;
}

errcode_t data_get_mock_goods_data(adv_goods_data_t *out)
{
    if (out == NULL) {
        return ERR_COMMON_NULL_PTR;
    }
    (void)memcpy_s(out->mac, 6, g_inventory_info.mac, 6);
    out->battery = g_inventory_info.battery;
    out->goods_id[0] = (g_inventory_info.goods_id >> 24) & 0xFF;
    out->goods_id[1] = (g_inventory_info.goods_id >> 16) & 0xFF;
    out->goods_id[2] = (g_inventory_info.goods_id >> 8) & 0xFF;
    out->goods_id[3] = g_inventory_info.goods_id & 0xFF;
    out->status = STATUS_PRESENT;
    return 0;
}

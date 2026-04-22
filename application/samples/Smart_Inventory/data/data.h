/**
 *******************************************************************************
 * @file data.h
 * @brief Smart Inventory Tag - Data Processing Module
 *
 *  Description:
 *   本模块负责业务数据的打包、解析和管理。
 *   负责：
 *     1. 盘点数据结构定义（MAC + 电池 + 货物ID）
 *     2. 数据打包函数（将业务数据组装成协议报文）
 *     3. 数据解析函数（从接收数据中提取有用信息）
 *     4. 数据校验（CRC校验、和校验等）
 *     5. Tag模拟数据生成（电池电量、货物ID）
 *
 *  数据包格式说明:
 *   广播数据格式（Adv Data）:
 *   | MAC(6B) | Battery(1B) | GoodsID(4B) | CRC(2B) |
 *   | 0-5     | 6           | 7-10        | 11-12   |
 *
 *  连接数据格式（Notify/Write）:
 *   | Header(1B) | MAC(6B) | Battery(1B) | GoodsID(4B) | CRC(2B) | Footer(1B) |
 *   | 0          | 1-6     | 7           | 8-11        | 12-13   | 14         |
 *
 *  设计特点:
 *   - 完全独立于通信层，不依赖任何SLE/PM API
 *   - 数据结构与协议格式分离，便于扩展
 *   - 提供模拟数据生成，便于测试
 *
 *  与其他模块关系:
 *   - 被 main/callback 模块调用进行数据打包
 *   - 调用 common 模块的内存操作函数
 *
 * @version V1.0
 * @date 2024
 *******************************************************************************
 */
#ifndef _SMART_INVENTORY_DATA_H_
#define _SMART_INVENTORY_DATA_H_

#include <stdint.h>
#include "errcode.h"

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
 * Macros & Constants
 *============================================================================*/

/* 数据包字段偏移量 */
#define DATA_OFFSET_MAC       0
#define DATA_OFFSET_BATTERY   6
#define DATA_OFFSET_GOODS_ID  7
#define DATA_OFFSET_CRC       11

/* 数据包总长度 */
#define DATA_PACKET_LEN_BROADCAST   13   /**< 广播数据包长度 [MAC(6)+Battery(1)+GoodsID(4)+CRC(2)] */
#define DATA_PACKET_LEN_CONNECTED    15   /**< 连接数据包长度 [Header(1)+MAC(6)+Battery(1)+GoodsID(4)+CRC(2)+Footer(1)] */
#define DATA_PACKET_LEN_MOCK          12   /**< Mock数据包长度 [MAC(6)+Battery(1)+GoodsID(4)+Status(1)] */

/* 电池电量范围 */
#define BATTERY_LEVEL_MIN     0
#define BATTERY_LEVEL_MAX     100
#define BATTERY_LEVEL_INVALID 0xFF

/* 货物ID范围 */
#define GOODS_ID_MIN          0
#define GOODS_ID_MAX          0xFFFFFFFF

/* CRC算法参数 */
#define CRC16_POLYNOMIAL      0x8005
#define CRC16_INIT            0xFFFF

/* 状态定义 */
#define STATUS_PRESENT        0x01  /**< 在场状态 */
#define STATUS_AWAY           0x00  /**< 离开状态 */

/*==============================================================================
 * Type Definitions
 *============================================================================*/

/**
 * @brief 盘点数据类型
 */
typedef enum {
    DATA_TYPE_BROADCAST = 0,    /**< 广播数据类型 */
    DATA_TYPE_NOTIFY,            /**< Notify数据类型 */
    DATA_TYPE_INDICATION,       /**< Indication数据类型 */
    DATA_TYPE_MAX
} data_type_t;

/**
 * @brief 广播数据包结构体
 */
typedef struct {
    uint8_t  mac[6];            /**< MAC地址 */
    uint8_t  battery;           /**< 电池电量 0-100% */
    uint8_t  goods_id[4];       /**< 货物ID (大端序) */
} __attribute__((packed)) broadcast_data_t;

/**
 * @brief 连接数据包头结构体
 */
typedef struct {
    uint8_t  header;            /**< 数据包头 0xAA */
    uint8_t  mac[6];            /**< MAC地址 */
    uint8_t  battery;           /**< 电池电量 */
    uint8_t  goods_id[4];       /**< 货物ID */
    uint16_t crc;               /**< CRC16校验 */
    uint8_t  footer;            /**< 数据包尾 0x55 */
} __attribute__((packed)) connected_data_t;

/**
 * @brief 原始数据包缓冲区
 */
typedef struct {
    uint8_t  *buffer;            /**< 数据缓冲区 */
    uint16_t  length;           /**< 数据长度 */
    uint16_t  capacity;         /**< 缓冲区容量 */
} data_buffer_t;

/**
 * @brief 盘点业务数据结构体 (用于Notify发送)
 * @details 格式: MAC(6B) + Battery(1B) + GoodsID(4B) + Status(1B) = 12字节
 */
typedef struct {
    uint8_t  mac[6];            /**< MAC地址 */
    uint8_t  battery;           /**< 电池电量 0-100 */
    uint8_t  goods_id[4];       /**< 货物ID (大端序) */
    uint8_t  status;            /**< 状态: 0x01=在场, 0x00=离开 */
} __attribute__((packed)) adv_goods_data_t;

/**
 * @brief Tag业务数据信息
 */
typedef struct {
    uint8_t  mac[6];            /**< MAC地址 */
    uint8_t  battery;           /**< 电池电量 0-100% */
    uint32_t goods_id;          /**< 货物ID */
    uint32_t timestamp;         /**< 数据生成时间戳 */
} inventory_info_t;

/**
 * @brief 数据包解析结果
 */
typedef struct {
    uint8_t  is_valid;         /**< 数据是否有效 */
    uint8_t  mac[6];            /**< 解析出的MAC */
    uint8_t  battery;           /**< 解析出的电量 */
    uint32_t goods_id;          /**< 解析出的货物ID */
    uint16_t expected_crc;       /**< 期望的CRC值 */
    uint16_t actual_crc;         /**< 实际计算的CRC值 */
} data_parse_result_t;

/*==============================================================================
 * External Function Declarations
 *============================================================================*/

/**
 * @brief 初始化数据模块
 * @details 读取MAC地址、初始化随机数种子等
 * @return errcode_t 错误码
 */
errcode_t data_init(void);

/**
 * @brief 打包广播数据
 * @details 将业务数据打包成广播格式
 * @param[in] info 业务数据信息
 * @param[out] out 输出缓冲区（至少DATA_PACKET_LEN_BROADCAST字节）
 * @param[out] out_len 实际输出长度
 * @return errcode_t 错误码
 */
errcode_t data_pack_broadcast(const inventory_info_t *info, uint8_t *out, uint16_t *out_len);

/**
 * @brief 打包连接数据
 * @details 将业务数据打包成连接传输格式
 * @param[in] info 业务数据信息
 * @param[out] out 输出缓冲区（至少DATA_PACKET_LEN_CONNECTED字节）
 * @param[out] out_len 实际输出长度
 * @return errcode_t 错误码
 */
errcode_t data_pack_connected(const inventory_info_t *info, uint8_t *out, uint16_t *out_len);

/**
 * @brief 解析广播数据
 * @param[in] in 输入数据
 * @param[in] in_len 输入长度
 * @param[out] result 解析结果
 * @return errcode_t 错误码
 */
errcode_t data_parse_broadcast(const uint8_t *in, uint16_t in_len, data_parse_result_t *result);

/**
 * @brief 解析连接数据
 * @param[in] in 输入数据
 * @param[in] in_len 输入长度
 * @param[out] result 解析结果
 * @return errcode_t 错误码
 */
errcode_t data_parse_connected(const uint8_t *in, uint16_t in_len, data_parse_result_t *result);

/**
 * @brief 计算CRC16校验
 * @param[in] data 输入数据
 * @param[in] len 数据长度
 * @return uint16_t CRC16值
 */
uint16_t data_calculate_crc16(const uint8_t *data, uint16_t len);

/**
 * @brief 获取Tag业务数据
 * @param[out] info 业务数据信息
 * @return errcode_t 错误码
 */
errcode_t data_get_inventory_info(inventory_info_t *info);

/**
 * @brief 获取MAC地址
 * @param[out] mac MAC地址（6字节）
 * @return errcode_t 错误码
 */
errcode_t data_get_mac(uint8_t *mac);

/**
 * @brief 获取电池电量
 * @param[out] battery 电池电量（0-100）
 * @return errcode_t 错误码
 */
errcode_t data_get_battery(uint8_t *battery);

/**
 * @brief 获取货物ID
 * @param[out] goods_id 货物ID
 * @return errcode_t 错误码
 */
errcode_t data_get_goods_id(uint32_t *goods_id);

/**
 * @brief 更新货物ID
 * @param goods_id 新的货物ID
 * @return errcode_t 错误码
 */
errcode_t data_set_goods_id(uint32_t goods_id);

/**
 * @brief 更新电池电量
 * @param battery 新的电池电量
 * @return errcode_t 错误码
 */
errcode_t data_set_battery(uint8_t battery);

/**
 * @brief 模拟生成电池电量（用于测试）
 * @return uint8_t 模拟的电池电量值
 */
uint8_t data_generate_mock_battery(void);

/**
 * @brief 模拟生成货物ID（用于测试）
 * @return uint32_t 模拟的货物ID
 */
uint32_t data_generate_mock_goods_id(void);

/**
 * @brief 获取数据包长度
 * @param type 数据类型
 * @return uint16_t 数据包长度
 */
uint16_t data_get_packet_len(data_type_t type);

/*==============================================================================
 * Helper Functions
 *============================================================================*/

/**
 * @brief 检查CRC校验是否通过
 * @param data 数据
 * @param len 长度（包含CRC）
 * @return uint8_t 1-校验通过，0-校验失败
 */
uint8_t data_verify_crc16(const uint8_t *data, uint16_t len);

/**
 * @brief 将MAC地址转换为字符串
 * @param mac MAC地址
 * @param str_out 输出字符串（至少13字节）
 * @return errcode_t 错误码
 */
errcode_t data_mac_to_string(const uint8_t *mac, char *str_out);

/**
 * @brief 获取Mock业务数据（用于连接建立时自动推送）
 * @details 格式: MAC(6B) + Battery(1B) + GoodsID(4B) + Status(1B) = 12字节
 * @param[out] out Mock数据输出缓冲区（至少12字节）
 * @return errcode_t 错误码
 */
errcode_t data_get_mock_goods_data(adv_goods_data_t *out);

#ifdef __cplusplus
}
#endif

#endif /* _SMART_INVENTORY_DATA_H_ */

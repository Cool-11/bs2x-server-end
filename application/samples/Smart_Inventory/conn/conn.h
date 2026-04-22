/**
 *******************************************************************************
 * @file conn.h
 * @brief Smart Inventory Tag - Connection Management Module
 *
 *  Description:
 *   本模块负责星闪SLE连接管理(Connection Manager)和GATT服务创建。
 *   负责：
 *     1. SLE连接建立/断开管理
 *     2. GATT服务注册（主服务、特征值、描述符）
 *     3. 连接参数更新
 *     4. Notify/Indication数据发送
 *     5. 连接状态维护
 *
 *  设计特点:
 *   - 独立于广播模块，可单独编译测试
 *   - 连接态下通过Notify方式精准上报数据
 *   - 支持连接参数动态调整（低功耗优化）
 *
 *  与其他模块关系:
 *   - 被 main 模块调用
 *   - 连接状态变化通知 callback 模块
 *
 * @version V1.0
 * @date 2024
 *******************************************************************************
 */
#ifndef _SMART_INVENTORY_CONN_H_
#define _SMART_INVENTORY_CONN_H_

#include <stdint.h>
#include "errcode.h"

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
 * Macros & Constants
 *============================================================================*/

/* GATT服务UUID定义 */
#define GATT_SERVICE_UUID_INVENTORY     0x2222    /**< 盘点服务UUID */
#define GATT_CHAR_UUID_INVENTORY_DATA   0x2323    /**< 盘点数据特征值UUID */
#define GATT_CHAR_UUID_COMMAND          0x2424    /**< 命令特征值UUID（预留）*/

/* 连接参数 */
#define CONN_INTERVAL_MIN                0x001E    /**< 最小连接间隔 7.5ms */
#define CONN_INTERVAL_MAX                0x001E    /**< 最大连接间隔 7.5ms */
#define CONN_LATENCY                    0         /**< 连接延迟（无延迟）*/
#define CONN_SUPERVISION_TIMEOUT        0x00C8    /**< 超时时间 2000ms */

/* MTU大小 */
#define CONN_MTU_SIZE_DEFAULT           512

/*==============================================================================
 * Type Definitions
 *============================================================================*/

/**
 * @brief 连接状态枚举
 */
typedef enum {
    CONN_STATE_DISCONNECTED = 0,  /**< 断开连接 */
    CONN_STATE_CONNECTING,         /**< 连接中 */
    CONN_STATE_CONNECTED,          /**< 已连接 */
    CONN_STATE_DISCONNECTING        /**< 断开中 */
} conn_state_t;

/**
 * @brief 连接信息结构体
 */
typedef struct {
    uint16_t    conn_id;              /**< 连接ID */
    uint8_t     peer_addr[6];        /**< 对端MAC地址 */
    conn_state_t state;              /**< 连接状态 */
    uint16_t    mtu_size;            /**< MTU大小 */
    uint32_t    connection_handle;   /**< 连接句柄 */
} conn_info_t;

/*==============================================================================
 * External Function Declarations
 *============================================================================*/

/**
 * @brief 初始化连接管理模块
 * @details 注册GATT服务、注册连接回调
 * @return errcode_t 错误码
 */
errcode_t conn_init(void);

/**
 * @brief 注册GATT服务器
 * @details 创建Inventory服务，添加特征值和描述符
 * @return errcode_t 错误码
 */
errcode_t conn_register_server(void);

/**
 * @brief 断开当前连接
 * @return errcode_t 错误码
 */
errcode_t conn_disconnect(void);

/**
 * @brief 发送Notify数据到主机
 * @param data 数据指针
 * @param len 数据长度
 * @return errcode_t 错误码
 */
errcode_t conn_send_notify(const uint8_t *data, uint16_t len);

/**
 * @brief 发送Indication数据到主机
 * @param data 数据指针
 * @param len 数据长度
 * @return errcode_t 错误码
 */
errcode_t conn_send_indication(const uint8_t *data, uint16_t len);

/**
 * @brief 更新连接参数
 * @param min_interval 最小连接间隔
 * @param max_interval 最大连接间隔
 * @param latency 连接延迟
 * @param timeout 超时时间
 * @return errcode_t 错误码
 */
errcode_t conn_update_param(uint16_t min_interval, uint16_t max_interval,
                           uint16_t latency, uint16_t timeout);

/**
 * @brief 获取当前连接状态
 * @return conn_state_t 连接状态
 */
conn_state_t conn_get_state(void);

/**
 * @brief 检查是否已连接
 * @return uint8_t 1-已连接，0-未连接
 */
uint8_t conn_is_connected(void);

/**
 * @brief 获取连接信息
 * @return conn_info_t* 连接信息指针
 */
conn_info_t *conn_get_info(void);

/**
 * @brief 获取当前连接ID
 * @return uint16_t 连接ID
 */
uint16_t conn_get_conn_id(void);

/**
 * @brief 设置连接建立回调
 * @param callback 连接建立回调函数
 * @return void
 */
void conn_set_connect_callback(void (*callback)(void));

/**
 * @brief 设置连接断开回调
 * @param callback 连接断开回调函数
 * @return void
 */
void conn_set_disconnect_callback(void (*callback)(void));

#ifdef __cplusplus
}
#endif

#endif /* _SMART_INVENTORY_CONN_H_ */

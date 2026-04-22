/**
 *******************************************************************************
 * @file callback.h
 * @brief Smart Inventory Tag - Callback Functions Module
 *
 *  Description:
 *   本模块负责定义和注册所有SLE和PM相关的回调函数。
 *   负责：
 *     1. SLE设备管理回调（上电/使能/去使能）
 *     2. SLE广播回调（广播使能/停止/终止）
 *     3. SLE连接回调（连接状态/配对完成/参数更新）
 *     4. GATT服务回调（读写请求/通知确认）
 *     5. PM电源管理回调（休眠/唤醒事件）
 *
 *  设计特点:
 *   - 完全解耦设计：每个回调函数独立，互不影响
 *   - 回调注册机制：通过函数指针注册，灵活可配置
 *   - 中间层设计：不直接处理业务逻辑，仅做路由分发
 *
 *  与其他模块关系:
 *   - 被 main 模块初始化时调用
 *   - 回调中调用 adv/conn/pm/data 模块的接口
 *   - 通过回调钩子通知 main 模块状态变化
 *
 *  使用说明:
 *   回调函数体本身不做复杂处理，仅做状态更新和事件分发，
 *   复杂业务逻辑在 main 模块的任务中处理。
 *
 * @version V1.0
 * @date 2024
 *******************************************************************************
 */
#ifndef _SMART_INVENTORY_CALLBACK_H_
#define _SMART_INVENTORY_CALLBACK_H_

#include <stdint.h>
#include "errcode.h"
#include "sle_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
 * Callback Type Definitions (函数指针类型定义)
 *============================================================================*/

/**
 * @brief SLE设备上电回调类型
 * @param status 上电状态 @ref sle_power_on_status_t
 */
typedef void (*callback_sle_power_on_t)(uint8_t status);

/**
 * @brief SLE使能回调类型
 * @param status 使能状态 @ref sle_enable_disable_status_t
 */
typedef void (*callback_sle_enable_t)(uint8_t status);

/**
 * @brief SLE去使能回调类型
 * @param status 去使能状态
 */
typedef void (*callback_sle_disable_t)(uint8_t status);

/**
 * @brief 广播使能回调类型
 * @param announce_id 广播ID
 * @param status 状态码
 */
typedef void (*callback_adv_enable_t)(uint32_t announce_id, errcode_t status);

/**
 * @brief 广播停止回调类型
 * @param announce_id 广播ID
 * @param status 状态码
 */
typedef void (*callback_adv_disable_t)(uint32_t announce_id, errcode_t status);

/**
 * @brief 广播终止回调类型
 * @param announce_id 广播ID
 */
typedef void (*callback_adv_terminal_t)(uint32_t announce_id);

/**
 * @brief 连接状态变化回调类型
 * @param conn_id 连接ID
 * @param addr 对端地址
 * @param conn_state 连接状态 @ref sle_acb_state_t
 * @param pair_state 配对状态 @ref sle_pair_state_t
 * @param disc_reason 断开原因 @ref sle_disc_reason_t
 */
typedef void (*callback_conn_state_t)(uint16_t conn_id, const sle_addr_t *addr,
    sle_acb_state_t conn_state, sle_pair_state_t pair_state, sle_disc_reason_t disc_reason);

/**
 * @brief 配对完成回调类型
 * @param conn_id 连接ID
 * @param addr 对端地址
 * @param status 状态码
 */
typedef void (*callback_pair_complete_t)(uint16_t conn_id, const sle_addr_t *addr, errcode_t status);

/**
 * @brief 连接参数更新完成回调类型
 * @param conn_id 连接ID
 * @param status 状态码
 * @param param 更新后的参数
 */
typedef void (*callback_conn_update_t)(uint16_t conn_id, errcode_t status,
    const sle_connection_param_update_evt_t *param);

/**
 * @brief PHY更新完成回调类型
 * @param conn_id 连接ID
 * @param status 状态码
 * @param param PHY参数
 */
typedef void (*callback_phy_update_t)(uint16_t conn_id, errcode_t status, const sle_set_phy_t *param);

/**
 * @brief GATT读请求回调类型
 * @param server_id 服务器ID
 * @param conn_id 连接ID
 * @param read_cb_para 读请求参数
 * @param status 状态码
 */
typedef void (*callback_gatt_read_t)(uint8_t server_id, uint16_t conn_id,
    const ssaps_req_read_cb_t *read_cb_para, errcode_t status);

/**
 * @brief GATT写请求回调类型
 * @param server_id 服务器ID
 * @param conn_id 连接ID
 * @param write_cb_para 写请求参数
 * @param status 状态码
 */
typedef void (*callback_gatt_write_t)(uint8_t server_id, uint16_t conn_id,
    const ssaps_req_write_cb_t *write_cb_para, errcode_t status);

/**
 * @brief MTU交换完成回调类型
 * @param server_id 服务器ID
 * @param conn_id 连接ID
 * @param mtu_size MTU大小
 * @param status 状态码
 */
typedef void (*callback_mtu_changed_t)(uint8_t server_id, uint16_t conn_id,
    const ssap_exchange_info_t *mtu_size, errcode_t status);

/**
 * @brief Notify发送确认回调类型
 * @param server_id 服务器ID
 * @param conn_id 连接ID
 * @param handle 特征值句柄
 * @param status 状态码
 */
typedef void (*callback_notify_confirm_t)(uint8_t server_id, uint16_t conn_id,
    uint16_t handle, errcode_t status);

/*==============================================================================
 * External Function Declarations
 *============================================================================*/

/**
 * @brief 注册所有SLE回调函数
 * @details 初始化回调函数指针表，注册到SLE协议栈
 * @return errcode_t 错误码
 */
errcode_t callback_register_sle_dev_manager(void);

/**
 * @brief 注册所有广播回调函数
 * @return errcode_t 错误码
 */
errcode_t callback_register_sle_announce(void);

/**
 * @brief 注册所有连接回调函数
 * @return errcode_t 错误码
 */
errcode_t callback_register_sle_connection(void);

/**
 * @brief 注册所有GATT服务回调函数
 * @param read_cb 读请求回调
 * @param write_cb 写请求回调
 * @return errcode_t 错误码
 */
errcode_t callback_register_sle_gatt_service(
    callback_gatt_read_t read_cb,
    callback_gatt_write_t write_cb);

/**
 * @brief 注册所有回调（一次性注册所有类型）
 * @return errcode_t 错误码
 */
errcode_t callback_register_all(void);

/**
 * @brief 获取SLE使能状态
 * @return uint8_t 1-已使能，0-未使能
 */
uint8_t callback_get_sle_enable_status(void);

/*==============================================================================
 * Direct Callback Handler Declarations (直接回调处理函数声明)
 *============================================================================*/

/* 设备管理回调 - 底层协议栈调用 */
void callback_sle_power_on_handler(uint8_t status);
void callback_sle_enable_handler(uint8_t status);
void callback_sle_disable_handler(uint8_t status);

/* 广播回调 - 底层协议栈调用 */
void callback_adv_enable_handler(uint32_t announce_id, errcode_t status);
void callback_adv_disable_handler(uint32_t announce_id, errcode_t status);
void callback_adv_terminal_handler(uint32_t announce_id);
void callback_seek_result_handler(sle_seek_result_info_t *seek_result);

/* 连接回调 - 底层协议栈调用 */
void callback_conn_state_changed_handler(uint16_t conn_id, const sle_addr_t *addr,
    sle_acb_state_t conn_state, sle_pair_state_t pair_state, sle_disc_reason_t disc_reason);
void callback_pair_complete_handler(uint16_t conn_id, const sle_addr_t *addr, errcode_t status);
void callback_conn_update_complete_handler(uint16_t conn_id, errcode_t status,
    const sle_connection_param_update_evt_t *param);
void callback_phy_update_complete_handler(uint16_t conn_id, errcode_t status,
    const sle_set_phy_t *param);

/* GATT服务回调 - 底层协议栈调用 */
void callback_gatt_read_request_handler(uint8_t server_id, uint16_t conn_id,
    const ssaps_req_read_cb_t *read_cb_para, errcode_t status);
void callback_gatt_write_request_handler(uint8_t server_id, uint16_t conn_id,
    const ssaps_req_write_cb_t *write_cb_para, errcode_t status);
void callback_gatt_mtu_changed_handler(uint8_t server_id, uint16_t conn_id,
    const ssap_exchange_info_t *mtu_size, errcode_t status);
void callback_gatt_notify_confirm_handler(uint8_t server_id, uint16_t conn_id,
    uint16_t handle, errcode_t status);

#ifdef __cplusplus
}
#endif

#endif /* _SMART_INVENTORY_CALLBACK_H_ */

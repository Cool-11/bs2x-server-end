/**
 *******************************************************************************
 * @file adv.h
 * @brief Smart Inventory Tag - Broadcast Configuration Module
 *
 *  Description:
 *   本模块负责星闪SLE广播(Advertise)相关配置和管理。
 *   负责：
 *     1. SLE广播参数配置（广播间隔、功率、模式等）
 *     2. 广播数据填充（将MAC+电量+货物ID填入Adv Data）
 *     3. 广播启动/停止控制
 *     4. 广播回调注册
 *
 *  设计特点:
 *   - 完全独立于连接管理模块，可单独编译测试
 *   - 广播数据传输使用Adv Data方式，实现极速无连接通信
 *   - 支持可连接可扫描模式，兼容阶段二需求
 *
 *  与其他模块关系:
 *   - 被 main 模块调用
 *   - 回调函数注册到 callback 模块
 *
 * @version V1.0
 * @date 2024
 *******************************************************************************
 */
#ifndef _SMART_INVENTORY_ADV_H_
#define _SMART_INVENTORY_ADV_H_

#include <stdint.h>
#include "errcode.h"
#include "sle_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
 * Macros & Constants
 *============================================================================*/

/* 广播ID定义 */
#define ADV_HANDLE_DEFAULT          1

/* 广播间隔定义 (单位: 125us) */
#define ADV_INTERVAL_MIN_DEFAULT     0xC8    /* 25ms */
#define ADV_INTERVAL_MAX_DEFAULT     0xC8    /* 25ms */

/* 连接参数 (单位: 125us) */
#define ADV_CONN_INTV_MIN_DEFAULT    0x64    /* 12.5ms */
#define ADV_CONN_INTV_MAX_DEFAULT    0x64    /* 12.5ms */
#define ADV_CONN_SUPERVISION_TIMEOUT  0x1F4   /* 5000ms */
#define ADV_CONN_MAX_LATENCY         0x1F3   /* 4990ms */

/* 广播发射功率 (dBm) */
#define ADV_TX_POWER_DEFAULT         6

/* 最大广播数据长度 */
#define ADV_DATA_LEN_MAX             251

/* 广播持续时间默认值 (ms) */
#define ADV_DEFAULT_DURATION_MS       5000

/*==============================================================================
 * Type Definitions
 *============================================================================*/

/**
 * @brief 广播参数配置结构体
 */
typedef struct {
    uint8_t  announce_handle;          /**< 广播句柄 */
    uint8_t  announce_mode;            /**< 广播模式 @ref sle_announce_mode_t */
    uint8_t  announce_gt_role;         /**< G/T角色协商 @ref sle_announce_gt_role_t */
    uint8_t  announce_level;           /**< 发现等级 @ref sle_announce_level_t */
    uint32_t announce_interval_min;    /**< 最小广播间隔 */
    uint32_t announce_interval_max;    /**< 最大广播间隔 */
    int8_t   announce_tx_power;       /**< 发射功率 */
    uint16_t conn_interval_min;        /**< 最小连接间隔 */
    uint16_t conn_interval_max;        /**< 最大连接间隔 */
    uint16_t conn_max_latency;         /**< 最大连接延迟 */
    uint16_t conn_supervision_timeout; /**< 连接超时时间 */
} adv_param_t;

/**
 * @brief 广播数据状态
 */
typedef enum {
    ADV_STATE_IDLE = 0,      /**< 空闲状态 */
    ADV_STATE_CONFIGURED,    /**< 已配置 */
    ADV_STATE_RUNNING,       /**< 广播运行中 */
    ADV_STATE_STOPPED        /**< 已停止 */
} adv_state_t;

/*==============================================================================
 * External Function Declarations
 *============================================================================*/

/**
 * @brief 初始化广播模块
 * @details 配置广播参数、注册回调、设置本地地址
 * @return errcode_t 错误码
 */
errcode_t adv_init(void);

/**
 * @brief 启动广播
 * @param duration_ms 广播持续时间(ms)，0表示持续广播直到手动停止
 * @return errcode_t 错误码
 */
errcode_t adv_start(uint32_t duration_ms);

/**
 * @brief 停止广播
 * @return errcode_t 错误码
 */
errcode_t adv_stop(void);

/**
 * @brief 重启广播
 * @return errcode_t 错误码
 */
errcode_t adv_restart(void);

/**
 * @brief 更新广播数据（动态更新Adv Data中的业务数据）
 * @param data 数据指针
 * @param len 数据长度
 * @return errcode_t 错误码
 */
errcode_t adv_update_broadcast_data(const uint8_t *data, uint16_t len);

/**
 * @brief 获取广播状态
 * @return adv_state_t 当前广播状态
 */
adv_state_t adv_get_state(void);

/**
 * @brief 设置广播使能状态
 * @param enable true-使能，false-禁用
 * @return errcode_t 错误码
 */
errcode_t adv_set_enable(uint8_t enable);

/**
 * @brief 获取广播使能状态
 * @return uint8_t 1-已使能，0-未使能
 */
uint8_t adv_is_enabled(void);

/**
 * @brief 获取本地MAC地址
 * @param mac_out 输出缓冲区(至少6字节)
 * @return errcode_t 错误码
 */
errcode_t adv_get_local_mac(uint8_t *mac_out);

#ifdef __cplusplus
}
#endif

#endif /* _SMART_INVENTORY_ADV_H_ */

/**
 *******************************************************************************
 * @file main.h
 * @brief Smart Inventory Tag - Main Entry and State Machine Module
 *
 *  Description:
 *   本模块是星闪无感盘点标签(Smart Inventory Tag)的主入口和状态机控制中心。
 *   负责：
 *     1. 系统初始化流程控制
 *     2. Tag状态机管理（Init -> Adv -> Connected -> Sleep）
 *     3. 业务任务创建和调度
 *     4. 各模块初始化顺序协调
 *
 *  状态机说明:
 *   - STATE_INIT      : 系统初始化状态
 *   - STATE_ADV       : 广播阶段（阶段一：无连接极速广播）
 *   - STATE_CONNECTED : 连接阶段（阶段二：定向连接与重传）
 *   - STATE_SLEEP     : 深度休眠阶段（阶段三）
 *   - STATE_MESH_RELAY: Mesh转发阶段（阶段四，预留功能）
 *
 *  依赖关系:
 *   本模块为顶层调度模块，依赖 adv/conn/callback/pm/data/common 等所有子模块
 *
 * @version V1.0
 * @date 2024
 *******************************************************************************
 */
#ifndef _SMART_INVENTORY_MAIN_H_
#define _SMART_INVENTORY_MAIN_H_

#include <stdint.h>
#include "errcode.h"

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
 * Macros & Constants
 *============================================================================*/

/* Tag名称定义 */
#define TAG_INVENTORY "INVENTORY_TAG"

/* 广播持续时间定义 (ms) */
#define ADV_BROADCAST_DURATION_MS   5000    /* 阶段一：广播5秒 */
#define ADV_RESTART_INTERVAL_MS     100     /* 广播重启间隔 */

/* 连接超时时间 (ms) */
#define CONNECTION_TIMEOUT_MS        10000   /* 阶段二：连接超时10秒 */

/* Mesh转发超时 (ms) - 预留 */
#define MESH_RELAY_TIMEOUT_MS        5000

/*==============================================================================
 * Type Definitions
 *============================================================================*/

/**
 * @brief Tag工作状态枚举
 */
typedef enum {
    STATE_INIT = 0,        /**< 系统初始化状态 */
    STATE_ADV,              /**< 广播阶段（无连接极速广播）*/
    STATE_CONNECTED,        /**< 连接阶段（定向连接与重传）*/
    STATE_SLEEP,            /**< 深度休眠阶段 */
    STATE_MESH_RELAY,       /**< Mesh转发阶段（预留）*/
    STATE_MAX
} tag_state_t;

/**
 * @brief Tag运行信息结构体
 */
typedef struct {
    tag_state_t current_state;   /**< 当前状态 */
    tag_state_t previous_state;  /**< 前一个状态 */
    uint8_t     mac_address[6]; /**< Tag MAC地址 */
    uint8_t     battery_level;   /**< 电池电量 0-100% */
    uint32_t    goods_id;        /**< 货物ID */
    uint8_t     is_initialized; /**< 初始化完成标志 */
    uint32_t    wakeup_count;    /**< 唤醒次数统计 */
    uint32_t    adv_count;       /**< 广播次数统计 */
    uint32_t    conn_count;      /**< 连接次数统计 */
} tag_runtime_info_t;

/*==============================================================================
 * External Function Declarations
 *============================================================================*/

/**
 * @brief 获取Tag运行时信息
 * @return tag_runtime_info_t* 运行时信息指针
 */
tag_runtime_info_t *tag_get_runtime_info(void);

/**
 * @brief 状态机初始化（供外部调用）
 * @details 在系统启动时调用，用于初始化状态机并触发各模块初始化
 * @return errcode_t 错误码
 */
errcode_t tag_state_machine_init(void);

/**
 * @brief 状态切换函数
 * @param new_state 目标状态
 * @return errcode_t 错误码
 */
errcode_t tag_state_transition(tag_state_t new_state);

/**
 * @brief 获取当前状态
 * @return tag_state_t 当前状态
 */
tag_state_t tag_get_current_state(void);

/**
 * @brief 获取状态名称字符串（用于日志）
 * @param state 状态枚举值
 * @return const char* 状态名称
 */
const char *tag_state_get_name(tag_state_t state);

/**
 * @brief 主入口任务函数
 * @param arg 任务参数
 * @return void* NULL
 */
void *tag_main_task(const char *arg);

/**
 * @brief 应用主入口（app_run宏调用）
 */
void tag_application_entry(void);

#ifdef __cplusplus
}
#endif

#endif /* _SMART_INVENTORY_MAIN_H_ */

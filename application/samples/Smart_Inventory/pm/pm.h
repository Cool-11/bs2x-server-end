/**
 *******************************************************************************
 * @file pm.h
 * @brief Smart Inventory Tag - Power Management Module
 *
 *  Description:
 *   本模块负责星闪标签的电源管理(Power Management)和低功耗控制。
 *   负责：
 *     1. 系统功耗状态管理（Work/Standby/Sleep三态转换）
 *     2. 深度睡眠(Deep Sleep)和浅睡眠(Light Sleep)控制
 *     3. 唤醒源配置（SLE广播唤醒、GPIO唤醒、定时器唤醒）
 *     4. 睡眠否决票管理（Veto机制）
 *     5. 功耗统计和信息记录
 *
 *  功耗等级说明:
 *   - Work 态：正常工作模式，SLE广播/连接
 *   - Standby 态：待机模式，降低功耗
 *   - Sleep 态：深度休眠，微安级功耗
 *
 *  唤醒源支持:
 *   - SLE广播/组播唤醒
 *   - GPIO引脚中断唤醒
 *   - 定时器唤醒
 *   - 特定事件唤醒
 *
 *  与其他模块关系:
 *   - 被 main 模块调用
 *   - 休眠前需要 adv/conn 模块配合停止广播/断开连接
 *   - 唤醒后通知 main 模块恢复工作
 *
 * @version V1.0
 * @date 2024
 *******************************************************************************
 */
#ifndef _SMART_INVENTORY_PM_H_
#define _SMART_INVENTORY_PM_H_

#include <stdint.h>
#include "errcode.h"

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
 * Macros & Constants
 *============================================================================*/

/* 功耗状态定义 */
#define PM_STATE_WORK       0
#define PM_STATE_STANDBY    1
#define PM_STATE_SLEEP      2

/* 状态转换超时时间 (ms) */
#define PM_WORK_TO_STANDBY_TIMEOUT    5000
#define PM_STANDBY_TO_SLEEP_TIMEOUT   10000

/* GPIO唤醒配置 */
#define PM_WAKEUP_GPIO_NUM            2   /* MGPIO2 */

/*==============================================================================
 * Type Definitions
 *============================================================================*/

/**
 * @brief 电源状态枚举
 */
typedef enum {
    PM_MODE_WORK = 0,       /**< 工作模式 */
    PM_MODE_STANDBY,        /**< 待机模式 */
    PM_MODE_SLEEP,          /**< 睡眠模式 */
    PM_MODE_DEEP_SLEEP      /**< 深度睡眠模式（微安级）*/
} pm_mode_t;

/**
 * @brief 唤醒源枚举
 */
typedef enum {
    PM_WAKEUP_SOURCE_SLE = 0,      /**< SLE广播/组播唤醒 */
    PM_WAKEUP_SOURCE_GPIO,          /**< GPIO引脚唤醒 */
    PM_WAKEUP_SOURCE_TIMER,         /**< 定时器唤醒 */
    PM_WAKEUP_SOURCE_RTC,           /**< RTC唤醒 */
    PM_WAKEUP_SOURCE_MAX
} pm_wakeup_source_t;

/**
 * @brief 睡眠历史信息
 */
typedef struct {
    uint64_t total_sleep_time;     /**< 总睡眠时间 */
    uint32_t sleep_count;          /**< 睡眠次数 */
    uint32_t wakeup_count;         /**< 唤醒次数 */
} pm_sleep_history_t;

/**
 * @brief PM运行时信息
 */
typedef struct {
    pm_mode_t current_mode;        /**< 当前功耗模式 */
    uint8_t  is_sleep_allowed;    /**< 是否允许睡眠 */
    uint8_t  veto_count;          /**< 睡眠否决票数量 */
    uint32_t last_sleep_time;      /**< 上次睡眠时间戳 */
} pm_runtime_info_t;

/*==============================================================================
 * External Function Declarations
 *============================================================================*/

/**
 * @brief 初始化电源管理模块
 * @details 初始化PM子系统、注册状态转换回调、配置唤醒源
 * @return errcode_t 错误码
 */
errcode_t pm_init(void);

/**
 * @brief 进入深度睡眠
 * @details 停止所有广播、断开连接、进入微安级深度休眠
 * @return errcode_t 错误码
 */
errcode_t pm_enter_deep_sleep(void);

/**
 * @brief 进入浅睡眠
 * @details 进入轻度睡眠，支持快速唤醒
 * @return errcode_t 错误码
 */
errcode_t pm_enter_light_sleep(void);

/**
 * @brief 从睡眠中唤醒
 * @details 配置唤醒源、恢复系统时钟
 * @return errcode_t 错误码
 */
errcode_t pm_wakeup(void);

/**
 * @brief 进入待机模式
 * @details 降低系统功耗，保持部分外设工作
 * @return errcode_t 错误码
 */
errcode_t pm_enter_standby(void);

/**
 * @brief 设置唤醒定时器
 * @param timeout_ms 超时时间(ms)
 * @return errcode_t 错误码
 */
errcode_t pm_set_wakeup_timer(uint32_t timeout_ms);

/**
 * @brief 取消唤醒定时器
 * @return errcode_t 错误码
 */
errcode_t pm_cancel_wakeup_timer(void);

/**
 * @brief 添加睡眠否决票
 * @param veto_id 否决票ID
 * @return errcode_t 错误码
 */
errcode_t pm_add_sleep_veto(uint8_t veto_id);

/**
 * @brief 移除睡眠否决票
 * @param veto_id 否决票ID
 * @return errcode_t 错误码
 */
errcode_t pm_remove_sleep_veto(uint8_t veto_id);

/**
 * @brief 重置工作状态计时器
 * @details 防止系统提前进入休眠
 * @return void
 */
void pm_reset_work_timer(void);

/**
 * @brief 获取当前功耗模式
 * @return pm_mode_t 当前功耗模式
 */
pm_mode_t pm_get_current_mode(void);

/**
 * @brief 获取PM运行时信息
 * @return pm_runtime_info_t* 运行时信息指针
 */
pm_runtime_info_t *pm_get_runtime_info(void);

/**
 * @brief 注册GPIO唤醒
 * @param gpio_num GPIO编号
 * @param edge 触发边沿（上升沿/下降沿）
 * @return errcode_t 错误码
 */
errcode_t pm_register_gpio_wakeup(uint8_t gpio_num, uint8_t edge);

/**
 * @brief 获取睡眠历史
 * @return pm_sleep_history_t* 睡眠历史指针
 */
pm_sleep_history_t *pm_get_sleep_history(void);

/*==============================================================================
 * State Transition Handlers (状态转换处理函数)
 *============================================================================*/

/**
 * @brief 状态转换回调结构体
 */
typedef struct {
    errcode_t (*work_to_standby)(uintptr_t arg);    /**< Work -> Standby 转换 */
    errcode_t (*standby_to_sleep)(uintptr_t arg);   /**< Standby -> Sleep 转换 */
    errcode_t (*standby_to_work)(uintptr_t arg);    /**< Standby -> Work 转换 */
    errcode_t (*sleep_to_work)(uintptr_t arg);      /**< Sleep -> Work 转换 */
} pm_state_trans_handler_t;

/**
 * @brief 注册状态转换处理函数
 * @param handler 状态转换回调结构体
 * @return errcode_t 错误码
 */
errcode_t pm_register_state_trans_handler(pm_state_trans_handler_t *handler);

/**
 * @brief 释放资源并准备休眠
 * @details 在进入休眠前调用，停止广播、断开连接
 * @return errcode_t 错误码
 */
errcode_t pm_prepare_for_sleep(void);

#ifdef __cplusplus
}
#endif

#endif /* _SMART_INVENTORY_PM_H_ */

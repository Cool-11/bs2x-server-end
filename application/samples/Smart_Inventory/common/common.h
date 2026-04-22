/**
 *******************************************************************************
 * @file common.h
 * @brief Smart Inventory Tag - Common Utilities Module
 *
 *  Description:
 *   本模块是公共工具库，提供所有模块共用的基础功能函数。
 *   负责：
 *     1. 日志打印接口（LOGI/LOGE/LOGD等）
 *     2. 延时函数（msleep/usleep）
 *     3. 内存操作（malloc/free/内存拷贝/内存设置）
 *     4. 公共宏定义（unused/数组大小等）
 *     5. 基础类型定义
 *     6. 硬件信息获取接口
 *
 *  设计原则:
 *   - 零外部依赖：本模块不依赖任何业务模块
 *   - 统一封装：封装SDK提供的osal接口，提供更友好的API
 *   - 二进制安全：所有内存操作使用安全版本函数
 *
 *  使用注意:
 *   - 所有业务模块应使用本模块提供的接口，避免直接调用osal
 *   - 日志标签(Tag)应在使用处定义，便于追踪日志来源
 *
 * @version V1.0
 * @date 2024
 *******************************************************************************
 */
#ifndef _SMART_INVENTORY_COMMON_H_
#define _SMART_INVENTORY_COMMON_H_

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdarg.h>
#include "osal_time.h"
#include "osal_task.h"
#include "osal_mem.h"
#include "soc_osal.h"

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
 * Macros & Constants
 *============================================================================*/

/* Tag名称定义（默认）*/
#ifndef TAG
#define TAG "INVENTORY"
#endif

/* 通用宏定义 */
#define UNUSED(x)                ((void)(x))                         /**< 标记未使用参数 */
#define ARRAY_SIZE(x)            (sizeof(x) / sizeof((x)[0]))        /**< 计算数组元素个数 */
#define BIT(n)                   (1U << (n))                          /**< 位操作宏 */
#define BYTE_OFFSET(n)           ((n) * 8)                            /**< 字节偏移转位数 */

/* 内存相关宏 */
#define MEMORY_ALIGN_SIZE        4                                     /**< 内存对齐大小 */
#define MEMORY_ALIGN(x)          (((x) + MEMORY_ALIGN_SIZE - 1) & ~(MEMORY_ALIGN_SIZE - 1))

/* 时间相关宏 */
#define MS_PER_SEC               1000
#define US_PER_MS                1000
#define US_PER_SEC               1000000
#define NS_PER_US                1000

/* 任务优先级定义 */
#define TASK_PRIORITY_HIGH       10    /**< 高优先级 */
#define TASK_PRIORITY_NORMAL      20    /**< 普通优先级 */
#define TASK_PRIORITY_LOW        30    /**< 低优先级 */

/* 任务栈大小定义 */
#define STACK_SIZE_SMALL         0x400   /**< 1KB */
#define STACK_SIZE_NORMAL        0x800   /**< 2KB */
#define STACK_SIZE_LARGE         0x1000  /**< 4KB */
#define STACK_SIZE_XLARGE        0x2000  /**< 8KB */

/*==============================================================================
 * Log Level Definitions
 *============================================================================*/

/**
 * @brief 日志级别枚举
 */
typedef enum {
    LOG_LEVEL_NONE = 0,     /**< 不输出 */
    LOG_LEVEL_ERROR,        /**< 错误 */
    LOG_LEVEL_WARN,         /**< 警告 */
    LOG_LEVEL_INFO,         /**< 信息 */
    LOG_LEVEL_DEBUG         /**< 调试 */
} log_level_t;

/*==============================================================================
 * Error Code Definitions
 *============================================================================*/

/**
 * @brief 模块错误码定义
 * @note  错误码格式：高8位为模块ID，低8位为错误序号
 */
typedef enum {
    /* 通用错误码 (0x00xx) */
    ERR_COMMON_SUCCESS       = 0x0000,    /**< 成功 */
    ERR_COMMON_NULL_PTR      = 0x0001,    /**< 空指针 */
    ERR_COMMON_INVALID_PARAM = 0x0002,    /**< 无效参数 */
    ERR_COMMON_NO_MEMORY     = 0x0003,    /**< 内存不足 */
    ERR_COMMON_TIMEOUT       = 0x0004,    /**< 超时 */
    ERR_COMMON_BUSY          = 0x0005,    /**< 忙 */
    ERR_COMMON_NO_RESOURCE   = 0x0006,    /**< 资源不足 */

    /* 模块错误码 (待扩展) */
    ERR_MODULE_BASE          = 0x0100     /**< 模块错误码基址 */
} common_errcode_t;

/*==============================================================================
 * Log Functions
 *============================================================================*/

/**
 * @brief 设置日志级别
 * @param level 日志级别
 * @return void
 */
void common_log_set_level(log_level_t level);

/**
 * @brief 获取日志级别
 * @return log_level_t 当前日志级别
 */
log_level_t common_log_get_level(void);

/**
 * @brief 日志打印函数（底层）
 * @param level 日志级别
 * @param tag 日志标签
 * @param fmt 格式字符串
 * @param args 可变参数列表
 * @return void
 */
void common_log_print(log_level_t level, const char *tag, const char *fmt, va_list args);

/**
 * @brief 错误日志打印
 * @param fmt 格式字符串
 * @param ... 可变参数
 */
void common_log_error(const char *fmt, ...);

/**
 * @brief 警告日志打印
 * @param fmt 格式字符串
 * @param ... 可变参数
 */
void common_log_warn(const char *fmt, ...);

/**
 * @brief 信息日志打印
 * @param fmt 格式字符串
 * @param ... 可变参数
 */
void common_log_info(const char *fmt, ...);

/**
 * @brief 调试日志打印
 * @param fmt 格式字符串
 * @param ... 可变参数
 */
void common_log_debug(const char *fmt, ...);

/* 日志宏定义（带Tag）*/
#define LOGE(fmt, ...)  common_log_error("[%s] " fmt, TAG, ##__VA_ARGS__)
#define LOGW(fmt, ...)  common_log_warn("[%s] " fmt, TAG, ##__VA_ARGS__)
#define LOGI(fmt, ...)  common_log_info("[%s] " fmt, TAG, ##__VA_ARGS__)
#define LOGD(fmt, ...)  common_log_debug("[%s] " fmt, TAG, ##__VA_ARGS__)

/*==============================================================================
 * Time Functions
 *============================================================================*/

/**
 * @brief 毫秒延时
 * @param ms 延时毫秒数
 * @return void
 */
void common_msleep(uint32_t ms);

/**
 * @brief 微秒延时
 * @param us 延时微秒数
 * @return void
 */
void common_usleep(uint32_t us);

/**
 * @brief 获取系统运行时间（毫秒）
 * @return uint32_t 运行时间(ms)
 */
uint32_t common_get_tick_ms(void);

/**
 * @brief 获取系统运行时间（微秒）
 * @return uint64_t 运行时间(us)
 */
uint64_t common_get_tick_us(void);

/**
 * @brief 检查是否超时
 * @param start_time 开始时间
 * @param timeout_ms 超时时间(ms)
 * @return bool true-已超时，false-未超时
 */
bool common_is_timeout(uint32_t start_time, uint32_t timeout_ms);

/*==============================================================================
 * Memory Functions
 *============================================================================*/

/**
 * @brief 分配内存
 * @param size 内存大小
 * @return void* 分配成功返回指针，失败返回NULL
 */
void *common_malloc(uint32_t size);

/**
 * @brief 释放内存
 * @param ptr 内存指针
 * @return void
 */
void common_free(void *ptr);

/**
 * @brief 分配对齐内存
 * @param size 内存大小
 * @param align 对齐要求
 * @return void* 分配成功返回指针，失败返回NULL
 */
void *common_malloc_align(uint32_t size, uint32_t align);

/**
 * @brief 释放对齐内存
 * @param ptr 内存指针
 * @return void
 */
void common_free_align(void *ptr);

/**
 * @brief 安全内存拷贝
 * @param dest 目标地址
 * @param dest_max 目标缓冲区最大长度
 * @param src 源地址
 * @param src_len 源数据长度
 * @return int 成功返回0，失败返回-1
 */
int common_memcpy_s(void *dest, uint32_t dest_max, const void *src, uint32_t src_len);

/**
 * @brief 安全内存设置
 * @param dest 目标地址
 * @param dest_max 目标缓冲区最大长度
 * @param value 设置值
 * @param count 设置长度
 * @return int 成功返回0，失败返回-1
 */
int common_memset_s(void *dest, uint32_t dest_max, uint8_t value, uint32_t count);

/**
 * @brief 安全字符串拷贝
 * @param dest 目标地址
 * @param dest_max 目标缓冲区最大长度
 * @param src 源地址
 * @return int 成功返回0，失败返回-1
 */
int common_strcpy_s(char *dest, uint32_t dest_max, const char *src);

/**
 * @brief 安全字符串拼接
 * @param dest 目标地址
 * @param dest_max 目标缓冲区最大长度
 * @param src 源地址
 * @return int 成功返回0，失败返回-1
 */
int common_strcat_s(char *dest, uint32_t dest_max, const char *src);

/*==============================================================================
 * Task Functions
 *============================================================================*/

/**
 * @brief 创建任务
 * @param name 任务名称
 * @param entry 任务入口函数
 * @param arg 任务参数
 * @param stack_size 栈大小
 * @param priority 优先级
 * @return osal_task* 任务句柄，失败返回NULL
 */
osal_task *common_task_create(const char *name, osal_kthread_handler entry,
                              int32_t arg, uint32_t stack_size, uint32_t priority);

/**
 * @brief 删除任务
 * @param task 任务句柄
 * @return void
 */
void common_task_delete(osal_task *task);

/**
 * @brief 挂起任务
 * @param task 任务句柄
 * @return void
 */
void common_task_suspend(osal_task *task);

/**
 * @brief 恢复任务
 * @param task 任务句柄
 * @return void
 */
void common_task_resume(osal_task *task);

/**
 * @brief 获取当前任务句柄
 * @return osal_task* 当前任务句柄
 */
osal_task *common_task_get_current(void);

/*==============================================================================
 * Atomic Operations
 *============================================================================*/

/**
 * @brief 关闭中断
 * @return uint32_t 之前的中断状态
 */
uint32_t common_irq_lock(void);

/**
 * @brief 恢复中断
 * @param irq_state 之前的中断状态
 * @return void
 */
void common_irq_restore(uint32_t irq_state);

/*==============================================================================
 * Utility Functions
 *============================================================================*/

/**
 * @brief 获取系统启动原因
 * @return uint32_t 启动原因码
 */
uint32_t common_get_boot_reason(void);

/**
 * @brief 将16进制数据转换为字符串
 * @param data 数据指针
 * @param len 数据长度
 * @param out 输出字符串
 * @param out_max 输出缓冲区最大长度
 * @return int 转换后的字符串长度
 */
int common_hex_to_string(const uint8_t *data, uint32_t len, char *out, uint32_t out_max);

/**
 * @brief 将16进制字符转换为数值
 * @param c 16进制字符 (0-9, A-F, a-f)
 * @return uint8_t 转换后的数值
 */
static inline uint8_t common_hex_char_to_val(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return 0;
}

/**
 * @brief 将字符串转换为16进制数据
 * @param str 字符串
 * @param out 输出数据
 * @param out_max 输出缓冲区最大长度
 * @return int 转换后的数据长度
 */
int common_string_to_hex(const char *str, uint8_t *out, uint32_t out_max);

/**
 * @brief 检查字符串是否是16进制格式
 * @param str 字符串
 * @return bool true-是，false-否
 */
bool common_is_hex_string(const char *str);

#ifdef __cplusplus
}
#endif

#endif /* _SMART_INVENTORY_COMMON_H_ */

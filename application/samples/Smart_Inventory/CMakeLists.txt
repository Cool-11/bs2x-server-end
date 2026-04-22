#===============================================================================
# @brief    CMake file for Smart Inventory Tag
# @brief    星闪无感盘点标签(Smart Inventory Tag)编译配置文件
#
# Description:
#   本文件定义了 Smart Inventory Tag 工程的源代码列表和头文件目录。
#   支持模块化编译，各功能模块独立源文件，方便维护和扩展。
#
# Module Structure:
#   - main/       : 主入口 + 状态机
#   - adv/        : 广播配置模块
#   - conn/       : 连接管理模块
#   - callback/   : 回调函数模块
#   - pm/         : 电源管理模块
#   - data/       : 数据处理模块
#   - common/     : 公共工具库模块
#
# Usage:
#   在 products/CMakeLists.txt 中通过 CONFIG_SAMPLE_SUPPORT_SPARK_INVENTORY_TAG 开关控制
#
# Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2024-2024. All rights reserved.
#===============================================================================

if(DEFINED CONFIG_SAMPLE_SUPPORT_SPARK_INVENTORY_TAG)
    # 源代码文件列表
    set(SOURCES_LIST
        ${CMAKE_CURRENT_SOURCE_DIR}/main/main.c
        ${CMAKE_CURRENT_SOURCE_DIR}/adv/adv.c
        ${CMAKE_CURRENT_SOURCE_DIR}/conn/conn.c
        ${CMAKE_CURRENT_SOURCE_DIR}/callback/callback.c
        ${CMAKE_CURRENT_SOURCE_DIR}/pm/pm.c
        ${CMAKE_CURRENT_SOURCE_DIR}/data/data.c
        ${CMAKE_CURRENT_SOURCE_DIR}/common/common.c
    )

    # 头文件目录列表
    set(HEADER_LIST
        ${CMAKE_CURRENT_SOURCE_DIR}/main
        ${CMAKE_CURRENT_SOURCE_DIR}/adv
        ${CMAKE_CURRENT_SOURCE_DIR}/conn
        ${CMAKE_CURRENT_SOURCE_DIR}/callback
        ${CMAKE_CURRENT_SOURCE_DIR}/pm
        ${CMAKE_CURRENT_SOURCE_DIR}/data
        ${CMAKE_CURRENT_SOURCE_DIR}/common
    )
endif()

# 将变量传递给父级CMake
set(SOURCES "${SOURCES}" ${SOURCES_LIST} PARENT_SCOPE)
set(PUBLIC_HEADER "${PUBLIC_HEADER}" ${HEADER_LIST} PARENT_SCOPE)

#===============================================================================
# @brief     子目录 CMakeLists.txt (My_project_2x)
#===============================================================================

# 1. 建议使用更准确的 STREQUAL 判定，确保配置确实开启
if (CONFIG_SAMPLE_SUPPORT_MY_PROJECT_2X STREQUAL "y")
    
    # 打印日志确认进入了编译分支（你在日志里已经看到了，说明这步没问题）
    message(STATUS ">>>> [My_Project_2x] Injecting Sources and Headers <<<<")

    # 2. 构造源文件绝对路径列表
    set(MY_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/app/main.c
        ${CMAKE_CURRENT_SOURCE_DIR}/components/shared_protocol/shared_protocol.c
        ${CMAKE_CURRENT_SOURCE_DIR}/components/hardware_hal/hardware_hal.c
        ${CMAKE_CURRENT_SOURCE_DIR}/components/sle_slave/sle_slave_mgr.c
        ${CMAKE_CURRENT_SOURCE_DIR}/components/storage_sync/storage_sync.c
    )

    # 3. 构造头文件绝对路径列表
    set(MY_HEADERS
        ${CMAKE_CURRENT_SOURCE_DIR}/app
        ${CMAKE_CURRENT_SOURCE_DIR}/components/shared_protocol
        ${CMAKE_CURRENT_SOURCE_DIR}/components/hardware_hal
        ${CMAKE_CURRENT_SOURCE_DIR}/components/sle_slave
        ${CMAKE_CURRENT_SOURCE_DIR}/components/storage_sync
    )

    # 4. 【关键步骤 A】：本地包含，保证当前目录可见
    include_directories(${MY_HEADERS})

    # 5. 【关键步骤 B】：将路径提升到父作用域，并确保列表格式正确
    # 注意：不要加双引号，直接以列表形式追加
    set(SOURCES ${SOURCES} ${MY_SOURCES} PARENT_SCOPE)
    set(PUBLIC_HEADER ${PUBLIC_HEADER} ${MY_HEADERS} PARENT_SCOPE)

endif()
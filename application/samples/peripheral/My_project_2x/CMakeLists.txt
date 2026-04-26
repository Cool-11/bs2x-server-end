#===============================================================================
# @brief    cmake file for My_project_2x
#===============================================================================
set(COMPONENT_NAME "my_project_2x")

set(SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/app/main.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/components/shared_protocol/shared_protocol.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/components/hardware_hal/hardware_hal.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/components/sle_slave/sle_slave_mgr.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/components/storage_sync/storage_sync.c"
)

set(PUBLIC_HEADER
    "${CMAKE_CURRENT_SOURCE_DIR}/components/shared_protocol/shared_protocol.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/components/hardware_hal/hardware_hal.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/components/sle_slave/sle_slave_mgr.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/components/storage_sync/storage_sync.h"
)

set(PRIVATE_HEADER
)

set(PRIVATE_DEFINES
)

set(PUBLIC_DEFINES
)

set(COMPONENT_PUBLIC_INC
    "${CMAKE_CURRENT_SOURCE_DIR}/components/shared_protocol"
    "${CMAKE_CURRENT_SOURCE_DIR}/components/hardware_hal"
    "${CMAKE_CURRENT_SOURCE_DIR}/components/sle_slave"
    "${CMAKE_CURRENT_SOURCE_DIR}/components/storage_sync"
)

build_component()

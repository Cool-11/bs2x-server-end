# app

## 功能描述
- 业务入口：通过 `app_run(my_project_2x_entry)` 注册启动。
- 接收 `sle_slave` 上报的写指令并执行分支：寻物、停止寻物、数量更新。
- 调用 `storage_sync` 管理库存镜像并触发广播刷新。

## 依赖关系
- 依赖 `sle_slave` 承载 SLE 协议栈与写回调。
- 依赖 `hardware_hal` 控制声光。
- 依赖 `storage_sync` 更新 `qty/status/seq`。

## 逻辑验证
- 启动应打印 `[BS2x_INIT] Entering my_project_2x_entry`。
- 写入 `0x01` 时应同时看到 SLE、HAL、SYNC 三类日志。
- 写入 `0x10 0x00 0x2D` 时应看到 `Qty updated to 45` 且 `seq` 递增。

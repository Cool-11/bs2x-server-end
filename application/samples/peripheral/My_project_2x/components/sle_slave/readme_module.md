# sle_slave

## 功能描述
- 负责 SLE 从设备角色：注册设备管理回调、连接回调、SSAP Server 回调。
- 负责广播参数设置、广播数据下发，以及 SSAP 写入命令接收。

## 依赖关系
- 依赖真实 SDK API：`sle_dev_manager_register_callbacks`、`enable_sle`、`sle_set_announce_param`、`sle_set_announce_data`、`sle_start_announce`。
- 依赖 SSAP Server API：`ssaps_register_server`、`ssaps_add_service_sync`、`ssaps_add_property_sync`、`ssaps_register_callbacks`。
- 通过回调把写入命令上抛给 `app/main.c`。

## 逻辑验证
- 上电后串口出现 `[BS2x_INIT] Entering sle_slave_init` 与 `sle enable`。
- 收到单播写入后串口出现 `[BS2x_SLE] Received SSAP Write len:X first:0xYY`。
- 数量更新后应看到 `refresh adv payload` 相关日志，表示广播数据已刷新。

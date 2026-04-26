# storage_sync

## 功能描述
- 维护 BS21e 标签 RAM 镜像库存结构体 `shared_proto_adv_field_t`。
- 接收业务层更新（数量/寻物状态）并统一触发广播 Payload 刷新。
- 严格保证数量变化时 `seq++`，满足 WS63 侧变更追踪。

## 依赖关系
- 依赖 `shared_protocol` 提供 12 字节协议结构与校验。
- 通过回调调用 `sle_slave` 的 `sle_slave_refresh_adv_payload` 完成广播数据更新。
- 被 `app/main.c` 业务入口调用。

## 逻辑验证
- 上电后看到日志：`[BS2x_SYNC] Entering storage_sync_init` 与 `init ok`。
- 收到数量更新写命令后，看到日志：`Qty updated to X ... (seq=Y)`。
- 随后看到日志：`publish ok qty=X status=... seq=Y`，表示已触发广播刷新。

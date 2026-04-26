# shared_protocol

## 功能描述
- 定义 WS63 与 BS21e 对齐的公共协议结构 `shared_proto_adv_field_t`（强制 12 字节）。
- 提供单播写入指令解析：`0x01` 寻物、`0x00` 停止、`0x10 High Low` 更新数量。

## 依赖关系
- 被 `sle_slave` 用于广播 Payload 编码。
- 被 `app` 和 `storage_sync` 共享，用于统一业务结构与命令语义。

## 逻辑验证
- 发送 `0x10 0x00 0x2D` 后，业务层应解析出 `qty=45`。
- `sizeof(shared_proto_adv_field_t)` 必须为 12。
- magic 必须为 `0xAABBCCDD`，否则拒绝刷新广播。

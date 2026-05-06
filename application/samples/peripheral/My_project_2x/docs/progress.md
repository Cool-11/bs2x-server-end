# BS2x Server端工程进度

> 更新日期：2026-05-05
> 当前阶段：第二轮迭代完成，断点日志全覆盖，编译通过

## 已完成任务

### P0 关键修复
| 编号 | 任务 | 状态 | 说明 |
|------|------|------|------|
| T01 | conn_latency 499→15 | ✅ | 解决latency>timeout导致连接断开问题 |
| T02 | CCCD Descriptor | ✅ | 添加UUID=0x2902的CCCD描述符，支持Notify订阅 |

### P1 功能开发
| 编号 | 任务 | 状态 | 说明 |
|------|------|------|------|
| T03 | 0x02盘点命令 | ✅ | shared_protocol新增INVENTORY命令定义和解析 |
| T04 | 0x20配网命令 | ✅ | shared_protocol新增BIND_TAG命令定义和解析 |
| T05 | tag_id NV持久化 | ✅ | storage_sync使用NV_ID_BS2X_TAG_ID(0x3001)存储 |
| T06 | Notify发送能力 | ✅ | 新增sle_slave_notify_conn()单连接通知 |
| T07 | status自动恢复 | ✅ | 寻物15秒后自动恢复status=0x00并刷新广播 |
| T08 | 命令处理集成 | ✅ | main.c集成0x02/0x20命令处理和Notify回复 |

### P2 第二轮迭代（断点日志 + 低功耗 + 出库逻辑）
| 编号 | 任务 | 状态 | 说明 |
|------|------|------|------|
| T11 | sle_slave_mgr断点日志 | ✅ | SSAP注册/连接状态/Notify发送/广播刷新全链路[BP]日志 |
| T12 | storage_sync断点日志 | ✅ | NV读写/状态变更/publish操作[BP]日志 |
| T13 | main.c断点日志 | ✅ | 命令入口/分支/Notify回复/PM状态转换/定时器[BP]日志 |
| T14 | shared_protocol断点日志 | ✅ | 解析入口/命令码/数据长度/原始hex/解析结果[BP]日志 |
| T14b | hardware_hal断点日志 | ✅ | GPIO操作/定时器/初始化/自动关闭[BP]日志 |
| T15 | 低功耗模式配置 | ✅ | Work→Standby:5s, Standby→Sleep:30s, PM回调注册 |
| T16 | 出库逻辑(qty=0) | ✅ | qty=0时status自动置0x02(OUTSTOCK), qty>0自动恢复NORMAL |
| T17 | 编译验证 | ✅ | 零错误零警告，Build 100% |

### 验证
| 编号 | 任务 | 状态 | 说明 |
|------|------|------|------|
| T09 | 编译验证(第一轮) | ✅ | 零错误零警告，Build success |
| T10 | 进度文档 | ✅ | 本文档 |
| T17 | 编译验证(第二轮) | ✅ | 零错误零警告，Build 100% |

## 断点日志覆盖范围

所有核心模块已统一使用 `[BP]` 前缀标记断点日志，串口搜索 `[BP]` 即可追踪完整调用链：

| 模块 | 日志前缀 | 覆盖关键点 |
|------|---------|-----------|
| shared_protocol | `[BS2x_PROTO][BP]` | 命令码/数据长度/原始hex/解析结果/magic校验 |
| sle_slave_mgr | `[BS2x_SLE][BP]` / `[BS2x_INIT][BP]` | SSAP注册/广播启停/连接增删/Write回调/Notify发送/广播刷新/MAC生成 |
| storage_sync | `[BS2x_SYNC][BP]` | NV读写/publish/qty变更/find状态/tag_id绑定/出库状态 |
| hardware_hal | `[BS2x_HAL][BP]` | GPIO操作/定时器启停/自动关闭/初始化 |
| main.c | `[BS2x_APP][BP]` | 命令分发/PM状态转换/定时器操作/Notify回复结果 |

### 日志追踪示例（寻物流程）
```
[BS2x_SLE] Received SSAP Write conn_id:0x1 len:1 data: 01
[BS2x_PROTO][BP] parse enter cmd:0x01 len:1 raw: 01
[BS2x_PROTO][BP] parse OK action=FIND_ME(0x01)
[BS2x_SLE][BP] parse cmd OK action:1 qty:0 tag_id:0
[BS2x_APP][BP] on_unicast_cmd conn_id:0x1 action:2 qty:0 tag_id:0
[BS2x_APP][BP] >> FIND_ME
[BS2x_HAL][BP] beep_on request ms=15000
[BS2x_HAL][BP] beep_on OK gpio=1
[BS2x_HAL][BP] led_on request ms=15000
[BS2x_HAL][BP] led_on OK gpio=0
[BS2x_SYNC][BP] find_status=FINDING seq=1
[BS2x_SYNC][BP] publish OK tag:1 qty:5 status:0x01 bat:100 seq:1
[BS2x_SLE][BP] refresh_adv tag:1 qty:5 status:0x01 seq:1
[BS2x_APP][BP] << FIND_ME done
```

## 修改文件清单

### 第一轮迭代
| 文件 | 修改内容 |
|------|---------|
| sle_slave_mgr.c | conn_latency 0x1F3→0x0F；添加CCCD Descriptor(uuid=0x2902)；新增sle_slave_notify_conn()；回调签名增加conn_id |
| sle_slave_mgr.h | 回调签名on_unicast_cmd增加conn_id参数；新增sle_slave_notify_conn声明 |
| shared_protocol.h | 新增CMD_INVENTORY(0x02)、CMD_BIND_TAG(0x20)、RSP_INVENTORY(0x82)、RSP_BIND_OK(0xA0)、RSP_BIND_FAIL(0xAF)；新增inventory_rsp_t和bind_rsp_t结构体；unicast_cmd_t增加tag_id字段 |
| shared_protocol.c | 新增0x02和0x20命令解析逻辑 |
| storage_sync.h | 新增NV_ID_BS2X_TAG_ID(0x3001)；新增set_tag_id/get_tag_id/get_field/publish接口 |
| storage_sync.c | 新增tag_id NV读写；init时从NV恢复tag_id；新增set_tag_id/get_tag_id/get_field/publish实现 |
| main.c | 新增find_status_restore_timer(15秒自动恢复)；新增send_inventory_rsp/send_bind_rsp；on_unicast_cmd增加INVENTORY和BIND_TAG处理；回调签名适配conn_id |

### 第二轮迭代
| 文件 | 修改内容 |
|------|---------|
| shared_protocol.c | 新增soc_osal.h引用；新增SHARED_PROTO_LOG宏；adv_field_is_valid添加magic校验[BP]日志；parse_unicast_cmd添加入口/原始hex/解析结果/失败原因[BP]日志 |
| hardware_hal.c | 所有函数统一[BP]前缀日志；init/force_off/auto_off_timer添加状态信息；beep_on/led_on添加GPIO编号和错误追踪；beep_off/led_off添加定时器停止追踪 |
| sle_slave_mgr.c | sle_enable_cbk添加[BP]日志(register_callbacks/setup_ssap/setup_announce/start_announce返回值)；ssaps_write_request添加[BP]解析日志；notify_conn添加[BP]发送结果；refresh_adv_payload添加[BP]字段日志 |
| storage_sync.c | NV读写添加[BP]日志；set_qty添加出库状态(0x02)逻辑和[BP]日志；set_find_status/set_tag_id/publish添加[BP]日志 |
| main.c | PM状态转换添加[BP]日志；命令处理添加入口/分支/完成[BP]日志；定时器操作添加[BP]日志；PM超时配置Work→Standby:5s, Standby→Sleep:30s；主循环心跳5s |

## 协议命令速查

| 命令码 | 含义 | 方向 | 回复 |
|--------|------|------|------|
| 0x00 | 停止寻物 | 63→21e | 无 |
| 0x01 | 寻物 | 63→21e | 无（广播status→0x01，15秒自动恢复0x00） |
| 0x02 | 盘点请求 | 63→21e | Notify [0x82, tag_id(2B), qty(2B), status, battery, seq(2B)] 共9字节 |
| 0x10 | 更新数量 | 63→21e | 无（广播qty实时更新） |
| 0x20 | 写入tag_id | 63→21e | Notify [0xA0, tag_id(2B)] 成功 / [0xAF, tag_id(2B)] 失败 |

### status字段含义
| 值 | 含义 | 触发条件 |
|----|------|---------|
| 0x00 | NORMAL | 默认状态/寻物恢复 |
| 0x01 | FINDING | 收到0x01寻物命令 |
| 0x02 | OUTSTOCK | qty被设为0 |

## 待开发项（下一迭代）

1. 电池电量ADC采集（当前硬编码100）
2. 多连接压力测试
3. 与WS63端联调验证
4. 低功耗实际功耗测量与超时调优

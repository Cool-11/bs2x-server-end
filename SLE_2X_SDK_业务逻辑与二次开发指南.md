# BS2X 2x SDK：SLE 广播/连接/数据传输业务逻辑与二次开发指南

> 调研范围：`/home/cool/fbb_bs2x/src` 下 SLE 相关头文件与典型样例（`sle_uart`、`sle_multi_conn`、`rcu`、`sle_ota_dongle`、`sle_one_to_many`）。

---

## 1. SDK 全局结构（SLE 视角）

- **协议对外 API 头文件（最关键）**：
  - `include/middleware/services/bts/sle/sle_device_manager.h`
  - `include/middleware/services/bts/sle/sle_device_discovery.h`
  - `include/middleware/services/bts/sle/sle_connection_manager.h`
  - `include/middleware/services/bts/sle/sle_ssap_server.h`
  - `include/middleware/services/bts/sle/sle_ssap_client.h`
  - `include/middleware/services/bts/sle/sle_transmition_manager.h`
  - `include/middleware/services/bts/sle/sle_low_latency.h`

- **应用样例主目录**：
  - `application/samples/products/`（产品化样例，最有参考价值）
  - `application/samples/peripheral/`（外设样例，便于快速上手）

- **当前工程默认示例配置**：
  - `defconfig` 中已启用：`CONFIG_SAMPLE_SUPPORT_SLE_UART=y`、`CONFIG_SAMPLE_SUPPORT_SLE_UART_CLIENT=y`
  - 对应会进入 `application/samples/products/sle_uart/`

---

## 2. SLE 核心分层模型（你后续二开要按这个层次设计）

1. **设备管理层（上电/使能）**
   - 注册回调：`sle_dev_manager_register_callbacks`
   - 启动协议栈：`enable_sle`
   - 典型回调：`sle_power_on_cb`、`sle_enable_cb`

2. **发现层（广播/扫描）**
   - 广播端：`sle_set_announce_param` + `sle_set_announce_data` + `sle_start_announce`
   - 扫描端：`sle_set_seek_param` + `sle_start_seek`
   - 回调注册：`sle_announce_seek_register_callbacks`

3. **连接与配对层**
   - 建连：`sle_connect_remote_device`
   - 配对：`sle_pair_remote_device`
   - 断链：`sle_disconnect_remote_device`
   - 回调注册：`sle_connection_register_callbacks`

4. **业务数据层（SSAP）**
   - 服务端：`ssaps_register_server` -> `ssaps_add_service_sync` -> `ssaps_add_property_sync` -> `ssaps_start_service`
   - 客户端：`ssapc_register_client` -> `ssapc_find_structure` -> `ssapc_write_req/read_req`
   - 通知/指示：`ssaps_notify_indicate` / `ssaps_notify_indicate_by_uuid`

5. **链路优化层（可选）**
   - 低时延：`sle_low_latency_set` / `sle_low_latency_tx_enable` / `sle_low_latency_rx_enable`
   - PHY/MCS：`sle_set_phy_param`、`sle_set_mcs`
   - 拥塞反馈：`sle_transmission_register_callbacks`（`SLE_QOS_IDLE/FLOWCTRL/BUSY`）

---

## 3. 广播业务逻辑（普通广播）

以 `sle_uart_server_adv.c`、`sle_multi_conn_server_adv.c` 为代表：

### 3.1 初始化顺序

1. 设备管理回调注册（power on / enable）
2. 在 `sle_enable_cb` 内初始化 server 业务（服务表、广播）
3. 设置广播参数 `sle_set_announce_param`
4. 设置广播/扫描响应数据 `sle_set_announce_data`
5. 启动广播 `sle_start_announce`

### 3.2 广播参数关键项

- `announce_mode = SLE_ANNOUNCE_MODE_CONNECTABLE_SCANABLE`
- `announce_gt_role = SLE_ANNOUNCE_ROLE_T_CAN_NEGO`
- `announce_interval_min/max`
- `conn_interval_min/max`
- `conn_supervision_timeout`
- `own_addr`（可显式设置固定地址，样例里常见）

### 3.3 广播数据内容

通常包含：
- discovery level
- access mode
- local name（用于扫描侧匹配设备）
- tx power（在 scan response 中）

---

## 4. 定向广播 + 直连业务逻辑（重点）

你提到“广播和直接建立连接”，SDK 里两种都支持：

### 4.1 扫描后主动连接（最常见）

客户端流程（`sle_uart_client.c`、`sle_multi_conn_client.c`）：
1. `sle_start_seek`
2. 在 `seek_result_cb` 里匹配目标（名称/MAC）
3. `sle_stop_seek`
4. 在 `seek_disable_cb` 中调用 `sle_connect_remote_device(&addr)`

### 4.2 定向广播（Directed Announce）

`application/samples/products/rcu/rcu/sle_rcu_server/sle_rcu_server_adv.c` 中有完整示例：

- 设置 `announce_mode = SLE_ANNOUNCE_MODE_CONNECTABLE_DIRECTED`
- 填写 `peer_addr`（目标设备地址）
- 调用 `sle_set_announce_param` + `sle_start_announce`

这套逻辑适合：
- 唤醒指定已绑定设备
- 降低非目标设备误连
- 快速重连场景

---

## 5. 连接建立后的业务逻辑（标准链路）

### 5.1 连接状态回调

`sle_connection_register_callbacks` 后，重点关注：
- `connect_state_changed_cb`
- `pair_complete_cb`
- （可选）`set_phy_cb`、`connect_param_update_cb`、`read_rssi_cb`

### 5.2 推荐时序

1. `SLE_ACB_STATE_CONNECTED`
2. 触发 `sle_pair_remote_device`（如需要安全）
3. `ssapc_exchange_info_req`（协商 MTU）
4. `ssapc_find_structure`（发现对端 service/property）
5. 保存目标 `handle`
6. 开始 `ssapc_write_req` / 接收 notification

---

## 6. SSAP 数据传输调用链（你要重点改的部分）

## 6.1 Server 侧

- 注册：`ssaps_register_server`
- 建表：
  - `ssaps_add_service_sync`
  - `ssaps_add_property_sync`
  - `ssaps_add_descriptor_sync`
- 启动服务：`ssaps_start_service`
- 发送数据：
  - 按 handle：`ssaps_notify_indicate`
  - 按 UUID：`ssaps_notify_indicate_by_uuid`
- 接收客户端写入：`ssaps_write_request_callback`

> `sle_uart_server.c` 里就是“串口数据 -> SSAP 通知”与“SSAP 写入 -> 串口输出”的双向桥接。

### 6.2 Client 侧

- 注册：`ssapc_register_client`
- 回调注册：`ssapc_register_callbacks`
- 发现：`ssapc_find_structure`
- 写：`ssapc_write_req` / `ssapc_write_cmd`
- 读：`ssapc_read_req` / `ssapc_read_req_by_uuid`
- 收包：`notification_cb` / `indication_cb`

---

## 7. 低时延与吞吐优化（可选）

样例中可见策略：
- 开启 low latency：`sle_low_latency_set(conn_id, true, 1000/2000)`
- 设置 PHY：`sle_set_phy_param`
- 设置 MCS：`sle_set_mcs`
- 根据链路忙闲状态降速：`sle_transmission_register_callbacks` + `SLE_QOS_*`

建议：先功能跑通，再按业务目标逐步启用这些优化。

---

## 8. 你在 2x SDK 上做二次开发的推荐路径

## 阶段 A：先选模板

建议直接复制改造：
- 单连接串口透传：`application/samples/products/sle_uart/`
- 多连接：`application/samples/products/sle_multi_conn/`
- 定向广播/唤醒：`application/samples/products/rcu/rcu/sle_rcu_server/`

## 阶段 B：改三件核心

1. **设备标识**：本地名、广播数据、厂商字段
2. **服务模型**：自定义 service/property UUID 与权限
3. **业务状态机**：扫描匹配策略、重连策略、断链恢复

## 阶段 C：工程接入

1. 在 `application/samples/products/CMakeLists.txt` 挂接你的目录（或复用现有 sample 目录）
2. 在 `defconfig` 开启对应 `CONFIG_SAMPLE_SUPPORT_xxx`
3. 编译运行后先验证：
   - 能广播
   - 能扫描并连接
   - 能完成 MTU 交换 + 属性发现
   - 能稳定双向收发

## 阶段 D：稳定性与量产化

- 增加异常处理：超时、重连退避、配对信息管理
- 处理 busy/flowctrl 场景的缓存与重发
- 加入日志等级控制与统计（收发包计数、丢包、重连次数）

---

## 9. 我对你当前 SDK 的结论（可直接执行）

1. **这个 SDK 的 SLE 功能完整**：已覆盖广播、扫描、建连、配对、SSAP 服务/客户端、低时延和吞吐优化。
2. **最适合你当前需求的基线**：`sle_uart`（最清晰的一对一数据链路）+ `rcu directed announce`（定向连接逻辑）。
3. **二次开发最佳实践**：
   - 先保持原样例调用链不变，只替换 UUID/广播字段/业务 payload；
   - 再逐步引入多连接、低时延、重连策略；
   - 最后收敛到你自己的“设备管理 + 链路管理 + 业务协议”三层结构。

---

## 10. 关键文件索引（便于你快速跳转）

- 协议接口：
  - `include/middleware/services/bts/sle/sle_device_manager.h`
  - `include/middleware/services/bts/sle/sle_device_discovery.h`
  - `include/middleware/services/bts/sle/sle_connection_manager.h`
  - `include/middleware/services/bts/sle/sle_ssap_server.h`
  - `include/middleware/services/bts/sle/sle_ssap_client.h`
  - `include/middleware/services/bts/sle/sle_transmition_manager.h`
  - `include/middleware/services/bts/sle/sle_low_latency.h`

- 单连接参考：
  - `application/samples/products/sle_uart/sle_uart.c`
  - `application/samples/products/sle_uart/sle_uart_server/sle_uart_server.c`
  - `application/samples/products/sle_uart/sle_uart_server/sle_uart_server_adv.c`
  - `application/samples/products/sle_uart/sle_uart_client/sle_uart_client.c`

- 多连接参考：
  - `application/samples/products/sle_multi_conn/client/sle_multi_conn_client.c`
  - `application/samples/products/sle_multi_conn/server/sle_multi_conn_server_adv.c`

- 定向广播参考：
  - `application/samples/products/rcu/rcu/sle_rcu_server/sle_rcu_server_adv.c`

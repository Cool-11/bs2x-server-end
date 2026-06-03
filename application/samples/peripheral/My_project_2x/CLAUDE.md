# CLAUDE.md — BS2x 智能标签固件开发指南

> 适用于：`application/samples/peripheral/My_project_2x/`
> 芯片平台：BS21E（SLE Server端）
> 系统角色：星闪多模态仓储管家系统 — 微功耗标签端
## 一、项目概述

### 1.1 系统双端架构

```
┌──────────────────────┐    SLE     ┌──────────────┐
│       WS63           │ ◄───────► │    BS21E     │
│  (串口屏 + 网关)     │           │  (标签群)    │
│  - 用户交互界面      │           │  - 数据存储   │
│  - SLE Client        │           │  - 广播       │
│  - 映射表管理        │           │  - 寻物执行   │
│  - TF卡存储          │           │  - 低功耗     │
└──────────────────────┘           └──────────────┘
```

- **WS63**：串口屏交互、SLE通信、映射表管理、TF卡存储、RSSI扫描
- **BS21E**：标签数据存储、广播、寻物执行、低功耗管理

### 1.2 BS21E 端职责

1. 上电生成唯一MAC，持久化到NV，重启不变
2. 持续广播 `tag_id + qty + status + battery + seq`（12字节厂商数据）
3. 接收SSAP单播命令：寻物(0x01)、盘点(0x02)、更新数量(0x10)、绑定tag_id(0x20)、解绑(0x21)
4. 无连接超时进入低功耗 Standby/Sleep

### 1.3 运行架构（LiteOS 事件驱动）
```
bt_service 任务（SDK内部，SLE协议栈）
    ↓ SSAP Write 回调
    ↓ 解析命令 → osEventFlagsSet(g_event_flags, EVENT_XXX)
    ↓ 立即返回

主任务（main loop）
    ↓ osEventFlagsWait(g_event_flags, ALL_EVENTS, osFlagsWaitAny, osWaitForever)
    ↓ 唤醒 → switch 处理业务逻辑 → 回到等待
    ↓ 无事件时 CPU 占用 0%
```

**事件标志位定义**：

| 标志位 | 值 | 触发源 | 含义 |
|--------|-----|--------|------|
| `EVENT_ALARM_START` | `1 << 0` | 0x01 寻物命令 | 启动蜂鸣器+LED，广播status→0x01 |
| `EVENT_ALARM_STOP` | `1 << 1` | 0x00 停止命令 | 关闭声光，广播status→0x00 |
| `EVENT_INVENTORY` | `1 << 2` | 0x02 盘点命令 | Notify回复当前数据 |
| `EVENT_UPDATE_QTY` | `1 << 3` | 0x10 更新数量 | 更新qty，刷新广播 |
| `EVENT_BIND_TAG` | `1 << 4` | 0x20 绑定命令 | 写NV绑定tag_id |
| `EVENT_UNBIND_TAG` | `1 << 5` | 0x21 解绑命令 | 清除tag_id+qty |

---
## 二、目录结构
```
My_project_2x/
├── CLAUDE.md               # 本文件：开发指南
├── CMakeLists.txt           # 构建入口，注入源文件和头文件
├── Kconfig                  # 顶层配置菜单（GPIO/PWM/定时器参数）
├── app/
│   └── main.c               # 应用入口 + 业务逻辑编排层
├── components/
│   ├── shared_protocol/     # [契约层] 命令码定义、序列化/反序列化
│   ├── hardware_hal/        # [驱动层] LED(GPIO) + 蜂鸣器(PWM) + 自动关闭定时器
│   ├── sle_slave/           # [通信层] SLE广播、SSAP服务、连接管理、Notify
│   └── storage_sync/        # [存储层] NV持久化 + 广播payload同步
└── docs/                    # 设计文档、协议规范、修改记录
```

### 模块依赖关系
```
main.c (业务编排 + 事件循环)
  ├── osEventFlagsWait()   ← LiteOS 原生阻塞等待，0% CPU
  ├── hardware_hal        ← 声光控制（PWM硬件驱动，CPU可休眠）
  ├── sle_slave           ← SLE协议栈（回调中只设标志位，不执行业务）
  │     └── shared_protocol ← 广播编码序列化
  ├── storage_sync        ← 数据持久化
  │     └── shared_protocol ← adv_field 结构体定义
  └── shared_protocol     ← 命令解析
```

**核心原则**：
- `shared_protocol` 是契约中心，所有模块通过它交换数据结构，避免直接耦合
- SLE 回调只做一件事：`osEventFlagsSet()`，不阻塞 bt_service 任务
- 主循环用 `osEventFlagsWait(osWaitForever)` 纯阻塞，无事件时 0% CPU
--
## 三、协议规范

### 3.1 广播数据结构（12字节）
```c
#pragma pack(push, 1)
typedef struct {
    uint32_t magic;      // 0xAABBCCDD（大端序，用于过滤BS2x设备）
    uint16_t tag_id;     // 标签ID（配网时由WS63写入，默认0）
    uint16_t qty;        // 当前数量
    uint8_t  status;     // 0x00=空闲, 0x01=寻物中, 0x02=使用中, 0x03=未配网
    uint8_t  battery;    // 电量百分比（0~100）
    uint16_t seq;        // 序列号（每次更新递增）
} shared_proto_adv_field_t;
#pragma pack(pop)
```
**端序要求**：所有多字节字段必须按**大端序**（网络字节序）序列化，使用 `proto_write_u16_be()` / `proto_write_u32_be()` 辅助函数。ARM Cortex-M 为小端模式，禁止直接 `memcpy` 结构体到广播buffer。
### 3.1.1 静态广播载荷（零拷贝优化）
广播 payload 使用全局静态 buffer，编译时分配在数据区，地址固定，避免内存碎片：
``
g_adv_payload[16]（全局静态区，地址固定）
┌──────────────────────────────────────────────────────────────┐
│ AD头部 │ 厂商ID │ magic(4B) │ tag_id(2B) │ qty(2B) │ status │ battery │ seq(2B) │
│ [0-1]  │ [2-3]  │ [4-7]     │ [8-9]      │ [10-11] │ [12]   │ [13]    │ [14-15] │
└──────────────────────────────────────────────────────────────┘
```

**偏移量宏定义**（单字节直接修改，零拷贝）：

| 字段 | 偏移量 | 修改方式 |
|------|--------|---------|
| tag_id | `ADV_OFFSET_TAG_ID = 8` | `g_adv_payload[8..9] = proto_write_u16_be` |
| qty | `ADV_OFFSET_QTY = 10` | `g_adv_payload[10..11] = proto_write_u16_be` |
| status | `ADV_OFFSET_STATUS = 12` | `g_adv_payload[12] = new_status` |
| battery | `ADV_OFFSET_BATTERY = 13` | `g_adv_payload[13] = new_battery` |
| seq | `ADV_OFFSET_SEQ = 14` | `g_adv_payload[14..15] = proto_write_u16_be` |

**刷新流程**：原地修改 buffer → Stop → `sle_set_announce_data(指针)` → Start（微秒级完成）

### 3.2 SSAP命令码

| 命令码 | 含义 | 数据格式 | 回复 | status变化 |
|--------|------|---------|------|-----------|
| 0x00 | 停止寻物 | `[0x00]` | 无 | 恢复原状态（根据tag_id和qty判断） |
| 0x01 | 寻物 | `[0x01]` | 无 | 广播status→0x01，15s后自动恢复 |
| 0x02 | 盘点 | `[0x02]` | Notify `[0x82, tag_id(2B), qty(2B), status, battery, seq(2B)]` 共9字节 | 无变化 |
| 0x10 | 更新数量 | `[0x10, qty_hi, qty_lo]` | 无 | qty>0→0x02, qty=0→0x00 |
| 0x20 | 绑定tag_id | `[0x20, tag_id_hi, tag_id_lo]` | Notify `[0xA0, tag_id(2B)]` 成功 / `[0xAF, tag_id(2B)]` 失败 | 绑定成功→0x00或0x02 |
| 0x21 | 解绑标签 | `[0x21]` | Notify `[0xA1, old_tag_id(2B)]` 成功 / `[0xAF, old_tag_id(2B)]` 失败 | 解绑后→0x03 |

### 3.3 status字段含义（与WS63端统一）

| 值 | 含义 | 触发条件 | 说明 |
|----|------|---------|------|
| 0x00 | IDLE（空闲） | 默认/寻物恢复/qty=0 | tag_id≠0但无库存 |
| 0x01 | FINDING（寻物中） | 收到0x01寻物命令 | 15秒后自动恢复 |
| 0x02 | IN_USE（使用中） | tag_id≠0且qty>0 | 正常工作状态 |
| 0x03 | UNBOUND（未配网） | tag_id==0 | 首次上电或解绑后 |

### 3.4 NV存储Key分配

| Key | 用途 | 说明 |
|-----|------|------|
| 0x3000 | MAC地址 | 6字节，首次上电生成，永久保存 |
| 0x3001 | tag_id | 2字节，配网时写入，永久保存 |

---

## 四、编码规范

### 4.1 日志规范

**统一格式**：`[模块前缀][BP] 操作描述 key1=value1 key2=value2`

| 模块 | 日志前缀 | 覆盖范围 |
|------|---------|---------|
| shared_protocol | `[BS2x_PROTO][BP]` | 命令码/数据长度/原始hex/解析结果/magic校验 |
| sle_slave_mgr | `[BS2x_SLE][BP]` / `[BS2x_INIT][BP]` | SSAP注册/广播启停/连接增删/Write回调/Notify/MAC生成 |
| storage_sync | `[BS2x_SYNC][BP]` | NV读写/publish/qty变更/find状态/tag_id绑定 |
| hardware_hal | `[BS2x_HAL][BP]` | GPIO/PWM操作/定时器启停/自动关闭/初始化 |
| main.c | `[BS2x_APP][BP]` | 命令分发/PM状态转换/定时器/Notify回复 |

**日志原则**：
1. 每个关键操作必须打印成功和失败两种情况
2. 失败日志必须包含错误码：`ret:0x%x`
3. 禁止在日志中打印敏感数据（如密钥）

### 4.2 命名规范

| 类型 | 规范 | 示例 |
|------|------|------|
| 模块前缀 | `my_project_2x_` | `my_project_2x_entry()` |
| 静态全局变量 | `g_` 前缀 | `g_hw`, `g_sync`, `g_cb` |
| 配置宏 | `CONFIG_MY_PROJECT_2X_` | `CONFIG_MY_PROJECT_2X_LED_GPIO` |
| 状态枚举 | 模块名_描述 | `HW_HAL_OK`, `STORAGE_SYNC_STATUS_FINDING` |
| 回调函数 | 模块_事件_cbk | `sle_slave_announce_enable_cbk()` |
| Kconfig选项 | `MY_PROJECT_2X_` | `MY_PROJECT_2X_BEEP_AUTO_OFF_MS` |

### 4.3 错误处理

- 函数返回 `errcode_t` 或模块自定义状态码
- 调用底层API后必须检查返回值
- 失败时打印日志并返回错误码，不静默忽略
- `main.c` 中对非关键初始化失败可打印警告继续运行

### 4.4 端序处理

**强制规则**：所有协议数据的序列化/反序列化必须使用显式端序函数：

```c
// 写入（大端序）
proto_write_u16_be(buf, value);
proto_write_u32_be(buf, value);

// 读取（大端序）
uint16_t val = proto_read_u16_be(buf);
uint32_t val = proto_read_u32_be(buf);
```

**禁止**：直接 `memcpy` 结构体到协议buffer（ARM小端会导致端序反转）。

### 4.5 LiteOS 事件驱动规范

**强制规则**：主循环必须使用 LiteOS 原生 API，禁止轮询：

```c
// 创建事件标志组（初始化阶段）
osEventFlagsId_t g_event_flags = osEventFlagsNew(NULL);

// 回调/中断上下文：设置标志位（非阻塞，<1μs）
osEventFlagsSet(g_event_flags, EVENT_ALARM_START);

// 主循环：纯阻塞等待（0% CPU，直到有事件到达）
uint32_t flags = osEventFlagsWait(g_event_flags, ALL_EVENTS, osFlagsWaitAny, osWaitForever);
```

**禁止**：
- 禁止在主循环使用 `while(1) { msleep(50); }` 轮询
- 禁止在回调中执行业务逻辑（只允许 `osEventFlagsSet`）
- 禁止使用 `volatile` 标志位 + 轮询替代事件标志组

**回调职责边界**：SLE 回调（bt_service 任务上下文）只做：
1. 解析命令（`shared_proto_parse_unicast_cmd`）
2. 保存参数到全局变量（`g_cmd`, `g_conn_id`）
3. `osEventFlagsSet(g_event_flags, EVENT_XXX)`
4. 立即返回

### 4.6 安全保护

- **硬件安全**：蜂鸣器/LED必须有自动关闭定时器，防止电池耗尽
- **PM兼容**：进入低功耗前必须关闭所有外设（beep_off + led_off）
- **连接安全**：tag_id写入需校验（当前tag_id==0时才允许新绑定）
- **buffer安全**：序列化前必须校验buffer长度

---

## 五、构建与配置

### 5.1 编译命令

```bash
# 编译固件
./build.py standard-bs21e-1100e -c

# 进入 menuconfig 配置界面
./build.py standard-bs21e-1100e menuconfig
```

### 5.2 启用项目

在 menuconfig 中或 defconfig 中设置：

```
CONFIG_SAMPLE_SUPPORT_MY_PROJECT_2X=y
```

### 5.3 Kconfig配置项

| 配置项 | 默认值 | 说明 |
|--------|--------|------|
| `MY_PROJECT_2X_LED_GPIO` | 9 | LED GPIO编号 |
| `MY_PROJECT_2X_BEEP_GPIO` | 8 | 蜂鸣器GPIO编号（备用） |
| `MY_PROJECT_2X_BEEP_AUTO_OFF_MS` | 15000 | 蜂鸣器安全超时(ms) |
| `MY_PROJECT_2X_PWM_PIN` | 20 | PWM引脚（无源蜂鸣器） |
| `MY_PROJECT_2X_PWM_PIN_MODE` | 40 | PWM引脚复用模式 |
| `MY_PROJECT_2X_PWM_CHANNEL` | 0 | PWM通道 |
| `MY_PROJECT_2X_PWM_GROUP_ID` | 0 | PWM分组（V151） |
| `MY_PROJECT_2X_ADV_INTERVAL_MS` | 500 | 广播间隔(ms)，适配32标签场景 |

---

## 六、开发流程规范


## 七、当前开发方向

### 7.1 近期待完成

| 优先级 | 任务 | 说明 |
|--------|------|------|
| P0 | LiteOS事件驱动重构 | `osEventFlagsWait` 替换轮询，主循环 0% CPU ✅ |
| P0 | 静态广播载荷 | 零拷贝静态 buffer，偏移量直接修改，解决内存碎片 ✅ |
| P0 | 蜂鸣器PWM频率修复 | `BUZZER_PWM_LOW_TIME/HIGH_TIME` 从 100 改为 8000（2kHz）✅ |
| P0 | 与WS63联调 | 验证完整链路：WS63→BS21E |
| P0 | 入库功能 | RSSI选标签 + 绑定tag_id + TF卡存储 |
| P1 | 硬件PWM+定时器寻物 | CPU休眠，硬件独立输出方波+定时关闭 |
| P1 | 电池ADC采集 | 替换硬编码battery=100，实现真实电量读取 |
| P2 | NV Flash防磨损 | qty只存RAM，定期/低电刷新Flash（延后到OTA阶段） |

### 7.2 低功耗状态机

```
Work ──(5s无活动)──> Standby ──(30s无活动)──> Sleep
  ^                    │                        │
  └──────(SLE命令)─────┘                        │
  └──────────────(SLE命令)──────────────────────┘
```

- Work→Standby：关闭声光外设
- Standby→Sleep：停止SLE广播
- 唤醒：重新启动SLE广播

### 7.3 硬件 PWM + 定时器寻物优化

寻物期间 CPU 可休眠，由硬件独立完成声光输出：

```
收到 0x01 寻物命令（主循环处理）：
  1. 配置 PWM 硬件输出 2kHz 方波 → 独立运行，不需要CPU
  2. 配置硬件定时器 = 15s → 独立运行，不需要CPU
  3. CPU 进入 Standby/Sleep（PWM + Timer 由硬件时钟驱动）

15秒后：
  硬件定时器中断 → ISR 关闭 PWM + osEventFlagsSet(EVENT_ALARM_STOP)
  → CPU 醒来，主循环处理后续逻辑
```

| 对比 | 当前 | 优化后 |
|------|------|--------|
| CPU 状态 | 运行 15s（忙等） | 休眠 15s |
| PWM 输出 | 软件轮询驱动 | 硬件自动输出 |
| 定时器 | `osal_timer`（软件） | 硬件 Timer/RTC 中断 |
| 功耗 | 高（CPU + PWM） | 低（仅 PWM + Timer） |

### 7.4 已确认事项

| 项目 | 确认结果 | 说明 |
|------|---------|------|
| 连接间隔单位 | 0.625ms/slot | WS63端确认，0x64=62.5ms |
| 广播间隔 | 500ms（0x0320） | 32标签场景，64次回调/秒 |
| status语义 | 0=空闲,1=寻物,2=使用中,3=未配网 | 与WS63端统一 |
| sle_set_announce_data | 拷贝buffer | 静态buffer方案可行 |

### 7.5 待确认事项

- PWM 外设在 Standby/Sleep 下是否继续输出
- 多连接下的广播刷新策略（当前：停止→更新→重启）

### 7.6 入库功能设计（RSSI选标签）

#### 7.6.1 功能概述

串口屏点击"入库"时，WS63通过SLE扫描获取周围标签的RSSI值，自动选择信号最强的未绑定标签，分配tag_id并存储到TF卡。

#### 7.6.2 架构流程

```
┌──────────────────────┐         ┌──────────────┐
│       WS63           │  SLE    │    BS21E     │
│  (串口屏 + 网关)     │ ◄─────► │  (标签群)    │
│                      │         │              │
│  1. 扫描RSSI         │         │  持续广播    │
│  2. 过滤未绑定标签    │         │  (含status)  │
│  3. 选信号最强标签    │         │              │
│  4. 连接 + 绑定      │ ──────► │  接收绑定    │
│  5. 收到tag_id确认   │ ◄────── │  回复tag_id  │
│  6. 存储到TF卡       │         │              │
└──────────────────────┘         └──────────────┘
```

#### 7.6.3 详细步骤

| 步骤 | 执行方 | 操作 | 说明 |
|------|--------|------|------|
| 1 | WS63 | SLE扫描 | 获取所有标签广播包（含RSSI值） |
| 2 | WS63 | 过滤标签 | 只选status=3(UNBOUND)或status=0(IDLE) |
| 3 | WS63 | RSSI排序 | 选信号最强的标签（最近距离） |
| 4 | WS63→BS21E | 连接+绑定 | 发送0x20命令+tag_id（从池中分配） |
| 5 | BS21E→WS63 | 回复确认 | Notify [0xA0, tag_id(2B)] |
| 6 | WS63 | TF卡存储 | 存储：tag_id + MAC + RSSI + 时间戳 |

#### 7.6.4 Status字段与绑定状态

| status | 含义 | 是否可绑定 | 说明 |
|--------|------|-----------|------|
| 0x00 | IDLE（空闲） | ✅ 可绑定 | 已配网但未使用，可直接分配 |
| 0x01 | FINDING（寻物中） | ❌ 不可 | 正在寻物，不可操作 |
| 0x02 | IN_USE（使用中） | ❌ 不可 | 已绑定，有库存 |
| 0x03 | UNBOUND（未配网） | ✅ 可绑定 | 完全空闲，需先配网 |

#### 7.6.5 WS63端职责

| 功能 | 说明 |
|------|------|
| RSSI扫描 | 扫描广播包，记录每个标签的RSSI值和MAC地址 |
| 标签过滤 | 只选status=3(UNBOUND)或status=0(IDLE)的标签 |
| tag_id池 | 维护可用tag_id列表，按顺序分配（1~65535） |
| TF卡存储 | 绑定成功后存储：tag_id, MAC, RSSI, 时间戳, 状态 |

#### 7.6.6 BS21E端职责（已实现）

| 功能 | 对应命令 | 状态 |
|------|---------|------|
| 接收绑定命令 | 0x20 | ✅ 已实现 |
| 保存tag_id到NV | storage_sync_set_tag_id() | ✅ 已实现 |
| 回复tag_id确认 | Notify [0xA0, tag_id] | ✅ 已实现 |
| status自动更新 | UNBOUND→IDLE/IN_USE | ✅ 已实现 |

#### 7.6.7 TF卡数据结构（建议）

```json
{
  "tags": [
    {
      "tag_id": 1,
      "mac": "AA:BB:CC:DD:EE:FF",
      "rssi": -45,
      "bind_time": "2026-05-27 18:00:00",
      "status": "IDLE",
      "qty": 0,
      "location": "A区-1架-3层"
    }
  ]
}
```

---

## 八、关键文件速查

| 文件 | 职责 | 关键函数 |
|------|------|---------|
| `app/main.c` | 业务编排 + 事件循环 | `my_project_2x_entry()`, `osEventFlagsWait()`, `my_project_2x_exec_cmd()` |
| `components/shared_protocol/shared_protocol.c` | 协议解析 | `shared_proto_parse_unicast_cmd()`, `shared_proto_serialize_*()` |
| `components/hardware_hal/hardware_hal.c` | 声光驱动 | `hardware_hal_beep_on_for_ms()`, `hardware_hal_led_on_for_ms()` |
| `components/sle_slave/sle_slave_mgr.c` | SLE通信 | `sle_slave_init()`, `sle_slave_refresh_adv_payload()`, `g_adv_payload[]` |
| `components/storage_sync/storage_sync.c` | 数据存储 | `storage_sync_publish()`, `storage_sync_set_qty()` |

---

## 九、参考文档

| 文档 | 路径 | 内容 |
|------|------|------|
| 协同协议规范 | `docs/ws63_cooperation_spec.md` | WS63-BS21E完整协议规范 |
| 开发进度 | `docs/progress.md` | 迭代记录、断点日志覆盖、待开发项 |
| 修复报告 | `docs/fix_report_v3.md` | 端序修复、PWM蜂鸣器、PM兼容 |
| 审核回复 | `docs/bs21e_review_reply.md` | 对WS63端审核反馈的逐项回复 |
| SDK开发指南 | `SLE_2X_SDK_业务逻辑与二次开发指南.md` | SDK API参考 |
| 调试指南 | `docs/debug/debug_guide.md` | 串口调试、UART命令、常见问题排查 |

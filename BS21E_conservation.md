Hi，为了提前对齐WS63和BS21E的星闪基础链路配置，避免联调时反复踩坑，麻烦你按照下面的清单逐项验证BS21E端的代码，并**输出你那边实际定义的配置值**，我会和WS63端做一一核对，不一致的地方我们同步修改。

---

## 【必须输出】BS21E端基础配置核对表
请直接复制下面的表格，在「BS21E实际值」列填写你代码里的真实配置，有疑问的地方可以在备注里说明：

| 模块 | 检查项 | WS63端预期值 | BS21E实际值 | 备注（如有疑问请填写） |
|------|--------|--------------|-------------|------------------------|
| 🔴 最高优先级：协议一致性 | 广播数据魔数 | `0xaabbccdd`（暂定，如果你有更合适的魔数也可以提） | `0xAABBCCDD`（小端存储，内存字节序为 DD CC BB AA） | 已通过 `_Static_assert` 编译检查，结构体大小12字节 |
| | 广播数据结构体 | **必须和`shared_protocol.h`完全一致**（包括字段顺序、大小、`__attribute__((packed))`字节对齐） | `shared_proto_adv_field_t` 使用 `#pragma pack(push, 1)` 1字节对齐，字段：magic(u32)+tag_id(u16)+qty(u16)+status(u8)+battery(u8)+seq(u16)=12字节 | 已添加 `_Static_assert(sizeof(...) == 12)` 编译时检查 |
| | 结构体大小编译检查 | 建议添加：`_Static_assert(sizeof(shared_proto_adv_field_t) == 12, "结构体大小不一致")` | ✅ 已添加 | 避免因字节对齐问题导致解析错误 |
| 📡 广播模块 | 广播间隔最小值 | `160`（单位：0.625ms → 100ms） | `0xC8`（200×0.125ms=25ms） | ⚠️ 与WS63预期不一致，需同步修改为160 |
| | 广播间隔最大值 | `320`（单位：0.625ms → 200ms） | `0xC8`（200×0.125ms=25ms） | ⚠️ 与WS63预期不一致，需同步修改为320 |
| | 广播信道 | `SLE_ADV_CHANNEL_MAP_ALL`（全信道） | `0x07`（全信道37/38/39） | ✅ 一致 |
| | 广播功率 | `0dBm` | `0dBm`（`announce_tx_power = 0`） | ✅ 一致 |
| | 广播启动API | 调用海思官方SDK的`sle_set_announce_param()` + `sle_start_announce()` | ✅ 使用海思官方API | |
| 📡 广播数据格式 | announce_data | 需包含DISCOVERY_LEVEL+ACCESS_MODE+Manufacturer Data | ✅ 已添加 DISCOVERY_LEVEL(0x01)+ACCESS_MODE(0x02)+Manufacturer Data | 之前缺少前两个字段导致WS63扫描不上 |
| | seek_rsp_data | 需包含TX_POWER_LEVEL+LOCAL_NAME | ✅ 已添加 TX_POWER_LEVEL(0x0C)+"BS2x_Tag" | 之前为NULL，WS63无法识别设备名称 |
| 🔗 连接模块 | 连接监听窗口 | **保持海思SDK默认（2ms）** | 保持SDK默认 | ✅ 一致 |
| | 连接状态回调API | 注册海思官方SDK的`sle_connection_callbacks_t` | ✅ 已注册 `sle_connection_register_callbacks()` | |
| | 断链恢复 | 连接断开后自动调用`sle_start_announce()`恢复广播 | ✅ `sle_slave_connect_state_changed_cbk` 中 DISCONNECTED 状态不主动重启广播，由 `sle_slave_start()` 在 PM 回调中处理 | |
| 🔐 安全认证 | 安全模式 | `SLE_SECURITY_MODE_ENCRYPTED`（启用加密） | ❌ 暂未启用，等demo跑通后再启用 | 先跑通基础链路再加安全认证 |
| | 配对方式API | 海思官方SDK的`SLE_PAIRING_TYPE_JUST_WORKS`（简单配对） | ❌ 暂未实现 | 同上 |
| 📦 SSAP服务 | 服务UUID | `0000ff00-0000-1000-8000-00805f9b34fb` | ✅ `g_service_uuid` 128-bit UUID | |
| | 读写特征UUID | `0000ff01-0000-1000-8000-00805f9b34fb` | ✅ `g_property_uuid` 128-bit UUID | |
| | 特征权限 | 可写+可通知 | `SSAP_PERMISSION_READ | SSAP_PERMISSION_WRITE` | ⚠️ 缺少 NOTIFY 权限，需补充 |
| | SSAP服务注册API | 调用海思官方SDK的`sle_ssaps_register_service()` | ✅ 使用 `ssaps_register_server()` + `ssaps_add_service_sync()` + `ssaps_add_property_sync()` + `ssaps_start_service()` | |
| 🔋 业务数据（临时方案） | 电量读取 | 暂时用模拟值，默认`battery=95` | 默认 `battery=100` | ⚠️ 需同步为95 |
| | 供电方式 | 电池供电 | 电池供电 | ✅ 一致 |

---

## 【必须输出】BS21E端独立验证板块
请你自己先做以下验证，确保BS21E端能独立正常工作：
1. ✅ 给BS21E上电，看是否有**所有模块初始化成功**的日志
2. ✅ 看是否有**广播启动成功**的日志（类似：`[BS2x_SLE] announce enable cb id:1 status:0x0 started:1`）
3. ✅ 看心跳日志是否正常（类似：`[BS2x_APP] Heartbeat: Adv is running...`）
4. ✅ 确认没有任何**报错日志**（比如`广播参数设置失败`、`SSAP服务注册失败`等）

---

## 【必须输出】BS21E端预验证日志
请提供BS21E上电后**前10秒的完整串口日志**，需要包含：
1. 所有模块初始化日志
2. 广播启动成功日志
3. 至少3条心跳日志
4. 所有报错日志（如果有）

---

## 补充说明（先回答你的疑问）
1. **关于魔数**：`0xaabbccdd`只是暂定的，主要用于过滤杂波，你有更合适的也可以提，我们统一改
2. **关于全信道**：星闪有3个专用广播信道（37/38/39），全信道就是同时在这3个信道发广播，WS63扫描成功率更高
3. **关于广播功率**：功率越大通信距离越远，但功耗也越高，0dBm是平衡距离和功耗的选择，你觉得不合适我们可以调
4. **关于UUID**：UUID是星闪协议标准要求的128位格式，主要用于唯一标识服务和特征，你可以理解为：
   - 服务UUID = "服务的名字"（比如"星闪标签服务"）
   - 特征UUID = "服务里的功能通道"（比如"数据读写通道"）
5. **关于监听窗口**：这是海思芯片硬件原生支持的，我们只需要调用官方的广播API就行，不需要写任何额外的时隙调度代码
6. **关于ADC电量**：暂时先用模拟值`battery=95`，等基础链路跑通了我们再一起加ADC驱动
7. **关于API**：所有API都请用海思官方BS21E SDK里的，不要自己封装，避免兼容性问题

辛苦你了！全部完成后把表格、独立验证结果和日志发给我，我们核对无误后就可以开始联调了，有任何问题随时沟通。

---

## 修复备份（2026-04-28）

### 问题现象
- My_project_2x 编译失败，主要报错为 SLE 相关错误码未定义，以及回调类型未识别。

### 本次修复
- 在 [sle_slave_mgr.c](application/samples/peripheral/My_project_2x/components/sle_slave/sle_slave_mgr.c) 增加包含头文件：
   - sle_slave_mgr.h
   - sle_errcode.h

### 验证结果
- 使用命令 `./build.py standard-bs21e-1100e -c` 重新编译，构建成功并完成打包流程。

---

## 补丁记录 v2.0（2026-04-28）

### 一、编译系统修复

#### 问题1：Kconfig 循环依赖
- **现象**：`menuconfig` 报错 `Dependency loop: SAMPLE_SUPPORT_MY_PROJECT_2X depends on SAMPLE_SUPPORT_MY_PROJECT_2X`
- **原因**：`My_project_2x/Kconfig` 用 `menuconfig` 重复定义了 `SAMPLE_SUPPORT_MY_PROJECT_2X`，而 `peripheral/Kconfig` 已经定义了 `config SAMPLE_SUPPORT_MY_PROJECT_2X`
- **修复**：将 `My_project_2x/Kconfig` 中的 `menuconfig SAMPLE_SUPPORT_MY_PROJECT_2X` 改为 `if SAMPLE_SUPPORT_MY_PROJECT_2X`，只保留子配置项

#### 问题2：CMakeLists.txt 格式不匹配
- **现象**：源文件被编译但头文件找不到（`hardware_hal.h: No such file or directory`）
- **原因**：最初使用 `build_component()` 模式，但与 `peripheral/CMakeLists.txt` 的 `add_subdirectory_if_exist` 模式不兼容
- **修复**：改为 sle_uart 风格的 `SOURCES_LIST` + `PARENT_SCOPE` 模式，并在 `peripheral/CMakeLists.txt` 中添加 `include_directories` 设置头文件搜索路径

#### 问题3：main.c 头文件路径错误
- **现象**：`#include "../components/hardware_hal/hardware_hal.h"` 带有乱码字符导致编译失败
- **修复**：改为 `#include "hardware_hal.h"`，依赖 CMake 的 `include_directories` 解析路径

#### 问题4：ERRCODE_SLE_SUCCESS 未定义
- **现象**：`'ERRCODE_SLE_SUCCESS' undeclared`
- **修复**：BS21E SDK 中使用 `ERRCODE_SUCC`，统一替换

### 二、定时器修复

#### 问题：`timer create failed ret=0x80001320`
- **错误码**：`0x80001320` = `ERRCODE_TIMER_NO_ENOUGH`（硬件定时器资源不足）
- **原因**：`uapi_timer_create(TIMER_INDEX_0, ...)` 使用硬件定时器 TIMER_INDEX_0，该定时器已被系统占用
- **修复**：改用 `osal_timer`（软件定时器），不受硬件资源限制
  - `uapi_timer_create` → `osal_timer_init`
  - `uapi_timer_start` → `osal_timer_mod` + `osal_timer_start`
  - `uapi_timer_stop` → `osal_timer_stop`
- **文件**：[hardware_hal.c](application/samples/peripheral/My_project_2x/components/hardware_hal/hardware_hal.c)

### 三、广播数据修复（⚠️ 关键修复）

#### 问题：WS63 扫描不到 BS21E
- **原因**：广播数据只包含 Manufacturer Data，缺少 SLE 协议必需的 `DISCOVERY_LEVEL` 和 `ACCESS_MODE` 字段；`seek_rsp_data` 为 NULL，缺少设备名称
- **修复**：参照 `sle_uart_server_adv.c`，在 `sle_slave_update_announce_data()` 中添加完整广播数据结构：

| 数据段 | 类型 | 值 |
|--------|------|-----|
| announce_data[0-2] | DISCOVERY_LEVEL (0x01) | SLE_ANNOUNCE_LEVEL_NORMAL |
| announce_data[3-5] | ACCESS_MODE (0x02) | 0x00 |
| announce_data[6...] | Manufacturer Data (0xFF) | 厂商ID(0x5A,0xA5) + shared_proto_adv_field_t(12字节) |
| seek_rsp_data[0-2] | TX_POWER_LEVEL (0x0C) | 0x00 |
| seek_rsp_data[3...] | COMPLETE_LOCAL_NAME (0x0B) | "BS2x_Tag" |

- **文件**：[sle_slave_mgr.c](application/samples/peripheral/My_project_2x/components/sle_slave/sle_slave_mgr.c)

### 四、心跳循环

- **修改**：在 `my_project_2x_entry()` 末尾添加 `while(1)` 心跳循环
- **输出**：每2秒打印 `[BS2x_APP] Heartbeat: Adv is running...`
- **目的**：确认任务存活，观察广播状态
- **文件**：[main.c](application/samples/peripheral/My_project_2x/app/main.c)

### 五、Payload 封包调试

- **修改**：在 `sle_slave_encode_manufacturer_adv()` 封包后打印前4字节 HEX
- **输出**：`[BS2x_SLE] adv payload first 4 bytes: 0xDD 0xCC 0xBB 0xAA (expect 0xAA 0xBB 0xCC 0xDD)`
- **说明**：`0xAABBCCDD` 是 `uint32_t` 值，RISC-V 小端模式下内存字节序为 `DD CC BB AA`，这是正确的，WS63 端用 `uint32_t` 读取结果一致
- **文件**：[sle_slave_mgr.c](application/samples/peripheral/My_project_2x/components/sle_slave/sle_slave_mgr.c)

---

## 调试方法

### 1. 编译验证
```bash
cd /home/cool/fbb_bs2x/src
./build.py standard-bs21e-1100e -c
```

### 2. 检查代码是否编译进固件
```bash
grep -a "my_project_2x_entry\|BS2x_INIT\|sle_slave_init" output/bs21e/acore/standard-bs21e-1100e/application.nm
```
如果有输出，说明代码已编译进固件。

### 3. 串口日志关键标记
| 日志前缀 | 模块 | 关键信息 |
|----------|------|----------|
| `[BS2x_INIT]` | 主入口 | `Entering my_project_2x_entry` 表示任务已启动 |
| `[BS2x_HAL]` | 硬件HAL | `init done` 表示GPIO和定时器初始化成功 |
| `[BS2x_SLE]` | SLE从机 | `announce enable cb ... started:1` 表示广播已启动 |
| `[BS2x_SLE]` | SLE从机 | `adv payload first 4 bytes` 确认封包正确 |
| `[BS2x_SYNC]` | 存储同步 | `publish ok` 表示广播payload已刷新 |
| `[BS2x_APP]` | 心跳 | `Heartbeat: Adv is running...` 表示任务存活 |

### 4. 常见问题排查

| 问题 | 可能原因 | 排查方法 |
|------|----------|----------|
| 串口无 `[BS2x_INIT]` 输出 | 代码未编译进固件 | 检查 menuconfig 是否勾选 `Support My_project_2x`，检查 `application.nm` |
| `timer create failed ret=0x80001320` | 硬件定时器资源不足 | 已改用 `osal_timer`，如仍报错检查 LiteOS 定时器配置 |
| `sle init fail` | SLE 协议栈初始化失败 | 检查 SLE 相关 menuconfig 是否启用 |
| WS63 扫描不到 | 广播数据缺少必需字段 | 检查 `announce_data_len` 和 `seek_rsp_data_len` 日志是否非零 |
| `adv payload first 4 bytes` 显示 `DD CC BB AA` | 这是正确的小端字节序 | WS63 端用 `uint32_t` 读取即可得到 `0xAABBCCDD` |

---

## ⚠️ 注意事项

### 待同步配置（与WS63端不一致）
1. **广播间隔**：当前 `0xC8`（25ms），WS63预期 `160/320`（100ms/200ms），需同步
2. **电量默认值**：当前 `100`，WS63预期 `95`，需同步
3. **SSAP特征权限**：当前只有 READ+WRITE，缺少 NOTIFY，需补充
4. **安全模式**：暂未启用，等demo跑通后再启用
5. **配对回调**：暂未实现，等安全模式启用时一起加

### 编译系统注意
1. 修改 `peripheral/CMakeLists.txt` 或 `My_project_2x/CMakeLists.txt` 后，需要 `rm -rf output/` 重新生成 CMake 缓存
2. **不要删除 `build/` 目录**，它包含编译工具链脚本，删除后无法编译
3. menuconfig 配置保存在 `build/config/target_config/bs21e/menuconfig/acore/standard_bs21e_1100e.config`

### 文件结构
```
My_project_2x/
├── CMakeLists.txt          # 源文件列表（sle_uart风格）
├── Kconfig                 # 子配置项（GPIO、定时器等）
├── app/
│   └── main.c              # 主入口 + 心跳循环
└── components/
    ├── shared_protocol/    # 12字节广播契约结构体
    ├── hardware_hal/       # GPIO控制 + osal_timer自动关断
    ├── sle_slave/          # SLE从机：广播+SSAP服务+连接管理
    └── storage_sync/       # RAM库存管理 + 广播payload刷新    NV存储
```
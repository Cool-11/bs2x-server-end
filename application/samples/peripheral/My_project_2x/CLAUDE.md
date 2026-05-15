# CLAUDE.md — BS2x 智能标签固件开发指南

> 适用于：`application/samples/peripheral/My_project_2x/`
> 芯片平台：BS21E（SLE Server端）
> 系统角色：星闪多模态仓储管家系统 — 微功耗标签端

---

## 一、项目概述

### 1.1 系统三端架构

```
┌──────────┐    JSON/串口    ┌──────────┐    SLE     ┌──────────┐
│  ESP32   │ ◄────────────► │  WS63    │ ◄───────► │  BS21E   │
│ 串口屏   │                │  Client  │            │  Server  │
└──────────┘                └──────────┘            └──────────┘
```

- **ESP32**：用户交互界面（选区、添加数量、触发寻物）
- **WS63**：SLE通信、映射表管理、协议转发
- **BS21E**：标签数据存储、广播、寻物执行、低功耗管理

### 1.2 BS21E 端职责

1. 上电生成唯一MAC，持久化到NV，重启不变
2. 持续广播 `tag_id + qty + status + battery + seq`（12字节厂商数据）
3. 接收SSAP单播命令：寻物(0x01)、盘点(0x02)、更新数量(0x10)、绑定tag_id(0x20)
4. 无连接超时进入低功耗 Standby/Sleep

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
main.c (业务编排)
  ├── hardware_hal        ← 声光控制
  ├── sle_slave           ← SLE协议栈
  │     └── shared_protocol ← 广播编码序列化
  ├── storage_sync        ← 数据持久化
  │     └── shared_protocol ← adv_field 结构体定义
  └── shared_protocol     ← 命令解析
```

**核心原则**：`shared_protocol` 是契约中心，所有模块通过它交换数据结构，避免直接耦合。

---

## 三、协议规范

### 3.1 广播数据结构（12字节）

```c
#pragma pack(push, 1)
typedef struct {
    uint32_t magic;      // 0xAABBCCDD（大端序，用于过滤BS2x设备）
    uint16_t tag_id;     // 标签ID（配网时由WS63写入，默认0）
    uint16_t qty;        // 当前数量
    uint8_t  status;     // 0x00=正常, 0x01=寻物中, 0x02=出库
    uint8_t  battery;    // 电量百分比（0~100）
    uint16_t seq;        // 序列号（每次更新递增）
} shared_proto_adv_field_t;
#pragma pack(pop)
```

**端序要求**：所有多字节字段必须按**大端序**（网络字节序）序列化，使用 `proto_write_u16_be()` / `proto_write_u32_be()` 辅助函数。ARM Cortex-M 为小端模式，禁止直接 `memcpy` 结构体到广播buffer。

### 3.2 SSAP命令码

| 命令码 | 含义 | 数据格式 | 回复 |
|--------|------|---------|------|
| 0x00 | 停止寻物 | `[0x00]` | 无 |
| 0x01 | 寻物 | `[0x01]` | 无（广播status→0x01，15s后自动恢复0x00） |
| 0x02 | 盘点 | `[0x02]` | Notify `[0x82, tag_id(2B), qty(2B), status, battery, seq(2B)]` 共9字节 |
| 0x10 | 更新数量 | `[0x10, qty_hi, qty_lo]` | 无（广播qty实时同步，延迟<100ms） |
| 0x20 | 绑定tag_id | `[0x20, tag_id_hi, tag_id_lo]` | Notify `[0xA0, tag_id(2B)]` 成功 / `[0xAF, tag_id(2B)]` 失败 |
| 0x21 | 解绑标签 | `[0x21]` | Notify `[0xA1, old_tag_id(2B)]` 成功 / `[0xAF, old_tag_id(2B)]` 失败 |

### 3.3 status字段含义

| 值 | 含义 | 触发条件 |
|----|------|---------|
| 0x00 | NORMAL | 默认/寻物恢复 |
| 0x01 | FINDING | 收到0x01寻物命令 |
| 0x02 | OUTSTOCK | qty被设为0 |

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

### 4.5 安全保护

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

---

## 六、开发流程规范

### 6.1 修改代码前

1. **阅读现有代码**：先理解当前模块的实现逻辑和依赖关系
2. **查阅SDK API**：禁止凭经验猜测函数名，必须参考SDK官方示例
3. **确认协议兼容**：修改协议相关代码前，确认与WS63端的兼容性

### 6.2 修改代码后

1. **编译验证**：`./build.py standard-bs21e-1100e -c` 零错误零警告
2. **日志验证**：串口日志可追踪完整调用链
3. **边界处理**：NULL指针、buffer溢出、超时等场景
4. **更新文档**：修改协议或接口时同步更新 `docs/` 下相关文档

### 6.3 Git工程化使用指南

#### 6.3.1 分支策略

```
master (稳定发布)
  │
  ├── dev (日常开发主线)
  │     │
  │     ├── feature/battery-adc    (功能分支)
  │     ├── feature/multi-conn     (功能分支)
  │     └── fix/adv-endian         (修复分支)
  │
  └── release/v1.0.0 (发布分支)
```

| 分支 | 用途 | 命名规范 | 生命周期 |
|------|------|---------|---------|
| `master` | 稳定版本，随时可编译烧录 | 固定 | 永久 |
| `dev` | 日常开发集成分支 | 固定 | 永久 |
| `feature/*` | 新功能开发 | `feature/功能名` | 完成后合并到dev并删除 |
| `fix/*` | Bug修复 | `fix/问题描述` | 完成后合并到dev并删除 |
| `release/*` | 版本发布准备 | `release/v版本号` | 发布后合并到master并删除 |

#### 6.3.2 分支操作命令

```bash
# 查看当前分支和状态
git status
git branch -a                # 查看所有分支（含远程）

# 创建并切换到功能分支（从dev拉出）
git checkout dev
git pull origin dev           # 先同步远程最新
git checkout -b feature/battery-adc

# 切换分支
git checkout dev
git checkout feature/battery-adc

# 删除本地分支（已合并后）
git branch -d feature/battery-adc

# 删除远程分支
git push origin --delete feature/battery-adc
```

#### 6.3.3 提交规范（Conventional Commits）

**格式**：`<type>(<scope>): <subject>`

| type | 含义 | 示例 |
|------|------|------|
| `feat` | 新功能 | `feat(hardware_hal): 新增电池ADC采集` |
| `fix` | Bug修复 | `fix(shared_protocol): 修复广播端序错误` |
| `refactor` | 重构（不改变功能） | `refactor(sle_slave): 拆分连接管理逻辑` |
| `docs` | 文档更新 | `docs(progress): 更新第三轮迭代进度` |
| `chore` | 构建/工具变更 | `chore(kconfig): 新增PWM配置项` |
| `style` | 代码格式调整 | `style(main): 统一缩进和空格` |
| `test` | 测试相关 | `test(uart): 新增UART自测命令` |
| `perf` | 性能优化 | `perf(pm): 优化低功耗唤醒延迟` |

**scope**（可选）：`shared_protocol` / `hardware_hal` / `sle_slave` / `storage_sync` / `main` / `kconfig` / `docs`

**示例**：
```bash
git commit -m "feat(hardware_hal): 新增电池ADC采集，替换硬编码battery=100"
git commit -m "fix(sle_slave): 修复多连接下广播刷新时序竞争"
git commit -m "docs(ws63_cooperation_spec): 补充OTA升级流程说明"
```

#### 6.3.4 日常开发工作流

**场景1：开发新功能**

```bash
# 1. 从dev创建功能分支
git checkout dev
git pull origin dev
git checkout -b feature/battery-adc

# 2. 开发过程中频繁提交（小步提交）
git add components/hardware_hal/hardware_hal.c
git commit -m "feat(hardware_hal): 实现ADC初始化和单次采样"

git add components/hardware_hal/hardware_hal.h
git commit -m "feat(hardware_hal): 新增battery_adc_read()接口声明"

git add app/main.c
git commit -m "feat(main): 集成电池ADC读取到主循环"

# 3. 开发完成，合并回dev
git checkout dev
git pull origin dev                    # 先同步远程最新
git merge --no-ff feature/battery-adc  # 合并（保留分支历史）
git push origin dev

# 4. 删除功能分支
git branch -d feature/battery-adc
git push origin --delete feature/battery-adc
```

**场景2：修复紧急Bug**

```bash
# 1. 从dev创建修复分支
git checkout dev
git checkout -b fix/adv-endian

# 2. 修复并提交
git add components/shared_protocol/shared_protocol.c
git commit -m "fix(shared_protocol): 修复u32大端序列化字节顺序错误"

# 3. 合并回dev
git checkout dev
git merge --no-ff fix/adv-endian
git push origin dev
git branch -d fix/adv-endian
```

**场景3：代码写到一半需要切分支处理其他事情**

```bash
# 暂存当前工作（不生成提交）
git stash save "battery-adc: ADC初始化写到一半"

# 切换到其他分支处理事情
git checkout dev
# ... 处理完 ...

# 回来继续
git checkout feature/battery-adc
git stash pop    # 恢复暂存的内容

# 查看暂存列表
git stash list
```

#### 6.3.5 合并策略

| 方式 | 命令 | 效果 | 适用场景 |
|------|------|------|---------|
| `--no-ff` | `git merge --no-ff feature/xxx` | 强制生成合并提交，保留分支历史 | **推荐**：功能分支合并到dev |
| `--squash` | `git merge --squash feature/xxx` | 压缩为一个提交 | 小功能/修复，提交太碎时 |
| `rebase` | `git rebase dev` | 变基，线性历史 | 个人分支同步dev最新代码 |

```bash
# 推荐：保留分支历史的合并
git checkout dev
git merge --no-ff feature/battery-adc -m "feat: 合并电池ADC功能分支"

# 可选：压缩为单个提交（提交太碎时）
git checkout dev
git merge --squash feature/battery-adc
git commit -m "feat(hardware_hal): 完成电池ADC采集功能"

# 可选：变基（保持线性历史，个人分支用）
git checkout feature/battery-adc
git rebase dev
# 如果有冲突，解决后 git rebase --continue
```

#### 6.3.6 冲突处理

```bash
# 合并时遇到冲突
git merge --no-ff feature/xxx
# CONFLICT (content): Merge conflict in app/main.c

# 1. 查看冲突文件
git status

# 2. 打开冲突文件，找到冲突标记
# <<<<<<< HEAD
# 当前分支的代码
# =======
# 合并进来的代码
# >>>>>>> feature/xxx

# 3. 手动编辑，保留正确的代码，删除冲突标记

# 4. 标记冲突已解决
git add app/main.c

# 5. 完成合并
git commit -m "merge: 解决main.c冲突，保留双方修改"
```

#### 6.3.7 查看历史与追溯

```bash
# 查看提交历史（简洁模式）
git log --oneline -20

# 查看某个文件的修改历史
git log --oneline app/main.c

# 查看某次提交改了什么
git show <commit-hash>

# 查看某个函数是谁在什么时候改的（追溯）
git blame app/main.c

# 查看两个版本之间的差异
git diff dev..feature/battery-adc

# 查看工作区未提交的修改
git diff
git diff --staged    # 已add但未commit的
```

#### 6.3.8 撤销操作

```bash
# 撤销工作区的修改（未add）
git checkout -- app/main.c           # 撤销单个文件
git checkout -- .                     # 撤销所有

# 撤销已add的文件（从暂存区移回工作区）
git reset HEAD app/main.c

# 撤销最近一次提交（保留修改在工作区）
git reset --soft HEAD~1

# 撤销最近一次提交（修改也丢弃，慎用！）
git reset --hard HEAD~1

# 安全方式：生成一个新的提交来"反做"某个提交
git revert <commit-hash>
```

#### 6.3.9 .gitignore 配置

本项目应忽略的文件（确认 `.gitignore` 中已包含）：

```gitignore
# 编译产物
output/
build/
*.o
*.elf
*.bin
*.hex

# IDE配置
.vscode/
.idea/
*.swp
*.swo

# Python虚拟环境
.venv/
.venv-1/
__pycache__/

# 临时文件
*.tmp
*.bak
*.log
```

#### 6.3.10 版本标签

```bash
# 打标签（里程碑版本）
git tag -a v1.0.0 -m "BS2x v1.0.0: 基础功能完成（寻物/盘点/配网/低功耗）"

# 查看所有标签
git tag -l

# 推送标签到远程
git push origin v1.0.0
git push origin --tags    # 推送所有标签

# 基于标签创建发布分支
git checkout -b release/v1.0.0 v1.0.0
```

#### 6.3.11 常用场景速查表

| 场景 | 命令 |
|------|------|
| 查看当前状态 | `git status` |
| 查看改了什么 | `git diff` |
| 提交单个文件 | `git add <file> && git commit -m "..."` |
| 提交所有修改 | `git add -A && git commit -m "..."` |
| 同步远程最新 | `git pull origin dev` |
| 推送到远程 | `git push origin dev` |
| 暂存当前工作 | `git stash` |
| 恢复暂存 | `git stash pop` |
| 查看提交历史 | `git log --oneline -20` |
| 撤销未提交的修改 | `git checkout -- <file>` |
| 撤销最近提交(保留修改) | `git reset --soft HEAD~1` |

---

## 七、当前开发方向

### 7.1 近期待完成

| 优先级 | 任务 | 说明 |
|--------|------|------|
| P0 | 电池ADC采集 | 替换硬编码battery=100，实现真实电量读取 |
| P0 | 与WS63联调 | 验证完整链路：ESP32→WS63→BS21E |
| P1 | 多连接测试 | 验证4连接并发场景下的稳定性 |
| P1 | 广播数据包优化 | 确保广播payload在多连接下正确刷新 |
| P2 | 低功耗优化 | PM超时调优、实际功耗测量 |
| P2 | OTA升级 | 验证OTA流程中MAC/tag_id/NV数据保留 |

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

### 7.3 待确认事项

- 连接间隔单位（125μs 还是 0.25ms）
- 出库语义（仅清qty还是解绑）
- 多连接下的广播刷新策略

---

## 八、关键文件速查

| 文件 | 职责 | 关键函数 |
|------|------|---------|
| `app/main.c` | 业务编排 | `my_project_2x_entry()`, `my_project_2x_on_unicast_cmd()` |
| `components/shared_protocol/shared_protocol.c` | 协议解析 | `shared_proto_parse_unicast_cmd()`, `shared_proto_serialize_*()` |
| `components/hardware_hal/hardware_hal.c` | 声光驱动 | `hardware_hal_beep_on_for_ms()`, `hardware_hal_led_on_for_ms()` |
| `components/sle_slave/sle_slave_mgr.c` | SLE通信 | `sle_slave_init()`, `sle_slave_refresh_adv_payload()` |
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

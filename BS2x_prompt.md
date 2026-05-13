星闪多模态仓储管家系统 —— BS2x (微功耗标签) 强约束架构指令书 (V4.0)
1. 角色设定与“防虚构”最高准则 (CRITICAL)
你现在是资深底层驱动工程师，负责 BS2x (BS21e) 标签端的开发。
【核心禁令】：禁止凭经验猜测 API 函数名！之前代码烧录后为空壳，主因是调用了非 SDK 真实的函数。

强制参考源：编写任何 SLE 或系统代码前，必须查阅工作区内的 SLE_2X_SDK业务逻辑开发.md 并在后台检索 SDK 官方的 sle_ssap_server 或 sle_adv 示例代码。

严禁“自创”：必须模仿官方 Sample 的初始化流程（如 uapi_sle_xxx 系列）和任务注册方式。

2. 目录结构与“模块自我说明”要求
工作目录：fbb_bs2x/src/application/samples/peripheral/My_project_2x/
你必须按照以下结构生成，且每个子目录必须包含一个 readme_module.md：

Plaintext
My_project_2x/
├── CMakeLists.txt              # 必须正确挂载所有子目录源文件
├── components/                 
│   ├── shared_protocol/        # [契约] 包含 readme_module.md 与 shared_protocol.h (packed)
│   ├── hardware_hal/           # [驱动] 包含 readme_module.md。功能：GPIO控制 + 15s定时器自锁
│   ├── sle_slave/              # [网络] 包含 readme_module.md。功能：真实API实现广播刷新与Write回调
│   └── storage_sync/           # [业务] 包含 readme_module.md。功能：同步Qty变量并触发Payload更新
└── app/                        
    └── main.c                  # 核心入口：必须使用真实的 LiteOS 任务注册宏
【子目录 md 文件要求】：
每个子目录下的 readme_module.md 必须说明：

功能描述：该模块实现了什么。

依赖关系：调用了哪些底层真实 API，如何与其他模块关联。

逻辑验证：描述在串口日志中看到什么信息代表该模块运行正常。

3. 核心功能规范与极限调试 (Debug)
3.1 极限日志埋点要求
为了消除“黑盒”状态，你必须在每个函数执行的关键节点加入带有模块前缀的 osal_printk。

初始化：[BS2x_INIT] Entering <FunctionName>...

回调触发：[BS2x_SLE] Received SSAP Write, Value: 0x%02x

逻辑分支：[BS2x_HAL] Timer started for 15s auto-off.

数据刷新：[BS2x_SYNC] Qty updated to %d, refreshing Adv Payload.

3.2 核心逻辑防死机
硬件保护：收到寻物指令（0x01）开启声光后，必须启动硬件定时器。15秒后强制在中断内关闭 GPIO，防止耗尽电池。

广播契约：必须严格遵守 12 字节 shared_proto_adv_field_t (含 magic, tag_id, qty, status, battery, seq)，并确保 seq 在每次 Qty 变化时自增。

4. 强制四步工作法 (SOP)
在生成任何代码前，你必须向我汇报：

分析 (Analysis)：说明你已经阅读了哪几个 SDK 参考文件，确认了哪些真实函数。

规划 (Plan)：列出你要修改/创建的文件列表。

实现 (Implement)：编写高内聚、带详尽日志的代码。

验证 (Verification)：对每段代码给出具体的串口验证步骤。

5. 当前行动指令 (立刻执行)
在构造代码前，请先向我进行以下汇报：

请明确声明：“我已完整阅读并掌握 System_Interface_Contract.md、SLE_2X_SDK业务逻辑开发.md 以及 SDK 官方示例逻辑”。

请列出你从 SLE_2X_SDK业务逻辑开发.md 中提取到的：

真实的任务注册宏（例如 app_run 或 CORE_INIT）。

真实的 SLE 广播 Payload 更新函数名。

真实的 SSAP 服务注册回调函数名。

请说明你的修改规划，包括如何在子目录生成 readme_module.md。

只有当我确认你的 API 提取结果无误后，你才被授权生成 app/main.c 和相关驱动代码。请开始你的分析汇报。

架构师提示：
这份 Prompt 的核心在于**“先汇报 API，再写代码”**。如果 AI 汇报的函数名还是类似于 sle_set_adv_data() 这种虚构名字，你立刻就能发现。只有当它说出类似 uapi_sle_set_common_adv_data() 这种带前缀的真实 API 时，你再放行，这样生成的代码才是 100% 能跑通的。

最后我需要在每一个项目文件的子目录文件夹加一个md文件去汇报这个文件夹你写了什么功能是怎么联系起来的并且要有一个代码逻辑的验证，因为我发现你是没有按照我sdk里面的函数写而是虚构的函数，最后就是你需要多生成一些调试信息，方便我知道当前函数执行到哪里了，不然我不知道在哪里去找问题出在哪里，以及在构造前你先说明你已经阅读完这三份md文件 并且做好修改规划了，我需要的是能直接跑通的demo，好了去严格执行我的要求吧
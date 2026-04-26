# components

## 功能描述
- 组件层拆分为 `shared_protocol`、`hardware_hal`、`sle_slave`、`storage_sync` 四个模块。
- 通过最小依赖方式组装，避免业务逻辑与底层驱动耦合。

## 依赖关系
- `app` 依赖该目录下各模块完成完整链路。
- `shared_protocol` 是协议契约中心，其它模块均引用。

## 逻辑验证
- 任一命令执行时，串口可看到来自 SLE/HAL/SYNC 三个模块的前缀日志，说明模块协作正常。

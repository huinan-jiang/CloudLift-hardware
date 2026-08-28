# CloudLift ESP32 ↔ APP BLE 通信协议 v1.0

本文件是硬件固件和 APP 的共同接口规范。ESP32 负责实时动作和安全保护；APP 负责交互、参数配置和状态展示。APP 不直接控制单个电机。

## BLE 标识

- 广播名称：`CloudLift`
- Service UUID：`7e400001-b5a3-f393-e0a9-e50e24dcca9e`
- Command characteristic：`7e400002-b5a3-f393-e0a9-e50e24dcca9e`，APP 写入
- Telemetry characteristic：`7e400003-b5a3-f393-e0a9-e50e24dcca9e`，APP 读取并订阅 Notify
- 文本编码：UTF-8
- 数据格式：单个紧凑 JSON 对象，不带换行
- 单条命令最大长度：191 字节

## APP 下发命令

```json
{"cmd":"START"}
{"cmd":"PAUSE"}
{"cmd":"STOP"}
{"cmd":"SET_FORCE","value":3}
{"cmd":"SET_SPEED","value":3}
{"cmd":"SET_TIME","seconds":600}
{"cmd":"SET_MODE","value":0}
{"cmd":"GET_STATUS"}
{"cmd":"CLEAR_FAULT"}
```

- `force`：1～5，仅空闲状态可修改。
- `speed`：1～5，可运行时修改。
- `seconds`：10～3600，仅空闲状态可修改。
- `mode`：0 组合、1 提拉、2 揉捻、3 往复；v1 固件暂时统一执行组合动作，字段已为后续模式预留。
- `START`：空闲时开始；暂停时继续。
- `STOP`：停止动作并执行安全释放。
- `CLEAR_FAULT`：清除故障前必须先排除机械或传感器问题。
- `GET_STATUS`：v1 固件持续上报状态，因此无需额外应答。

## ESP32 状态上报

ESP32 每 500 ms Notify 一次：

```json
{"state":"massaging","pressure_left":1450,"pressure_right":1510,"limit_left":false,"limit_right":false,"force":3,"speed":3,"mode":0,"fault":"none","remaining_s":574}
```

`state` 可取：

- `idle`：空闲
- `clamping`：正在夹紧
- `massaging`：正在运行
- `paused`：暂停，电机停止
- `releasing`：正在释放
- `fault`：故障锁定

`fault` 当前可取：

- `none`
- `over_pressure`
- `limit_during_clamp`
- `clamp_timeout`

## APP 行为要求

1. 连接后立即订阅 Telemetry Notify。
2. 只有收到 `state=massaging` 才显示“运行中”。
3. 主操作页始终显示停止按钮；停止命令不能只依赖页面返回动作。
4. 蓝牙断开时立即显示“设备连接断开，设备正在安全释放”。ESP32 自行执行释放。
5. `fault` 不为 `none` 时显示故障原因，禁止自动重新启动。
6. APP 不根据压力值自行驱动或反转电机，压力只用于显示；安全决策属于 ESP32。

## v1 限制

- 压力值目前是 12 位 ADC 原始值，安装放大板并标定后再改为牛顿或等级值。
- 电量检测尚未接入，v1 状态包不包含电量。
- BLE 尚未配对加密，正式产品版应增加绑定、鉴权和 OTA 签名校验。

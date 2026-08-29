# CloudLift ESP32 ↔ APP BLE 通信协议 v2.1

当前默认固件是 `ble_motor_control/CloudLiftBleMotorControl.ino`。上电后所有电机停止；应变片只采样和上报，不参与电机控制。APP通过BLE命令控制位移电机和按摩电机。

## BLE标识

- 广播名称：`CloudLift`
- Service UUID：`7e400001-b5a3-f393-e0a9-e50e24dcca9e`
- Command characteristic（APP写入）：`7e400002-b5a3-f393-e0a9-e50e24dcca9e`
- Telemetry characteristic（APP读取/订阅Notify）：`7e400003-b5a3-f393-e0a9-e50e24dcca9e`
- 文本编码：UTF-8；每条命令是一个紧凑JSON对象，不带换行；最大192字节。

## 命令

### 位移电机组

```json
{"cmd":"MOVE","direction":-1}
{"cmd":"MOVE","direction":1}
{"cmd":"MOVE","direction":0}
```

`direction=-1`是已经验证的两台位移电机同时反转方向；`direction=1`是相反方向；`0`停止两台位移电机。

也支持简写：

```json
{"cmd":"MOVE_CLOSE"}
{"cmd":"MOVE_OPEN"}
{"cmd":"MOVE_STOP"}
```

### 按摩电机组

```json
{"cmd":"MASSAGE_START","speed":200}
{"cmd":"MASSAGE","speed":160}
{"cmd":"MASSAGE_STOP"}
```

`speed`范围为0～255。`MASSAGE_START`不依赖应变片，收到命令后立即启动两台按摩电机。

### 总控制和状态

```json
{"cmd":"STOP_ALL"}
{"cmd":"START"}
{"cmd":"PAUSE"}
{"cmd":"SET_SPEED","value":3}
{"cmd":"SET_GEAR","gear":2}
{"cmd":"SET_MODE","mode":0}
{"cmd":"PING"}
{"cmd":"GET_STATUS"}
```

- `START`：启动按摩电机，使用当前设定速度；不会自动运行位移电机。
- `PAUSE`：停止按摩电机，位移电机状态不变；APP需要暂停全部动作时应使用 `STOP_ALL`。
- `STOP_ALL`：停止四台电机。
- `SET_SPEED`：设置按摩速度等级1～5，对应PWM 120/150/180/210/240。
- `SET_GEAR`：档位1/2/3分别为默认速度、默认速度的1/2和1/3，也可使用 `{"cmd":"GEAR","value":2}`。
- `SET_MODE`：立即启动自动模式0～2，也可使用 `{"cmd":"MODE","value":1}`。
- `PING`：保持手动运行许可。手动控制时APP至少每5秒发送一次，否则10秒无命令会自动停机。
- `GET_STATUS`：请求立即发送一次状态包。

### 自动模式

- 模式0：位移电机运行10秒；随后按摩电机以设定速度的1/3运行60秒；完成后全部停止。
- 模式1：位移电机运行10秒；按摩依次以1/3、2/3、全速运行90秒、90秒、120秒；完成后全部停止。
- 模式2：位移电机运行10秒；按摩依次以全速、2/3、1/3运行90秒、90秒、120秒；完成后全部停止。
- 自动模式期间不需要 `PING`，但BLE断开仍会立即停止全部电机。发送手动动作、速度或档位命令会退出自动模式。

## 状态上报

Telemetry每500ms Notify一次，示例：

```json
{"version":"1.0","state":"massaging","move":0,"move_pwm":0,"massage":200,"gear":1,"mode":0,"mode_stage":"massage_1","strain_raw":1200,"strain_delta":2895,"baseline":4095,"fault":"none"}
```

`state`可取：`idle`、`moving`、`massaging`、`combined`。`move`为-1/0/1，`move_pwm`和`massage`为当前PWM值，`gear`为当前档位，`mode`为-1（手动）或0～2，`mode_stage`显示当前自动阶段。应变值为GPIO14的12位ADC原始值和相对零点差值，暂不代表公斤或牛顿。

`fault`可取：`none`、`ble_disconnected`、`command_timeout`。发生断开或命令超时后全部电机停止。

## APP要求

1. 连接后发现上述Service，并订阅Telemetry Notify。
2. 手动控制电机期间每5秒内发送一次 `PING`；自动模式不需要。
3. 页面始终提供 `STOP_ALL`，不要只依赖页面返回或断开连接。
4. 不要根据应变值在APP内自行驱动电机；当前应变值只用于显示。

旧的“应变片自动控制电机”协议已经保存到 `strain_control_archive/BLE_PROTOCOL_v1_pressure.md`，不属于当前默认固件。

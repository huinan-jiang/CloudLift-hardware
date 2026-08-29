# CloudLift BLE 手动控制固件 v1.0

这是当前默认烧录程序。上电后所有电机保持停止，不会根据应变片自动启动、停止或反转；应变片 GPIO14 只用于采样并通过 BLE 上报。

## 当前接线

- 位移电机1：GPIO4 → PWMA，GPIO5 → AIN2，GPIO6 → AIN1。
- 按摩电机1：GPIO40 → PWMC，GPIO41 → CIN2，GPIO42 → CIN1。
- 位移电机2：GPIO47 → PWMD，GPIO48 → DIN2，GPIO45 → DIN1。
- 按摩电机2：GPIO9 → PWMB，GPIO10 → BIN2，GPIO11 → BIN1。
- 两块TB6612的STBY共接GPIO17；程序上电后输出HIGH。
- 应变模块AO → GPIO14，所有GND共地。

## Arduino IDE

直接打开 `CloudLiftBleMotorControl.ino` 并选择 ESP32-S3 Dev Module。程序使用ESP32 Arduino核心自带BLE库，不需要安装额外库。烧录前关闭串口监视器，烧录后用BLE扫描工具搜索 `CloudLift`。

## 安全行为

- 上电、BLE断开、命令超时（10秒）都会停止所有电机。
- APP控制电机时应至少每5秒发送一次 `PING`，否则运行中的电机会自动停止。
- 位移电机使用已验证的直通100%输出；按摩电机使用PWM速度值0～255。

## 最小命令

命令特征值写入紧凑JSON，不带换行：

```json
{"cmd":"MOVE","direction":-1}
{"cmd":"MOVE","direction":0}
{"cmd":"MASSAGE_START","speed":200}
{"cmd":"MASSAGE_STOP"}
{"cmd":"STOP_ALL"}
{"cmd":"PING"}
```

`direction=-1`为当前确认的位移反转方向，`direction=1`为相反方向，`0`为停止。BLE服务、特征值和完整命令见项目根目录 `BLE_PROTOCOL.md`。

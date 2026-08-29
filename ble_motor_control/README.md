# CloudLift BLE 手动与模式控制固件 v1.1

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

- 上电和BLE断开会停止所有电机。
- 手动控制时应至少每5秒发送一次 `PING`，否则10秒无命令会自动停止。
- 自动模式按固定时序运行，不受手动命令超时影响；BLE断开仍会立即停止。
- 四台电机均使用PWM；三档速度依次为默认速度、默认速度的1/2和1/3。

## 最小命令

命令特征值写入紧凑JSON，不带换行：

```json
{"cmd":"MOVE","direction":-1}
{"cmd":"MOVE","direction":0}
{"cmd":"MASSAGE_START","speed":200}
{"cmd":"MASSAGE_STOP"}
{"cmd":"SET_GEAR","gear":2}
{"cmd":"SET_MODE","mode":0}
{"cmd":"STOP_ALL"}
{"cmd":"PING"}
```

`direction=-1`为当前确认的位移反转方向，`direction=1`为相反方向，`0`为停止。BLE服务、特征值和完整命令见项目根目录 `BLE_PROTOCOL.md`。

自动模式均先运行位移电机10秒：模式0随后以1/3按摩速度运行60秒；模式1依次以1/3、2/3、全速运行90秒、90秒、120秒；模式2依次以全速、2/3、1/3运行90秒、90秒、120秒。

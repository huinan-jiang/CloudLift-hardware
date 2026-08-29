# ESP32-S3 四电机压力触发按摩框架 v0.5.8

## 接线

- 位移电机1：GPIO4 → PWMA，GPIO5 → AIN2，GPIO6 → AIN1。
- 按摩电机1：GPIO40 → PWMC，GPIO41 → CIN2，GPIO42 → CIN1。
- 位移电机2：GPIO47 → PWMD，GPIO48 → DIN2，GPIO45 → DIN1。
- 按摩电机2：GPIO9 → PWMB，GPIO10 → BIN2，GPIO11 → BIN1。
- 应变模块：AO → GPIO14，GND必须与ESP32共地。
- 两块TB6612共用STBY → GPIO17，程序上电后主动输出HIGH。

应变模块AO电压必须处于0～3.3V。模块若使用5V供电且AO可能超过3.3V，必须加分压或电平转换，禁止直接接入ESP32-S3。

## 动作

- 两台位移电机作为一组同步控制，默认使用相反电气方向以适配左右镜像安装。
- 通电校准后必须先连续300ms检测到 `free`，才进入“夹紧”阶段；如果应变片仍被按压，系统保持 `waiting`。
- 进入夹紧后，两台位移电机同时以PWM 255起动800ms，再降到PWM 200并按原方向的反方向运行；按摩电机保持停止。
- 应变 `delta` 达到 `STRAIN_TARGET_DELTA` 后，停止位移并启动两台按摩电机。
- 按摩最多运行60秒；达到超压阈值或夹紧10秒未达到目标时，反向释放1秒并停机。
- 按ESP32的RESET/EN可重新测试。

## 方向调整

如果某台电机物理方向错误，只修改代码顶部对应符号：

- `MOVE1_SIGN`
- `MOVE2_SIGN`
- `MASSAGE1_SIGN`
- `MASSAGE2_SIGN`

在 `+1` 与 `-1` 之间切换。不要为了调整方向而交换多组GPIO定义。

## 安全限制

当前代码没有机械限位和电机堵转检测；夹紧阶段只能架空或短行程测试，并确认应变片受力方向正确。GPIO45是ESP32-S3启动配置相关引脚之一；如果出现无法启动或烧录异常，应优先将PWMD改接其他普通输出GPIO后同步修改代码。

## 应变数据

GPIO14每20ms采样一次，串口监视器使用115200波特率，每200ms输出：

```text
strain raw=1234 filtered=1228 baseline=1000 delta=228 level=contact state=clamping min=980 max=1305
```

- `raw`：当前12位ADC原始值，范围0～4095。
- `filtered`：一阶低通滤波值。
- `baseline`：上电前1.5秒自动计算的无载零点。
- `delta`：无载零点减去滤波值；该模块受力越大，AO读数越低。
- `level`：当前应变等级。
- `state`：当前控制状态，例如夹紧、按摩、释放或故障。
- `min/max`：本次上电后的最小值和最大值。

默认等级：

- `free`：delta小于200。
- `contact`：delta达到200。
- `target`：delta达到1000，开始按摩。
- `over`：delta达到3800，安全释放并停机（当前为临时架空测试值）。

三个阈值分别由 `STRAIN_CONTACT_DELTA`、`STRAIN_TARGET_DELTA`、`STRAIN_OVERLOAD_DELTA` 设置。v0.5.x开始使用 `target` 和 `over` 等级控制动作流程。
夹紧最长时间由 `CLAMP_TIMEOUT_MS` 设置，释放时间由 `RELEASE_RUN_MS` 设置。当前默认值是10秒和1秒，后续可根据实际压力和行程调整。

# ESP32-S3 双电机自动测试

## 电机1

- GPIO4 → AIN1
- GPIO5 → AIN2
- GPIO6 → PWMA

## 电机2

- GPIO40 → PWMC
- GPIO41 → CIN2
- GPIO42 → CIN1

程序通电后等待3秒，两台电机正转5秒、停止2秒、反转5秒、停止5秒，然后循环。

GPIO40连接的PWMC与CIN1/CIN2属于同一个C通道，三根控制线匹配。

所有驱动板逻辑电源与 ESP32-S3 共3.3V逻辑电平；电机使用独立3.0V电源；所有GND共地。STBY若直接接3.3V，不再接ESP32 GPIO。

第一次测试必须架空电机。若两台电机因机械镜像需要相反方向，可将第二个 `driveMotor` 调用中的 `TEST_PWM` 正负号反过来。

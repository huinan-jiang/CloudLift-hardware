#include <Arduino.h>
#if __has_include(<esp_arduino_version.h>)
#include <esp_arduino_version.h>
#endif

#ifndef ESP_ARDUINO_VERSION_MAJOR
#define ESP_ARDUINO_VERSION_MAJOR 2
#endif

// Motor 1 driver inputs
constexpr uint8_t MOTOR1_IN1 = 4;   // AIN1
constexpr uint8_t MOTOR1_IN2 = 5;   // AIN2
constexpr uint8_t MOTOR1_PWM = 6;   // PWMA

// Motor 2 driver inputs, according to the provided wiring order:
// PWMC -> GPIO40, CIN2 -> GPIO41, CIN1 -> GPIO42.
constexpr uint8_t MOTOR2_PWM = 40;  // PWMC
constexpr uint8_t MOTOR2_IN2 = 41;  // CIN2
constexpr uint8_t MOTOR2_IN1 = 42;  // CIN1

constexpr uint8_t MOTOR1_CHANNEL = 0;  // Arduino-ESP32 2.x only
constexpr uint8_t MOTOR2_CHANNEL = 1;  // Arduino-ESP32 2.x only
constexpr uint32_t PWM_FREQUENCY = 20000;
constexpr uint8_t PWM_RESOLUTION = 8;
constexpr int TEST_PWM = 160;  // 0..255

void attachPwm(uint8_t pin, uint8_t channel) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(pin, PWM_FREQUENCY, PWM_RESOLUTION);
#else
  ledcSetup(channel, PWM_FREQUENCY, PWM_RESOLUTION);
  ledcAttachPin(pin, channel);
#endif
}

void writePwm(uint8_t pin, uint8_t channel, uint8_t duty) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(pin, duty);
#else
  ledcWrite(channel, duty);
#endif
}

void driveMotor(uint8_t in1, uint8_t in2, uint8_t pwmPin,
                uint8_t pwmChannel, int speed) {
  speed = constrain(speed, -255, 255);
  digitalWrite(in1, speed > 0 ? HIGH : LOW);
  digitalWrite(in2, speed < 0 ? HIGH : LOW);
  writePwm(pwmPin, pwmChannel, abs(speed));
}

void stopBoth() {
  driveMotor(MOTOR1_IN1, MOTOR1_IN2, MOTOR1_PWM, MOTOR1_CHANNEL, 0);
  driveMotor(MOTOR2_IN1, MOTOR2_IN2, MOTOR2_PWM, MOTOR2_CHANNEL, 0);
}

void setup() {
  pinMode(MOTOR1_IN1, OUTPUT);
  pinMode(MOTOR1_IN2, OUTPUT);
  pinMode(MOTOR2_IN1, OUTPUT);
  pinMode(MOTOR2_IN2, OUTPUT);

  attachPwm(MOTOR1_PWM, MOTOR1_CHANNEL);
  attachPwm(MOTOR2_PWM, MOTOR2_CHANNEL);
  stopBoth();

  delay(3000);
}

void loop() {
  // Both motors forward for 5 seconds.
  driveMotor(MOTOR1_IN1, MOTOR1_IN2, MOTOR1_PWM, MOTOR1_CHANNEL, TEST_PWM);
  driveMotor(MOTOR2_IN1, MOTOR2_IN2, MOTOR2_PWM, MOTOR2_CHANNEL, TEST_PWM);
  delay(5000);

  stopBoth();
  delay(2000);

  // Both motors reverse for 5 seconds.
  driveMotor(MOTOR1_IN1, MOTOR1_IN2, MOTOR1_PWM, MOTOR1_CHANNEL, -TEST_PWM);
  driveMotor(MOTOR2_IN1, MOTOR2_IN2, MOTOR2_PWM, MOTOR2_CHANNEL, -TEST_PWM);
  delay(5000);

  stopBoth();
  delay(5000);
}

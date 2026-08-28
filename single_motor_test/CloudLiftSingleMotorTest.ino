#include <Arduino.h>
#if __has_include(<esp_arduino_version.h>)
#include <esp_arduino_version.h>
#endif

#ifndef ESP_ARDUINO_VERSION_MAJOR
#define ESP_ARDUINO_VERSION_MAJOR 2
#endif

// TB6612 A channel -> ESP32-S3
constexpr uint8_t AIN1_PIN = 4;
constexpr uint8_t AIN2_PIN = 5;
constexpr uint8_t PWMA_PIN = 6;

constexpr uint8_t PWM_CHANNEL = 0;  // used only by Arduino-ESP32 2.x
constexpr uint32_t PWM_FREQUENCY = 20000;
constexpr uint8_t PWM_RESOLUTION = 8;
constexpr int TEST_PWM = 160;       // 0..255

void setPwm(uint8_t duty) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(PWMA_PIN, duty);
#else
  ledcWrite(PWM_CHANNEL, duty);
#endif
}

void motorStop() {
  digitalWrite(AIN1_PIN, LOW);
  digitalWrite(AIN2_PIN, LOW);
  setPwm(0);
}

void motorForward(uint8_t duty) {
  digitalWrite(AIN1_PIN, HIGH);
  digitalWrite(AIN2_PIN, LOW);
  setPwm(duty);
}

void motorReverse(uint8_t duty) {
  digitalWrite(AIN1_PIN, LOW);
  digitalWrite(AIN2_PIN, HIGH);
  setPwm(duty);
}

void setup() {
  pinMode(AIN1_PIN, OUTPUT);
  pinMode(AIN2_PIN, OUTPUT);

#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(PWMA_PIN, PWM_FREQUENCY, PWM_RESOLUTION);
#else
  ledcSetup(PWM_CHANNEL, PWM_FREQUENCY, PWM_RESOLUTION);
  ledcAttachPin(PWMA_PIN, PWM_CHANNEL);
#endif

  motorStop();
  delay(3000);  // three seconds to move away from the mechanism after power-on
}

void loop() {
  motorForward(TEST_PWM);
  delay(5000);

  motorStop();
  delay(2000);

  motorReverse(TEST_PWM);
  delay(5000);

  motorStop();
  delay(5000);
}


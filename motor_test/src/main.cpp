#include <Arduino.h>
#if __has_include(<esp_arduino_version.h>)
#include <esp_arduino_version.h>
#endif

#ifndef ESP_ARDUINO_VERSION_MAJOR
#define ESP_ARDUINO_VERSION_MAJOR 2
#endif

namespace Pins {
// ESP32-S3 pin assignment. GPIO22-25 do not exist on ESP32-S3, and
// GPIO26-32 are commonly reserved for flash/PSRAM on modules with PSRAM.
constexpr uint8_t ARC_LEFT_IN1 = 4;
constexpr uint8_t ARC_LEFT_IN2 = 5;
constexpr uint8_t ARC_LEFT_PWM = 6;
constexpr uint8_t ARC_RIGHT_IN1 = 7;
constexpr uint8_t ARC_RIGHT_IN2 = 8;
constexpr uint8_t ARC_RIGHT_PWM = 9;

constexpr uint8_t ROLLER_LEFT_IN1 = 10;
constexpr uint8_t ROLLER_LEFT_IN2 = 11;
constexpr uint8_t ROLLER_LEFT_PWM = 12;
constexpr uint8_t ROLLER_RIGHT_IN1 = 14;
constexpr uint8_t ROLLER_RIGHT_IN2 = 15;
constexpr uint8_t ROLLER_RIGHT_PWM = 16;

constexpr uint8_t STBY = 17;
constexpr uint8_t START_STOP_BUTTON = 18;  // button between GPIO18 and GND
constexpr uint8_t STATUS_LED = 47;         // optional external LED
}  // namespace Pins

namespace TestConfig {
constexpr int TARGET_PWM = 160;          // 0..255; start unloaded
constexpr uint32_t RAMP_MS = 1000;       // soft-start time
constexpr uint32_t REVERSE_MS = 4000;    // arc motors reverse every 4 seconds
constexpr uint32_t MAX_RUN_MS = 30000;   // automatic stop after 30 seconds
constexpr uint32_t DEBOUNCE_MS = 40;

// Change a sign from +1 to -1 if that motor turns in the wrong direction.
constexpr int8_t ARC_LEFT_SIGN = +1;
constexpr int8_t ARC_RIGHT_SIGN = +1;
constexpr int8_t ROLLER_LEFT_SIGN = +1;
constexpr int8_t ROLLER_RIGHT_SIGN = -1;
}  // namespace TestConfig

class Motor {
 public:
  Motor(uint8_t in1, uint8_t in2, uint8_t pwm, uint8_t channel)
      : in1_(in1), in2_(in2), pwm_(pwm), channel_(channel) {}

  void begin() {
    pinMode(in1_, OUTPUT);
    pinMode(in2_, OUTPUT);
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcAttach(pwm_, 20000, 8);
#else
    ledcSetup(channel_, 20000, 8);
    ledcAttachPin(pwm_, channel_);
#endif
    stop();
  }

  void drive(int speed) {
    speed = constrain(speed, -255, 255);
    digitalWrite(in1_, speed > 0 ? HIGH : LOW);
    digitalWrite(in2_, speed < 0 ? HIGH : LOW);
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcWrite(pwm_, abs(speed));
#else
    ledcWrite(channel_, abs(speed));
#endif
  }

  void stop() {
    digitalWrite(in1_, LOW);
    digitalWrite(in2_, LOW);
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcWrite(pwm_, 0);
#else
    ledcWrite(channel_, 0);
#endif
  }

 private:
  uint8_t in1_, in2_, pwm_, channel_;
};

Motor arcLeft(Pins::ARC_LEFT_IN1, Pins::ARC_LEFT_IN2, Pins::ARC_LEFT_PWM, 0);
Motor arcRight(Pins::ARC_RIGHT_IN1, Pins::ARC_RIGHT_IN2, Pins::ARC_RIGHT_PWM, 1);
Motor rollerLeft(Pins::ROLLER_LEFT_IN1, Pins::ROLLER_LEFT_IN2, Pins::ROLLER_LEFT_PWM, 2);
Motor rollerRight(Pins::ROLLER_RIGHT_IN1, Pins::ROLLER_RIGHT_IN2, Pins::ROLLER_RIGHT_PWM, 3);

bool running = false;
uint32_t runStartedAt = 0;

void stopAll() {
  arcLeft.stop();
  arcRight.stop();
  rollerLeft.stop();
  rollerRight.stop();
  running = false;
  digitalWrite(Pins::STATUS_LED, LOW);
  Serial.println("STOPPED");
}

void startTest() {
  runStartedAt = millis();
  running = true;
  digitalWrite(Pins::STATUS_LED, HIGH);
  Serial.println("RUNNING: press button or send s to stop");
}

void toggleTest() {
  if (running) stopAll();
  else startTest();
}

bool buttonPressedEvent() {
  static bool stable = HIGH;
  static bool previousRead = HIGH;
  static uint32_t changedAt = 0;
  const bool reading = digitalRead(Pins::START_STOP_BUTTON);
  if (reading != previousRead) {
    previousRead = reading;
    changedAt = millis();
  }
  if (millis() - changedAt >= TestConfig::DEBOUNCE_MS && reading != stable) {
    stable = reading;
    return stable == LOW;
  }
  return false;
}

void runCombinedMode() {
  const uint32_t elapsed = millis() - runStartedAt;
  if (elapsed >= TestConfig::MAX_RUN_MS) {
    stopAll();
    return;
  }

  const int pwm = elapsed < TestConfig::RAMP_MS
                      ? map(elapsed, 0, TestConfig::RAMP_MS, 0, TestConfig::TARGET_PWM)
                      : TestConfig::TARGET_PWM;
  const int8_t arcDirection = ((elapsed / TestConfig::REVERSE_MS) % 2 == 0) ? +1 : -1;

  arcLeft.drive(TestConfig::ARC_LEFT_SIGN * arcDirection * pwm);
  arcRight.drive(TestConfig::ARC_RIGHT_SIGN * arcDirection * pwm);
  rollerLeft.drive(TestConfig::ROLLER_LEFT_SIGN * pwm);
  rollerRight.drive(TestConfig::ROLLER_RIGHT_SIGN * pwm);
}

void setup() {
  Serial.begin(115200);
  pinMode(Pins::STBY, OUTPUT);
  pinMode(Pins::STATUS_LED, OUTPUT);
  pinMode(Pins::START_STOP_BUTTON, INPUT_PULLUP);

  arcLeft.begin();
  arcRight.begin();
  rollerLeft.begin();
  rollerRight.begin();
  digitalWrite(Pins::STBY, HIGH);
  stopAll();

  Serial.println("CloudLift four-motor bench test ready");
  Serial.println("Press GPIO32 button or send r to run; send s to stop");
}

void loop() {
  if (buttonPressedEvent()) toggleTest();

  if (Serial.available()) {
    const char command = static_cast<char>(Serial.read());
    if ((command == 'r' || command == 'R') && !running) startTest();
    if (command == 's' || command == 'S') stopAll();
  }

  if (running) runCombinedMode();
  delay(2);
}

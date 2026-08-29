#include <Arduino.h>
#if __has_include(<esp_arduino_version.h>)
#include <esp_arduino_version.h>
#endif

#ifndef ESP_ARDUINO_VERSION_MAJOR
#define ESP_ARDUINO_VERSION_MAJOR 2
#endif

// CloudLift dual-motor asynchronous bench test v0.2.2
// Motor 1 = displacement motor; Motor 2 = massage motor.

constexpr uint8_t MOVE_IN1 = 4;   // AIN1
constexpr uint8_t MOVE_IN2 = 5;   // AIN2
constexpr uint8_t MOVE_PWM = 6;   // PWMA

constexpr uint8_t MASSAGE_PWM = 40;  // PWMC
constexpr uint8_t MASSAGE_IN2 = 41;  // CIN2
constexpr uint8_t MASSAGE_IN1 = 42;  // CIN1

constexpr uint8_t MOVE_PWM_CHANNEL = 0;     // Arduino-ESP32 2.x only
constexpr uint8_t MASSAGE_PWM_CHANNEL = 1;  // Arduino-ESP32 2.x only
constexpr uint32_t PWM_FREQUENCY = 20000;
constexpr uint8_t PWM_RESOLUTION = 8;

constexpr int MOVE_TARGET_PWM = 255;       // full power for loaded displacement motor
constexpr int MASSAGE_TARGET_PWM = 160;
constexpr int8_t MASSAGE_DIRECTION = +1;
constexpr uint32_t POWER_ON_DELAY_MS = 3000;
constexpr uint32_t SOFT_START_MS = 800;
constexpr uint32_t MOVE_RUN_MS = 4000;
constexpr uint32_t REVERSAL_PAUSE_MS = 400;
constexpr uint32_t MAX_TEST_RUN_MS = 60000;

enum class MovePhase {
  FORWARD,
  PAUSE_BEFORE_REVERSE,
  REVERSE,
  PAUSE_BEFORE_FORWARD
};

MovePhase movePhase = MovePhase::FORWARD;
uint32_t bootAt = 0;
uint32_t testStartedAt = 0;
uint32_t movePhaseStartedAt = 0;
bool testRunning = false;
bool testFinished = false;

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

int softStartPwm(uint32_t elapsed, int targetPwm) {
  if (elapsed >= SOFT_START_MS) return targetPwm;
  return map(elapsed, 0, SOFT_START_MS, 0, targetPwm);
}

void stopBothMotors() {
  driveMotor(MOVE_IN1, MOVE_IN2, MOVE_PWM, MOVE_PWM_CHANNEL, 0);
  driveMotor(MASSAGE_IN1, MASSAGE_IN2, MASSAGE_PWM,
             MASSAGE_PWM_CHANNEL, 0);
}

void startTest() {
  testRunning = true;
  testStartedAt = millis();
  movePhase = MovePhase::FORWARD;
  movePhaseStartedAt = testStartedAt;
  Serial.println("v0.2.2 asynchronous test started");
}

void updateMassageMotor(uint32_t now) {
  const int pwm = softStartPwm(now - testStartedAt, MASSAGE_TARGET_PWM);
  driveMotor(MASSAGE_IN1, MASSAGE_IN2, MASSAGE_PWM, MASSAGE_PWM_CHANNEL,
             MASSAGE_DIRECTION * pwm);
}

void changeMovePhase(MovePhase next, uint32_t now) {
  movePhase = next;
  movePhaseStartedAt = now;
}

void updateDisplacementMotor(uint32_t now) {
  const uint32_t elapsed = now - movePhaseStartedAt;

  switch (movePhase) {
    case MovePhase::FORWARD:
      driveMotor(MOVE_IN1, MOVE_IN2, MOVE_PWM, MOVE_PWM_CHANNEL,
                 softStartPwm(elapsed, MOVE_TARGET_PWM));
      if (elapsed >= MOVE_RUN_MS) {
        driveMotor(MOVE_IN1, MOVE_IN2, MOVE_PWM, MOVE_PWM_CHANNEL, 0);
        changeMovePhase(MovePhase::PAUSE_BEFORE_REVERSE, now);
      }
      break;

    case MovePhase::PAUSE_BEFORE_REVERSE:
      driveMotor(MOVE_IN1, MOVE_IN2, MOVE_PWM, MOVE_PWM_CHANNEL, 0);
      if (elapsed >= REVERSAL_PAUSE_MS) {
        changeMovePhase(MovePhase::REVERSE, now);
      }
      break;

    case MovePhase::REVERSE:
      driveMotor(MOVE_IN1, MOVE_IN2, MOVE_PWM, MOVE_PWM_CHANNEL,
                 -softStartPwm(elapsed, MOVE_TARGET_PWM));
      if (elapsed >= MOVE_RUN_MS) {
        driveMotor(MOVE_IN1, MOVE_IN2, MOVE_PWM, MOVE_PWM_CHANNEL, 0);
        changeMovePhase(MovePhase::PAUSE_BEFORE_FORWARD, now);
      }
      break;

    case MovePhase::PAUSE_BEFORE_FORWARD:
      driveMotor(MOVE_IN1, MOVE_IN2, MOVE_PWM, MOVE_PWM_CHANNEL, 0);
      if (elapsed >= REVERSAL_PAUSE_MS) {
        changeMovePhase(MovePhase::FORWARD, now);
      }
      break;
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(MOVE_IN1, OUTPUT);
  pinMode(MOVE_IN2, OUTPUT);
  pinMode(MASSAGE_IN1, OUTPUT);
  pinMode(MASSAGE_IN2, OUTPUT);
  attachPwm(MOVE_PWM, MOVE_PWM_CHANNEL);
  attachPwm(MASSAGE_PWM, MASSAGE_PWM_CHANNEL);
  stopBothMotors();
  bootAt = millis();
  Serial.println("CloudLift v0.2.2 ready; automatic start in 3 seconds");
}

void loop() {
  const uint32_t now = millis();

  if (!testRunning && !testFinished && now - bootAt >= POWER_ON_DELAY_MS) {
    startTest();
  }

  if (testRunning) {
    if (now - testStartedAt >= MAX_TEST_RUN_MS) {
      stopBothMotors();
      testRunning = false;
      testFinished = true;
      Serial.println("60-second test completed; power cycle to run again");
    } else {
      updateMassageMotor(now);
      updateDisplacementMotor(now);
    }
  }

  delay(2);  // only yields CPU time; motor timing uses millis()
}

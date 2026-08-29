#include <Arduino.h>
#if __has_include(<esp_arduino_version.h>)
#include <esp_arduino_version.h>
#endif

#ifndef ESP_ARDUINO_VERSION_MAJOR
#define ESP_ARDUINO_VERSION_MAJOR 2
#endif

// CloudLift four-motor asynchronous bench test v0.3.0

// Displacement motor 1: A channel
constexpr uint8_t MOVE1_IN1 = 4;   // AIN1
constexpr uint8_t MOVE1_IN2 = 5;   // AIN2
constexpr uint8_t MOVE1_PWM = 6;   // PWMA

// Massage motor 1: C channel
constexpr uint8_t MASSAGE1_PWM = 40;  // PWMC
constexpr uint8_t MASSAGE1_IN2 = 41;  // CIN2
constexpr uint8_t MASSAGE1_IN1 = 42;  // CIN1

// Displacement motor 2: D channel
constexpr uint8_t MOVE2_PWM = 45;  // PWMD
constexpr uint8_t MOVE2_IN2 = 48;  // DIN2
constexpr uint8_t MOVE2_IN1 = 47;  // DIN1

// Massage motor 2: B channel
constexpr uint8_t MASSAGE2_PWM = 9;   // PWMB
constexpr uint8_t MASSAGE2_IN2 = 10;  // BIN2
constexpr uint8_t MASSAGE2_IN1 = 11;  // BIN1

// Used only by Arduino-ESP32 2.x. Arduino-ESP32 3.x uses PWM pins directly.
constexpr uint8_t MOVE1_CHANNEL = 0;
constexpr uint8_t MASSAGE1_CHANNEL = 1;
constexpr uint8_t MOVE2_CHANNEL = 2;
constexpr uint8_t MASSAGE2_CHANNEL = 3;
constexpr uint32_t PWM_FREQUENCY = 20000;
constexpr uint8_t PWM_RESOLUTION = 8;

// Change one sign if its motor rotates opposite to the required physical motion.
// The second motor in each pair defaults to the opposite electrical direction
// because the left/right mechanisms are normally mirrored.
constexpr int8_t MOVE1_SIGN = +1;
constexpr int8_t MOVE2_SIGN = -1;
constexpr int8_t MASSAGE1_SIGN = +1;
constexpr int8_t MASSAGE2_SIGN = -1;

constexpr int MOVE_START_PWM = 255;
constexpr int MOVE_HOLD_PWM = 200;
constexpr int MASSAGE_TARGET_PWM = 160;
constexpr uint32_t POWER_ON_DELAY_MS = 3000;
constexpr uint32_t MASSAGE_START_DELAY_MS = 500;
constexpr uint32_t MASSAGE_SOFT_START_MS = 800;
constexpr uint32_t MOVE_START_BOOST_MS = 250;
constexpr uint32_t MOVE_RUN_MS = 1000;
constexpr uint32_t REVERSAL_PAUSE_MS = 600;
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
                uint8_t channel, int speed) {
  speed = constrain(speed, -255, 255);
  digitalWrite(in1, speed > 0 ? HIGH : LOW);
  digitalWrite(in2, speed < 0 ? HIGH : LOW);
  writePwm(pwmPin, channel, abs(speed));
}

void driveDisplacementPair(int speed) {
  driveMotor(MOVE1_IN1, MOVE1_IN2, MOVE1_PWM, MOVE1_CHANNEL,
             MOVE1_SIGN * speed);
  driveMotor(MOVE2_IN1, MOVE2_IN2, MOVE2_PWM, MOVE2_CHANNEL,
             MOVE2_SIGN * speed);
}

void driveMassagePair(int speed) {
  driveMotor(MASSAGE1_IN1, MASSAGE1_IN2, MASSAGE1_PWM, MASSAGE1_CHANNEL,
             MASSAGE1_SIGN * speed);
  driveMotor(MASSAGE2_IN1, MASSAGE2_IN2, MASSAGE2_PWM, MASSAGE2_CHANNEL,
             MASSAGE2_SIGN * speed);
}

void stopAllMotors() {
  driveDisplacementPair(0);
  driveMassagePair(0);
}

int displacementPwm(uint32_t elapsed) {
  return elapsed < MOVE_START_BOOST_MS ? MOVE_START_PWM : MOVE_HOLD_PWM;
}

int massagePwm(uint32_t elapsed) {
  if (elapsed >= MASSAGE_SOFT_START_MS) return MASSAGE_TARGET_PWM;
  return map(elapsed, 0, MASSAGE_SOFT_START_MS, 0, MASSAGE_TARGET_PWM);
}

void changeMovePhase(MovePhase next, uint32_t now) {
  movePhase = next;
  movePhaseStartedAt = now;
}

void updateDisplacementPair(uint32_t now) {
  const uint32_t elapsed = now - movePhaseStartedAt;

  switch (movePhase) {
    case MovePhase::FORWARD:
      driveDisplacementPair(displacementPwm(elapsed));
      if (elapsed >= MOVE_RUN_MS) {
        driveDisplacementPair(0);
        changeMovePhase(MovePhase::PAUSE_BEFORE_REVERSE, now);
      }
      break;

    case MovePhase::PAUSE_BEFORE_REVERSE:
      driveDisplacementPair(0);
      if (elapsed >= REVERSAL_PAUSE_MS) {
        changeMovePhase(MovePhase::REVERSE, now);
      }
      break;

    case MovePhase::REVERSE:
      driveDisplacementPair(-displacementPwm(elapsed));
      if (elapsed >= MOVE_RUN_MS) {
        driveDisplacementPair(0);
        changeMovePhase(MovePhase::PAUSE_BEFORE_FORWARD, now);
      }
      break;

    case MovePhase::PAUSE_BEFORE_FORWARD:
      driveDisplacementPair(0);
      if (elapsed >= REVERSAL_PAUSE_MS) {
        changeMovePhase(MovePhase::FORWARD, now);
      }
      break;
  }
}

void updateMassagePair(uint32_t now) {
  const uint32_t elapsed = now - testStartedAt;
  if (elapsed < MASSAGE_START_DELAY_MS) {
    driveMassagePair(0);
    return;
  }
  driveMassagePair(massagePwm(elapsed - MASSAGE_START_DELAY_MS));
}

void startTest() {
  testRunning = true;
  testStartedAt = millis();
  movePhase = MovePhase::FORWARD;
  movePhaseStartedAt = testStartedAt;
  Serial.println("CloudLift v0.3.0 four-motor test started");
}

void setup() {
  Serial.begin(115200);

  const uint8_t directionPins[] = {
      MOVE1_IN1, MOVE1_IN2, MASSAGE1_IN1, MASSAGE1_IN2,
      MOVE2_IN1, MOVE2_IN2, MASSAGE2_IN1, MASSAGE2_IN2};
  for (uint8_t pin : directionPins) pinMode(pin, OUTPUT);

  attachPwm(MOVE1_PWM, MOVE1_CHANNEL);
  attachPwm(MASSAGE1_PWM, MASSAGE1_CHANNEL);
  attachPwm(MOVE2_PWM, MOVE2_CHANNEL);
  attachPwm(MASSAGE2_PWM, MASSAGE2_CHANNEL);

  stopAllMotors();
  bootAt = millis();
  Serial.println("CloudLift v0.3.0 ready; automatic start in 3 seconds");
}

void loop() {
  const uint32_t now = millis();

  if (!testRunning && !testFinished && now - bootAt >= POWER_ON_DELAY_MS) {
    startTest();
  }

  if (testRunning) {
    if (now - testStartedAt >= MAX_TEST_RUN_MS) {
      stopAllMotors();
      testRunning = false;
      testFinished = true;
      Serial.println("60-second test completed; reset ESP32 to run again");
    } else {
      updateDisplacementPair(now);
      updateMassagePair(now);
    }
  }

  delay(2);
}


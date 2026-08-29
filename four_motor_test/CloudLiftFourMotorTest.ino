#include <Arduino.h>
#if __has_include(<esp_arduino_version.h>)
#include <esp_arduino_version.h>
#endif

#ifndef ESP_ARDUINO_VERSION_MAJOR
#define ESP_ARDUINO_VERSION_MAJOR 2
#endif

// CloudLift four-motor pressure-gated massage framework v0.5.0

// Strain sensor module analog output. AO must stay within 0..3.3V.
constexpr uint8_t STRAIN_AO_PIN = 14;

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
constexpr int MASSAGE1_TARGET_PWM = 160;
constexpr int MASSAGE2_START_PWM = 255;
constexpr int MASSAGE2_HOLD_PWM = 210;
constexpr uint32_t POWER_ON_DELAY_MS = 3000;
constexpr uint32_t MASSAGE_START_DELAY_MS = 500;
constexpr uint32_t MASSAGE_SOFT_START_MS = 800;
constexpr uint32_t MASSAGE2_EXTRA_DELAY_MS = 300;
constexpr uint32_t MASSAGE2_START_BOOST_MS = 250;
constexpr uint32_t MOVE_START_BOOST_MS = 250;
constexpr uint32_t MOVE_RUN_MS = 1000;
constexpr uint32_t REVERSAL_PAUSE_MS = 600;
constexpr uint32_t MAX_TEST_RUN_MS = 60000;
constexpr uint32_t CLAMP_TIMEOUT_MS = 10000;
constexpr uint32_t RELEASE_RUN_MS = 1000;
constexpr uint32_t STRAIN_SAMPLE_MS = 20;
constexpr uint32_t STRAIN_REPORT_MS = 200;
constexpr uint32_t STRAIN_CALIBRATION_MS = 1500;
// This module rests near 4095 and its AO value falls as force increases.
constexpr int STRAIN_CONTACT_DELTA = 200;
constexpr int STRAIN_TARGET_DELTA = 1000;
constexpr int STRAIN_OVERLOAD_DELTA = 2500;

enum class MovePhase {
  FORWARD,
  PAUSE_BEFORE_REVERSE,
  REVERSE,
  PAUSE_BEFORE_FORWARD
};

enum class SystemState {
  WAITING,
  CLAMPING,
  MASSAGING,
  RELEASING,
  COMPLETE,
  FAULT
};

MovePhase movePhase = MovePhase::FORWARD;
uint32_t bootAt = 0;
uint32_t testStartedAt = 0;
uint32_t movePhaseStartedAt = 0;
bool testRunning = false;
bool testFinished = false;
SystemState systemState = SystemState::WAITING;
uint32_t systemStateStartedAt = 0;
bool releaseToFault = false;
uint32_t lastStrainSampleAt = 0;
uint32_t lastStrainReportAt = 0;
int strainRaw = 0;
int strainFiltered = 0;
int strainMinimum = 4095;
int strainMaximum = 0;
bool strainFilterReady = false;
bool strainBaselineReady = false;
uint32_t strainBaselineSum = 0;
uint32_t strainBaselineSamples = 0;
int strainBaseline = 0;
int strainDelta = 0;

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

void driveMassageMotors(int massage1Speed, int massage2Speed) {
  driveMotor(MASSAGE1_IN1, MASSAGE1_IN2, MASSAGE1_PWM, MASSAGE1_CHANNEL,
             MASSAGE1_SIGN * massage1Speed);
  driveMotor(MASSAGE2_IN1, MASSAGE2_IN2, MASSAGE2_PWM, MASSAGE2_CHANNEL,
             MASSAGE2_SIGN * massage2Speed);
}

void stopAllMotors() {
  driveDisplacementPair(0);
  driveMassageMotors(0, 0);
}

int displacementPwm(uint32_t elapsed) {
  return elapsed < MOVE_START_BOOST_MS ? MOVE_START_PWM : MOVE_HOLD_PWM;
}

int massagePwm(uint32_t elapsed) {
  if (elapsed >= MASSAGE_SOFT_START_MS) return MASSAGE1_TARGET_PWM;
  return map(elapsed, 0, MASSAGE_SOFT_START_MS, 0, MASSAGE1_TARGET_PWM);
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
    driveMassageMotors(0, 0);
    return;
  }

  const uint32_t massageElapsed = elapsed - MASSAGE_START_DELAY_MS;
  const int massage1Pwm = massagePwm(massageElapsed);
  int massage2Pwm = 0;
  if (massageElapsed >= MASSAGE2_EXTRA_DELAY_MS) {
    const uint32_t massage2Elapsed = massageElapsed - MASSAGE2_EXTRA_DELAY_MS;
    massage2Pwm = massage2Elapsed < MASSAGE2_START_BOOST_MS
                      ? MASSAGE2_START_PWM
                      : MASSAGE2_HOLD_PWM;
  }
  driveMassageMotors(massage1Pwm, massage2Pwm);
}

void startTest() {
  testRunning = true;
  testFinished = false;
  systemState = SystemState::CLAMPING;
  systemStateStartedAt = millis();
  testStartedAt = systemStateStartedAt;
  Serial.println("CloudLift v0.5.0 started: clamping until target pressure");
}

void beginRelease(bool fault) {
  releaseToFault = fault;
  systemState = SystemState::RELEASING;
  systemStateStartedAt = millis();
  driveMassageMotors(0, 0);
  Serial.println(fault ? "Pressure safety release" : "Massage time complete; releasing");
}

void enterMassage() {
  systemState = SystemState::MASSAGING;
  systemStateStartedAt = millis();
  testStartedAt = systemStateStartedAt;
  driveDisplacementPair(0);
  Serial.println("Target pressure reached; massage started");
}

void updatePressureGatedControl(uint32_t now) {
  switch (systemState) {
    case SystemState::WAITING:
    case SystemState::COMPLETE:
    case SystemState::FAULT:
      stopAllMotors();
      break;

    case SystemState::CLAMPING:
      driveMassageMotors(0, 0);
      if (strainBaselineReady && strainDelta >= STRAIN_OVERLOAD_DELTA) {
        beginRelease(true);
      } else if (strainBaselineReady && strainDelta >= STRAIN_TARGET_DELTA) {
        enterMassage();
      } else if (now - systemStateStartedAt >= CLAMP_TIMEOUT_MS) {
        beginRelease(true);
        Serial.println("Clamp timeout; target pressure was not reached");
      } else {
        // Keep moving the displacement mechanism toward the pressure target.
        driveDisplacementPair(MOVE_HOLD_PWM);
      }
      break;

    case SystemState::MASSAGING:
      driveDisplacementPair(0);
      if (strainBaselineReady && strainDelta >= STRAIN_OVERLOAD_DELTA) {
        beginRelease(true);
      } else if (now - testStartedAt >= MAX_TEST_RUN_MS) {
        beginRelease(false);
      } else {
        updateMassagePair(now);
      }
      break;

    case SystemState::RELEASING:
      driveMassageMotors(0, 0);
      driveDisplacementPair(-MOVE_HOLD_PWM);
      if (now - systemStateStartedAt >= RELEASE_RUN_MS) {
        stopAllMotors();
        testRunning = false;
        testFinished = true;
        systemState = releaseToFault ? SystemState::FAULT : SystemState::COMPLETE;
        Serial.println(releaseToFault ? "Safety stop; reset ESP32 to run again"
                                      : "Cycle completed; reset ESP32 to run again");
      }
      break;
  }
}

const char* strainLevelName() {
  if (!strainBaselineReady) return "calibrating";
  if (strainDelta >= STRAIN_OVERLOAD_DELTA) return "over";
  if (strainDelta >= STRAIN_TARGET_DELTA) return "target";
  if (strainDelta >= STRAIN_CONTACT_DELTA) return "contact";
  return "free";
}

void updateStrainSensor(uint32_t now) {
  if (now - lastStrainSampleAt >= STRAIN_SAMPLE_MS) {
    lastStrainSampleAt = now;
    strainRaw = analogRead(STRAIN_AO_PIN);
    if (!strainFilterReady) {
      strainFiltered = strainRaw;
      strainFilterReady = true;
    } else {
      strainFiltered = (strainFiltered * 7 + strainRaw) / 8;
    }
    strainMinimum = min(strainMinimum, strainRaw);
    strainMaximum = max(strainMaximum, strainRaw);

    if (!strainBaselineReady) {
      if (now <= STRAIN_CALIBRATION_MS) {
        strainBaselineSum += strainRaw;
        strainBaselineSamples++;
      } else if (strainBaselineSamples > 0) {
        strainBaseline = strainBaselineSum / strainBaselineSamples;
        strainBaselineReady = true;
      }
    }
    if (strainBaselineReady) {
      strainDelta = max(0, strainBaseline - strainFiltered);
    }
  }

  if (now - lastStrainReportAt >= STRAIN_REPORT_MS) {
    lastStrainReportAt = now;
    Serial.printf(
        "strain raw=%d filtered=%d baseline=%d delta=%d level=%s min=%d max=%d\n",
        strainRaw, strainFiltered, strainBaseline, strainDelta,
        strainLevelName(), strainMinimum, strainMaximum);
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(STRAIN_AO_PIN, INPUT);
  analogReadResolution(12);

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
  Serial.println("CloudLift v0.5.0 ready; automatic start in 3 seconds");
}

void loop() {
  const uint32_t now = millis();
  updateStrainSensor(now);

  if (!testRunning && !testFinished && now - bootAt >= POWER_ON_DELAY_MS) {
    startTest();
  }

  if (testRunning) updatePressureGatedControl(now);

  delay(2);
}

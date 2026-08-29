#include <Arduino.h>

// CloudLift ESP32-S3 displacement-motor direct diagnostic v1.1
// This sketch intentionally bypasses the strain sensor, massage motors,
// pressure state machine and LEDC PWM. PWM pins are driven HIGH for 100% duty.

// Displacement motor 1: TB6612 A channel
constexpr uint8_t MOVE1_IN1 = 4;   // AIN1
constexpr uint8_t MOVE1_IN2 = 5;   // AIN2
constexpr uint8_t MOVE1_PWM = 6;   // PWMA

// Displacement motor 2: TB6612 D channel
constexpr uint8_t MOVE2_IN1 = 47;  // DIN1
constexpr uint8_t MOVE2_IN2 = 48;  // DIN2
constexpr uint8_t MOVE2_PWM = 45;  // PWMD

// Both TB6612 boards share the standby control used by the working firmware.
constexpr uint8_t STBY_PIN = 17;

constexpr uint32_t CYCLE_MS = 14000;
int lastStage = -1;
uint32_t cycleStartedAt = 0;

void driveDirect(uint8_t in1, uint8_t in2, uint8_t pwm, int direction) {
  digitalWrite(in1, direction > 0 ? HIGH : LOW);
  digitalWrite(in2, direction < 0 ? HIGH : LOW);
  digitalWrite(pwm, direction == 0 ? LOW : HIGH);
}

void stopBoth() {
  driveDirect(MOVE1_IN1, MOVE1_IN2, MOVE1_PWM, 0);
  driveDirect(MOVE2_IN1, MOVE2_IN2, MOVE2_PWM, 0);
}

void setStage(int stage) {
  if (stage == lastStage) return;
  lastStage = stage;
  stopBoth();

  switch (stage) {
    case 0:
      Serial.println("stage=0 STOP; direct test starts in 2 seconds");
      break;
    case 1:
      Serial.println("stage=1 MOTOR1 reverse: GPIO4=LOW GPIO5=HIGH GPIO6=HIGH");
      driveDirect(MOVE1_IN1, MOVE1_IN2, MOVE1_PWM, -1);
      break;
    case 2:
      Serial.println("stage=2 STOP");
      break;
    case 3:
      Serial.println("stage=3 MOTOR2 reverse: GPIO47=LOW GPIO48=HIGH GPIO45=HIGH");
      driveDirect(MOVE2_IN1, MOVE2_IN2, MOVE2_PWM, -1);
      break;
    case 4:
      Serial.println("stage=4 STOP");
      break;
    case 5:
      Serial.println("stage=5 BOTH reverse at 100% power");
      driveDirect(MOVE1_IN1, MOVE1_IN2, MOVE1_PWM, -1);
      driveDirect(MOVE2_IN1, MOVE2_IN2, MOVE2_PWM, -1);
      break;
    case 6:
      Serial.println("stage=6 STOP; cycle will repeat");
      break;
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(STBY_PIN, OUTPUT);
  digitalWrite(STBY_PIN, HIGH);

  const uint8_t pins[] = {
      MOVE1_IN1, MOVE1_IN2, MOVE1_PWM,
      MOVE2_IN1, MOVE2_IN2, MOVE2_PWM};
  for (uint8_t pin : pins) pinMode(pin, OUTPUT);

  stopBoth();
  cycleStartedAt = millis();
  Serial.println("CloudLift displacement direct diagnostic v1.1; GPIO17 STBY=HIGH");
}

void loop() {
  const uint32_t elapsed = (millis() - cycleStartedAt) % CYCLE_MS;

  if (elapsed < 2000) {
    setStage(0);
  } else if (elapsed < 5000) {
    setStage(1);
  } else if (elapsed < 6000) {
    setStage(2);
  } else if (elapsed < 9000) {
    setStage(3);
  } else if (elapsed < 10000) {
    setStage(4);
  } else if (elapsed < 13000) {
    setStage(5);
  } else {
    setStage(6);
  }

  delay(2);
}

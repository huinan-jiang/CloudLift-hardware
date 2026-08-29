#include <Arduino.h>

// Motor 1: A channel
constexpr uint8_t MOVE_IN1 = 4;   // AIN1
constexpr uint8_t MOVE_IN2 = 5;   // AIN2
constexpr uint8_t MOVE_PWM = 6;   // PWMA

// Motor 2: C channel
constexpr uint8_t MASSAGE_PWM = 40;  // PWMC
constexpr uint8_t MASSAGE_IN2 = 41;  // CIN2
constexpr uint8_t MASSAGE_IN1 = 42;  // CIN1

void stopBoth() {
  digitalWrite(MOVE_PWM, LOW);
  digitalWrite(MASSAGE_PWM, LOW);
  digitalWrite(MOVE_IN1, LOW);
  digitalWrite(MOVE_IN2, LOW);
  digitalWrite(MASSAGE_IN1, LOW);
  digitalWrite(MASSAGE_IN2, LOW);
}

void forwardBoth() {
  digitalWrite(MOVE_IN1, HIGH);
  digitalWrite(MOVE_IN2, LOW);
  digitalWrite(MASSAGE_IN1, HIGH);
  digitalWrite(MASSAGE_IN2, LOW);
  digitalWrite(MOVE_PWM, HIGH);     // 100% duty
  digitalWrite(MASSAGE_PWM, HIGH);  // 100% duty
}

void reverseBoth() {
  digitalWrite(MOVE_IN1, LOW);
  digitalWrite(MOVE_IN2, HIGH);
  digitalWrite(MASSAGE_IN1, LOW);
  digitalWrite(MASSAGE_IN2, HIGH);
  digitalWrite(MOVE_PWM, HIGH);     // 100% duty
  digitalWrite(MASSAGE_PWM, HIGH);  // 100% duty
}

void setup() {
  pinMode(MOVE_IN1, OUTPUT);
  pinMode(MOVE_IN2, OUTPUT);
  pinMode(MOVE_PWM, OUTPUT);
  pinMode(MASSAGE_IN1, OUTPUT);
  pinMode(MASSAGE_IN2, OUTPUT);
  pinMode(MASSAGE_PWM, OUTPUT);

  stopBoth();
  delay(3000);
}

void loop() {
  forwardBoth();
  delay(5000);

  stopBoth();
  delay(2000);

  reverseBoth();
  delay(5000);

  stopBoth();
  delay(3000);
}


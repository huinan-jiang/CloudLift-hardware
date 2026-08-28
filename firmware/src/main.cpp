#include <Arduino.h>
#include "config.h"

class Motor {
 public:
  Motor(uint8_t in1, uint8_t in2, uint8_t pwm, uint8_t channel)
      : in1_(in1), in2_(in2), pwm_(pwm), channel_(channel) {}

  void begin() {
    pinMode(in1_, OUTPUT);
    pinMode(in2_, OUTPUT);
    ledcSetup(channel_, 20000, 8);
    ledcAttachPin(pwm_, channel_);
    stop();
  }

  void drive(int speed) {
    speed = constrain(speed, -255, 255);
    digitalWrite(in1_, speed > 0 ? HIGH : LOW);
    digitalWrite(in2_, speed < 0 ? HIGH : LOW);
    ledcWrite(channel_, abs(speed));
  }

  void stop() {
    digitalWrite(in1_, LOW);
    digitalWrite(in2_, LOW);
    ledcWrite(channel_, 0);
  }

 private:
  uint8_t in1_, in2_, pwm_, channel_;
};

Motor arcLeft(Pins::ARC_LEFT_IN1, Pins::ARC_LEFT_IN2, Pins::ARC_LEFT_PWM, 0);
Motor arcRight(Pins::ARC_RIGHT_IN1, Pins::ARC_RIGHT_IN2, Pins::ARC_RIGHT_PWM, 1);
Motor rollerLeft(Pins::ROLLER_LEFT_IN1, Pins::ROLLER_LEFT_IN2, Pins::ROLLER_LEFT_PWM, 2);
Motor rollerRight(Pins::ROLLER_RIGHT_IN1, Pins::ROLLER_RIGHT_IN2, Pins::ROLLER_RIGHT_PWM, 3);

enum class State { IDLE, CLAMPING, MASSAGING, RELEASING, FAULT };
State state = State::IDLE;
int8_t travelDirection = +1;
uint32_t stateStartedAt = 0;
uint32_t lastControlAt = 0;

int pressureLeft = 0;
int pressureRight = 0;

bool leftLimit() { return digitalRead(Pins::LIMIT_LEFT) == HIGH; }
bool rightLimit() { return digitalRead(Pins::LIMIT_RIGHT) == HIGH; }
int maxPressure() { return max(pressureLeft, pressureRight); }

void stopAll() {
  arcLeft.stop();
  arcRight.stop();
  rollerLeft.stop();
  rollerRight.stop();
}

void enterState(State next) {
  stopAll();
  state = next;
  stateStartedAt = millis();
  digitalWrite(Pins::STATUS_LED, next == State::FAULT ? HIGH : LOW);
}

void samplePressure() {
  // Simple IIR filter. Values are raw 12-bit ADC readings until calibration.
  pressureLeft = (pressureLeft * 7 + analogRead(Pins::PRESSURE_LEFT)) / 8;
  pressureRight = (pressureRight * 7 + analogRead(Pins::PRESSURE_RIGHT)) / 8;
}

bool buttonPressedEvent() {
  static bool stable = HIGH;
  static bool previousRead = HIGH;
  static uint32_t changedAt = 0;
  const bool reading = digitalRead(Pins::START_STOP);
  if (reading != previousRead) {
    previousRead = reading;
    changedAt = millis();
  }
  if (millis() - changedAt >= Control::DEBOUNCE_MS && reading != stable) {
    stable = reading;
    return stable == LOW;
  }
  return false;
}

void controlStep() {
  samplePressure();

  if (maxPressure() >= Control::PRESSURE_MAX_RAW && state != State::IDLE &&
      state != State::RELEASING) {
    enterState(State::RELEASING);
  }

  switch (state) {
    case State::IDLE:
      break;

    case State::CLAMPING:
      if (leftLimit() || rightLimit()) {
        enterState(State::FAULT);
      } else if (maxPressure() >= Control::PRESSURE_TARGET_RAW) {
        enterState(State::MASSAGING);
      } else if (millis() - stateStartedAt > Control::CLAMP_TIMEOUT_MS) {
        enterState(State::FAULT);
      } else {
        arcLeft.drive(Control::ARC_LEFT_CLOSE_SIGN * Control::ARC_CLOSE_SPEED);
        arcRight.drive(Control::ARC_RIGHT_CLOSE_SIGN * Control::ARC_CLOSE_SPEED);
      }
      break;

    case State::MASSAGING:
      if (leftLimit()) travelDirection = +1;
      if (rightLimit()) travelDirection = -1;
      arcLeft.drive(travelDirection * Control::ARC_TRAVERSE_SPEED);
      arcRight.drive(travelDirection * Control::ARC_TRAVERSE_SPEED);
      rollerLeft.drive(Control::ROLLER_LEFT_SIGN * Control::ROLLER_SPEED);
      rollerRight.drive(Control::ROLLER_RIGHT_SIGN * Control::ROLLER_SPEED);
      break;

    case State::RELEASING:
      arcLeft.drive(-Control::ARC_LEFT_CLOSE_SIGN * Control::ARC_CLOSE_SPEED);
      arcRight.drive(-Control::ARC_RIGHT_CLOSE_SIGN * Control::ARC_CLOSE_SPEED);
      if (maxPressure() <= Control::PRESSURE_RELEASE_RAW || leftLimit() || rightLimit()) {
        enterState(State::IDLE);
      }
      break;

    case State::FAULT:
      stopAll();
      break;
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(Pins::STBY, OUTPUT);
  pinMode(Pins::STATUS_LED, OUTPUT);
  pinMode(Pins::START_STOP, INPUT_PULLUP);
  // GPIO34/35 have no internal pull resistors: fit external 10k pull-ups.
  pinMode(Pins::LIMIT_LEFT, INPUT);
  pinMode(Pins::LIMIT_RIGHT, INPUT);
  analogReadResolution(12);

  arcLeft.begin();
  arcRight.begin();
  rollerLeft.begin();
  rollerRight.begin();
  digitalWrite(Pins::STBY, HIGH);
  enterState(State::IDLE);
  Serial.println("Cloud-Parting Hand controller ready");
}

void loop() {
  if (buttonPressedEvent()) {
    if (state == State::IDLE) enterState(State::CLAMPING);
    else if (state == State::FAULT) enterState(State::IDLE);
    else enterState(State::RELEASING);
  }

  if (millis() - lastControlAt >= Control::CONTROL_PERIOD_MS) {
    lastControlAt = millis();
    controlStep();
  }

  static uint32_t lastReportAt = 0;
  if (millis() - lastReportAt >= 500) {
    lastReportAt = millis();
    Serial.printf("state=%u pressure=%d,%d limits=%d,%d\n",
                  static_cast<unsigned>(state), pressureLeft, pressureRight,
                  leftLimit(), rightLimit());
  }
}


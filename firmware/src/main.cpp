#include <Arduino.h>
#include <ArduinoJson.h>
#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>
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

enum class State { IDLE, CLAMPING, MASSAGING, PAUSED, RELEASING, FAULT };
State state = State::IDLE;
int8_t travelDirection = +1;
uint32_t stateStartedAt = 0;
uint32_t lastControlAt = 0;
uint32_t sessionDurationMs = Control::DEFAULT_SESSION_MS;
uint32_t sessionStartedAt = 0;
uint32_t pausedAt = 0;
uint8_t forceLevel = 3;
uint8_t speedLevel = 3;
uint8_t mode = 0;
const char* faultCode = "none";
bool faultAfterRelease = false;

BLECharacteristic* telemetryCharacteristic = nullptr;
QueueHandle_t commandQueue = nullptr;
volatile bool bleConnected = false;
volatile bool disconnectPending = false;

int pressureLeft = 0;
int pressureRight = 0;

bool leftLimit() { return digitalRead(Pins::LIMIT_LEFT) == HIGH; }
bool rightLimit() { return digitalRead(Pins::LIMIT_RIGHT) == HIGH; }
int maxPressure() { return max(pressureLeft, pressureRight); }

const char* stateName() {
  switch (state) {
    case State::IDLE: return "idle";
    case State::CLAMPING: return "clamping";
    case State::MASSAGING: return "massaging";
    case State::PAUSED: return "paused";
    case State::RELEASING: return "releasing";
    case State::FAULT: return "fault";
  }
  return "fault";
}

int scaledSpeed(int base) {
  static constexpr uint8_t scale[] = {55, 70, 85, 100, 115};
  return constrain(base * scale[constrain(speedLevel, 1, 5) - 1] / 100, 0, 255);
}

int targetPressure() {
  // Temporary raw-ADC mapping. Replace after sensor calibration.
  static constexpr int targets[] = {900, 1200, 1500, 1750, 1950};
  return targets[constrain(forceLevel, 1, 5) - 1];
}

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

void requestFaultRelease(const char* code) {
  faultCode = code;
  faultAfterRelease = true;
  enterState(State::RELEASING);
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
      state != State::RELEASING && state != State::FAULT) {
    requestFaultRelease("over_pressure");
  }

  switch (state) {
    case State::IDLE:
      break;

    case State::CLAMPING:
      if (leftLimit() || rightLimit()) {
        requestFaultRelease("limit_during_clamp");
      } else if (maxPressure() >= targetPressure()) {
        sessionStartedAt = millis();
        enterState(State::MASSAGING);
      } else if (millis() - stateStartedAt > Control::CLAMP_TIMEOUT_MS) {
        requestFaultRelease("clamp_timeout");
      } else {
        arcLeft.drive(Control::ARC_LEFT_CLOSE_SIGN * scaledSpeed(Control::ARC_CLOSE_SPEED));
        arcRight.drive(Control::ARC_RIGHT_CLOSE_SIGN * scaledSpeed(Control::ARC_CLOSE_SPEED));
      }
      break;

    case State::MASSAGING:
      if (millis() - sessionStartedAt >= sessionDurationMs) {
        enterState(State::RELEASING);
        break;
      }
      if (mode == 0 || mode == 3) {
        if (leftLimit()) travelDirection = +1;
        if (rightLimit()) travelDirection = -1;
        arcLeft.drive(travelDirection * scaledSpeed(Control::ARC_TRAVERSE_SPEED));
        arcRight.drive(travelDirection * scaledSpeed(Control::ARC_TRAVERSE_SPEED));
      } else {
        arcLeft.stop();
        arcRight.stop();
      }
      if (mode == 0 || mode == 2) {
        rollerLeft.drive(Control::ROLLER_LEFT_SIGN * scaledSpeed(Control::ROLLER_SPEED));
        rollerRight.drive(Control::ROLLER_RIGHT_SIGN * scaledSpeed(Control::ROLLER_SPEED));
      } else {
        rollerLeft.stop();
        rollerRight.stop();
      }
      break;

    case State::PAUSED:
      stopAll();
      break;

    case State::RELEASING:
      arcLeft.drive(-Control::ARC_LEFT_CLOSE_SIGN * Control::ARC_CLOSE_SPEED);
      arcRight.drive(-Control::ARC_RIGHT_CLOSE_SIGN * Control::ARC_CLOSE_SPEED);
      if (maxPressure() <= Control::PRESSURE_RELEASE_RAW || leftLimit() || rightLimit()) {
        if (faultAfterRelease) {
          faultAfterRelease = false;
          enterState(State::FAULT);
        } else {
          enterState(State::IDLE);
        }
      }
      break;

    case State::FAULT:
      stopAll();
      break;
  }
}

struct BleCommand { char json[BleConfig::MAX_COMMAND_BYTES]; };

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer*) override { bleConnected = true; }
  void onDisconnect(BLEServer*) override {
    bleConnected = false;
    disconnectPending = true;
    BLEDevice::startAdvertising();
  }
};

class CommandCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* characteristic) override {
    const std::string value = characteristic->getValue();
    if (value.empty() || value.size() >= BleConfig::MAX_COMMAND_BYTES) return;
    BleCommand command{};
    memcpy(command.json, value.data(), value.size());
    command.json[value.size()] = '\0';
    xQueueSend(commandQueue, &command, 0);
  }
};

void processCommand(const char* json) {
  StaticJsonDocument<192> doc;
  if (deserializeJson(doc, json)) return;
  const char* command = doc["cmd"] | "";

  if (!strcmp(command, "START")) {
    if (state == State::IDLE) {
      faultCode = "none";
      faultAfterRelease = false;
      sessionStartedAt = millis();
      enterState(State::CLAMPING);
    } else if (state == State::PAUSED) {
      sessionStartedAt += millis() - pausedAt;
      enterState(State::MASSAGING);
    }
  } else if (!strcmp(command, "PAUSE") && state == State::MASSAGING) {
    pausedAt = millis();
    enterState(State::PAUSED);
  } else if (!strcmp(command, "STOP")) {
    if (state != State::IDLE) enterState(State::RELEASING);
  } else if (!strcmp(command, "SET_FORCE") && state == State::IDLE) {
    forceLevel = constrain(doc["value"] | 3, 1, 5);
  } else if (!strcmp(command, "SET_SPEED")) {
    speedLevel = constrain(doc["value"] | 3, 1, 5);
  } else if (!strcmp(command, "SET_TIME") && state == State::IDLE) {
    const uint32_t seconds = constrain(doc["seconds"] | 600, 10, 3600);
    sessionDurationMs = seconds * 1000UL;
  } else if (!strcmp(command, "SET_MODE") && state == State::IDLE) {
    mode = constrain(doc["value"] | 0, 0, 3);
  } else if (!strcmp(command, "CLEAR_FAULT") && state == State::FAULT) {
    faultCode = "none";
    faultAfterRelease = false;
    enterState(State::IDLE);
  }
}

void publishTelemetry() {
  if (!bleConnected || telemetryCharacteristic == nullptr) return;
  StaticJsonDocument<256> doc;
  doc["state"] = stateName();
  doc["pressure_left"] = pressureLeft;
  doc["pressure_right"] = pressureRight;
  doc["limit_left"] = leftLimit();
  doc["limit_right"] = rightLimit();
  doc["force"] = forceLevel;
  doc["speed"] = speedLevel;
  doc["mode"] = mode;
  doc["fault"] = faultCode;
  uint32_t elapsed = 0;
  if (state == State::MASSAGING) elapsed = millis() - sessionStartedAt;
  else if (state == State::PAUSED) elapsed = pausedAt - sessionStartedAt;
  doc["remaining_s"] = elapsed >= sessionDurationMs ? 0 : (sessionDurationMs - elapsed) / 1000;
  char buffer[256];
  const size_t length = serializeJson(doc, buffer, sizeof(buffer));
  telemetryCharacteristic->setValue(reinterpret_cast<uint8_t*>(buffer), length);
  telemetryCharacteristic->notify();
}

void setupBle() {
  commandQueue = xQueueCreate(6, sizeof(BleCommand));
  BLEDevice::init(BleConfig::DEVICE_NAME);
  BLEServer* server = BLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());
  BLEService* service = server->createService(BleConfig::SERVICE_UUID);
  BLECharacteristic* commandCharacteristic = service->createCharacteristic(
      BleConfig::COMMAND_UUID, BLECharacteristic::PROPERTY_WRITE);
  commandCharacteristic->setCallbacks(new CommandCallbacks());
  telemetryCharacteristic = service->createCharacteristic(
      BleConfig::TELEMETRY_UUID,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  telemetryCharacteristic->addDescriptor(new BLE2902());
  service->start();
  server->getAdvertising()->addServiceUUID(BleConfig::SERVICE_UUID);
  server->getAdvertising()->start();
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
  setupBle();
  Serial.println("Cloud-Parting Hand controller ready");
}

void loop() {
  if (disconnectPending) {
    disconnectPending = false;
    if (state != State::IDLE && state != State::FAULT) enterState(State::RELEASING);
  }

  BleCommand bleCommand{};
  while (commandQueue != nullptr && xQueueReceive(commandQueue, &bleCommand, 0) == pdTRUE) {
    processCommand(bleCommand.json);
  }

  if (buttonPressedEvent()) {
    if (state == State::IDLE) {
      faultCode = "none";
      faultAfterRelease = false;
      sessionStartedAt = millis();
      enterState(State::CLAMPING);
    }
    else if (state == State::FAULT) {
      faultCode = "none";
      faultAfterRelease = false;
      enterState(State::IDLE);
    }
    else enterState(State::RELEASING);
  }

  if (millis() - lastControlAt >= Control::CONTROL_PERIOD_MS) {
    lastControlAt = millis();
    controlStep();
  }

  static uint32_t lastTelemetryAt = 0;
  if (millis() - lastTelemetryAt >= Control::TELEMETRY_PERIOD_MS) {
    lastTelemetryAt = millis();
    publishTelemetry();
  }

  static uint32_t lastReportAt = 0;
  if (millis() - lastReportAt >= 500) {
    lastReportAt = millis();
    Serial.printf("state=%u pressure=%d,%d limits=%d,%d\n",
                  static_cast<unsigned>(state), pressureLeft, pressureRight,
                  leftLimit(), rightLimit());
  }
}

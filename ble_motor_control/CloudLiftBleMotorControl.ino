#include <Arduino.h>
#if __has_include(<esp_arduino_version.h>)
#include <esp_arduino_version.h>
#endif
#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>

#ifndef ESP_ARDUINO_VERSION_MAJOR
#define ESP_ARDUINO_VERSION_MAJOR 2
#endif

// CloudLift BLE manual motor-control firmware v1.0.
// Motors are stopped at boot. The strain sensor is telemetry only;
// it never starts, stops or reverses a motor in this firmware.

// Confirmed wiring: displacement motor 1 (TB6612 A channel).
constexpr uint8_t MOVE1_PWM = 4;  // PWMA
constexpr uint8_t MOVE1_IN2 = 5;  // AIN2
constexpr uint8_t MOVE1_IN1 = 6;  // AIN1

// Confirmed wiring: massage motor 1 (TB6612 C channel).
constexpr uint8_t MASSAGE1_PWM = 40;  // PWMC
constexpr uint8_t MASSAGE1_IN2 = 41;  // CIN2
constexpr uint8_t MASSAGE1_IN1 = 42;  // CIN1

// Confirmed wiring: displacement motor 2 (TB6612 D channel).
constexpr uint8_t MOVE2_PWM = 47;  // PWMD
constexpr uint8_t MOVE2_IN2 = 48;  // DIN2
constexpr uint8_t MOVE2_IN1 = 45;  // DIN1

// Confirmed wiring: massage motor 2 (TB6612 B channel).
constexpr uint8_t MASSAGE2_PWM = 9;   // PWMB
constexpr uint8_t MASSAGE2_IN2 = 10;  // BIN2
constexpr uint8_t MASSAGE2_IN1 = 11;  // BIN1

constexpr uint8_t STBY_PIN = 17;
constexpr uint8_t STRAIN_AO_PIN = 14;

constexpr uint8_t MASSAGE1_CHANNEL = 0;
constexpr uint8_t MASSAGE2_CHANNEL = 1;
constexpr uint32_t PWM_FREQUENCY = 20000;
constexpr uint8_t PWM_RESOLUTION = 8;
constexpr uint32_t STRAIN_SAMPLE_MS = 20;
constexpr uint32_t TELEMETRY_MS = 500;
constexpr uint32_t STRAIN_CALIBRATION_MS = 1500;
constexpr uint32_t COMMAND_TIMEOUT_MS = 10000;
constexpr size_t MAX_COMMAND_BYTES = 192;

constexpr char DEVICE_NAME[] = "CloudLift";
constexpr char SERVICE_UUID[] = "7e400001-b5a3-f393-e0a9-e50e24dcca9e";
constexpr char COMMAND_UUID[] = "7e400002-b5a3-f393-e0a9-e50e24dcca9e";
constexpr char TELEMETRY_UUID[] = "7e400003-b5a3-f393-e0a9-e50e24dcca9e";

BLECharacteristic* telemetryCharacteristic = nullptr;
QueueHandle_t commandQueue = nullptr;
volatile bool bleConnected = false;
volatile bool disconnectPending = false;
bool statusRequested = false;

int moveDirection = 0;  // -1 or +1; 0=stop
int massageSpeed = 0;   // 0..255
int configuredMassageSpeed = 200;
const char* faultCode = "none";
uint32_t lastCommandAt = 0;

int strainRaw = 0;
int strainFiltered = 0;
int strainBaseline = 0;
int strainDelta = 0;
bool strainFilterReady = false;
bool strainBaselineReady = false;
uint32_t strainBaselineSum = 0;
uint32_t strainBaselineSamples = 0;
uint32_t lastStrainAt = 0;
uint32_t lastTelemetryAt = 0;

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

void drivePwmMotor(uint8_t in1, uint8_t in2, uint8_t pwm, uint8_t channel,
                  int speed) {
  speed = constrain(speed, -255, 255);
  digitalWrite(in1, speed > 0 ? HIGH : LOW);
  digitalWrite(in2, speed < 0 ? HIGH : LOW);
  writePwm(pwm, channel, abs(speed));
}

void driveDirectMotor(uint8_t in1, uint8_t in2, uint8_t pwm, int direction) {
  digitalWrite(in1, direction > 0 ? HIGH : LOW);
  digitalWrite(in2, direction < 0 ? HIGH : LOW);
  digitalWrite(pwm, direction == 0 ? LOW : HIGH);
}

void setMove(int direction) {
  moveDirection = constrain(direction, -1, 1);
  // Both displacement motors use the same confirmed electrical direction.
  driveDirectMotor(MOVE1_IN1, MOVE1_IN2, MOVE1_PWM, moveDirection);
  driveDirectMotor(MOVE2_IN1, MOVE2_IN2, MOVE2_PWM, moveDirection);
}

void setMassage(int speed) {
  massageSpeed = constrain(speed, 0, 255);
  drivePwmMotor(MASSAGE1_IN1, MASSAGE1_IN2, MASSAGE1_PWM,
                MASSAGE1_CHANNEL, massageSpeed);
  // The second massage motor is electrically mirrored.
  drivePwmMotor(MASSAGE2_IN1, MASSAGE2_IN2, MASSAGE2_PWM,
                MASSAGE2_CHANNEL, -massageSpeed);
}

void stopAll() {
  setMove(0);
  setMassage(0);
}

bool motorsActive() { return moveDirection != 0 || massageSpeed != 0; }

const char* stateName() {
  if (moveDirection != 0 && massageSpeed != 0) return "combined";
  if (moveDirection != 0) return "moving";
  if (massageSpeed != 0) return "massaging";
  return "idle";
}

bool readStringJson(const char* source, const char* key, char* output,
                   size_t outputSize) {
  char pattern[32];
  snprintf(pattern, sizeof(pattern), "\"%s\"", key);
  const char* position = strstr(source, pattern);
  if (!position) return false;
  const char* colon = strchr(position + strlen(pattern), ':');
  if (!colon) return false;
  const char* firstQuote = strchr(colon, '"');
  if (!firstQuote) return false;
  const char* endQuote = strchr(firstQuote + 1, '"');
  if (!endQuote) return false;
  const size_t length = static_cast<size_t>(endQuote - firstQuote - 1);
  if (length >= outputSize) return false;
  memcpy(output, firstQuote + 1, length);
  output[length] = '\0';
  return true;
}

int readIntJson(const char* source, const char* key, int fallback) {
  char pattern[32];
  snprintf(pattern, sizeof(pattern), "\"%s\"", key);
  const char* position = strstr(source, pattern);
  if (!position) return fallback;
  const char* colon = strchr(position + strlen(pattern), ':');
  if (!colon) return fallback;
  return atoi(colon + 1);
}

void processCommand(const char* json) {
  char command[32]{};
  if (!readStringJson(json, "cmd", command, sizeof(command))) {
    Serial.println("BLE command rejected: missing cmd");
    return;
  }

  lastCommandAt = millis();
  faultCode = "none";

  if (!strcmp(command, "MOVE")) {
    setMove(readIntJson(json, "direction", 0));
  } else if (!strcmp(command, "MOVE_OPEN")) {
    setMove(+1);
  } else if (!strcmp(command, "MOVE_CLOSE")) {
    setMove(-1);
  } else if (!strcmp(command, "MOVE_STOP")) {
    setMove(0);
  } else if (!strcmp(command, "MASSAGE")) {
    setMassage(readIntJson(json, "speed", configuredMassageSpeed));
  } else if (!strcmp(command, "MASSAGE_START") || !strcmp(command, "START")) {
    setMassage(readIntJson(json, "speed", configuredMassageSpeed));
  } else if (!strcmp(command, "MASSAGE_STOP") || !strcmp(command, "PAUSE")) {
    setMassage(0);
  } else if (!strcmp(command, "STOP") || !strcmp(command, "STOP_ALL")) {
    stopAll();
  } else if (!strcmp(command, "SET_SPEED")) {
    const int level = constrain(readIntJson(json, "value", 3), 1, 5);
    static constexpr uint8_t levels[] = {120, 150, 180, 210, 240};
    configuredMassageSpeed = levels[level - 1];
    if (massageSpeed != 0) setMassage(configuredMassageSpeed);
  } else if (!strcmp(command, "PING") || !strcmp(command, "GET_STATUS")) {
    statusRequested = true;
  } else {
    Serial.printf("BLE command rejected: %s\n", command);
    return;
  }

  Serial.printf("BLE cmd=%s move=%d massage=%d\n", command, moveDirection,
                massageSpeed);
  statusRequested = true;
}

void sampleStrain(uint32_t now) {
  if (now - lastStrainAt < STRAIN_SAMPLE_MS) return;
  lastStrainAt = now;
  strainRaw = analogRead(STRAIN_AO_PIN);
  if (!strainFilterReady) {
    strainFiltered = strainRaw;
    strainFilterReady = true;
  } else {
    strainFiltered = (strainFiltered * 7 + strainRaw) / 8;
  }

  if (!strainBaselineReady) {
    if (now <= STRAIN_CALIBRATION_MS) {
      strainBaselineSum += strainRaw;
      strainBaselineSamples++;
    } else if (strainBaselineSamples > 0) {
      strainBaseline = strainBaselineSum / strainBaselineSamples;
      strainBaselineReady = true;
    }
  }
  if (strainBaselineReady) strainDelta = max(0, strainBaseline - strainFiltered);
}

void publishTelemetry() {
  if (!bleConnected || telemetryCharacteristic == nullptr) return;
  char buffer[244];
  const int written = snprintf(
      buffer, sizeof(buffer),
      "{\"version\":\"1.0\",\"state\":\"%s\",\"move\":%d,"
      "\"massage\":%d,\"strain_raw\":%d,\"strain_delta\":%d,"
      "\"baseline\":%d,\"fault\":\"%s\"}",
      stateName(), moveDirection, massageSpeed, strainRaw, strainDelta,
      strainBaseline, faultCode);
  if (written <= 0 || written >= static_cast<int>(sizeof(buffer))) return;
  telemetryCharacteristic->setValue(reinterpret_cast<uint8_t*>(buffer),
                                     static_cast<size_t>(written));
  telemetryCharacteristic->notify();
}

struct BleCommand {
  char json[MAX_COMMAND_BYTES];
};

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer*) override {
    bleConnected = true;
    lastCommandAt = millis();
    Serial.println("BLE connected");
  }

  void onDisconnect(BLEServer*) override {
    bleConnected = false;
    disconnectPending = true;
    BLEDevice::startAdvertising();
    Serial.println("BLE disconnected; motors will stop");
  }
};

class CommandCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* characteristic) override {
    String value = characteristic->getValue();
    if (value.isEmpty() || value.length() >= MAX_COMMAND_BYTES) return;
    BleCommand command{};
    memcpy(command.json, value.c_str(), value.length());
    command.json[value.length()] = '\0';
    if (commandQueue != nullptr) xQueueSend(commandQueue, &command, 0);
  }
};

void setupBle() {
  commandQueue = xQueueCreate(8, sizeof(BleCommand));
  BLEDevice::init(DEVICE_NAME);
  BLEDevice::setMTU(247);
  BLEServer* server = BLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());
  BLEService* service = server->createService(SERVICE_UUID);

  BLECharacteristic* commandCharacteristic = service->createCharacteristic(
      COMMAND_UUID, BLECharacteristic::PROPERTY_WRITE |
                        BLECharacteristic::PROPERTY_WRITE_NR);
  commandCharacteristic->setCallbacks(new CommandCallbacks());

  telemetryCharacteristic = service->createCharacteristic(
      TELEMETRY_UUID,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  telemetryCharacteristic->addDescriptor(new BLE2902());
  service->start();

  BLEAdvertising* advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(SERVICE_UUID);
  advertising->setScanResponse(true);
  advertising->start();
}

void setup() {
  Serial.begin(115200);
  pinMode(STBY_PIN, OUTPUT);
  digitalWrite(STBY_PIN, HIGH);
  pinMode(STRAIN_AO_PIN, INPUT);
  analogReadResolution(12);

  const uint8_t directPins[] = {
      MOVE1_IN1, MOVE1_IN2, MOVE1_PWM, MOVE2_IN1, MOVE2_IN2, MOVE2_PWM};
  for (uint8_t pin : directPins) pinMode(pin, OUTPUT);
  pinMode(MASSAGE1_IN1, OUTPUT);
  pinMode(MASSAGE1_IN2, OUTPUT);
  pinMode(MASSAGE2_IN1, OUTPUT);
  pinMode(MASSAGE2_IN2, OUTPUT);
  attachPwm(MASSAGE1_PWM, MASSAGE1_CHANNEL);
  attachPwm(MASSAGE2_PWM, MASSAGE2_CHANNEL);
  stopAll();
  setupBle();
  lastCommandAt = millis();
  Serial.println("CloudLift BLE manual control v1.0 ready; all motors stopped");
}

void loop() {
  const uint32_t now = millis();
  sampleStrain(now);

  if (disconnectPending) {
    disconnectPending = false;
    stopAll();
    faultCode = "ble_disconnected";
    statusRequested = true;
  }

  BleCommand command{};
  while (commandQueue != nullptr &&
         xQueueReceive(commandQueue, &command, 0) == pdTRUE) {
    processCommand(command.json);
  }

  if (bleConnected && motorsActive() &&
      now - lastCommandAt > COMMAND_TIMEOUT_MS) {
    stopAll();
    faultCode = "command_timeout";
    Serial.println("BLE command timeout; all motors stopped");
    statusRequested = true;
  }

  if (statusRequested || now - lastTelemetryAt >= TELEMETRY_MS) {
    statusRequested = false;
    lastTelemetryAt = now;
    publishTelemetry();
  }
  delay(2);
}

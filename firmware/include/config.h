#pragma once

#include <Arduino.h>

// ESP32 DevKit V1 -> two TB6612FNG boards. Both boards share STBY.
namespace Pins {
constexpr uint8_t STBY = 13;

constexpr uint8_t ARC_LEFT_IN1 = 16;
constexpr uint8_t ARC_LEFT_IN2 = 17;
constexpr uint8_t ARC_LEFT_PWM = 25;
constexpr uint8_t ARC_RIGHT_IN1 = 18;
constexpr uint8_t ARC_RIGHT_IN2 = 19;
constexpr uint8_t ARC_RIGHT_PWM = 26;

constexpr uint8_t ROLLER_LEFT_IN1 = 21;
constexpr uint8_t ROLLER_LEFT_IN2 = 22;
constexpr uint8_t ROLLER_LEFT_PWM = 27;
constexpr uint8_t ROLLER_RIGHT_IN1 = 23;
constexpr uint8_t ROLLER_RIGHT_IN2 = 5;
constexpr uint8_t ROLLER_RIGHT_PWM = 14;

// Normally-closed switches to GND. LOW=healthy, HIGH=limit hit/wire broken.
constexpr uint8_t LIMIT_LEFT = 34;
constexpr uint8_t LIMIT_RIGHT = 35;

// ADC1 pins remain usable if Wi-Fi is enabled later.
constexpr uint8_t PRESSURE_LEFT = 36;
constexpr uint8_t PRESSURE_RIGHT = 39;
constexpr uint8_t START_STOP = 32;  // push button to GND
constexpr uint8_t STATUS_LED = 2;
}  // namespace Pins

namespace Control {
constexpr int ARC_CLOSE_SPEED = 115;       // 0..255
constexpr int ARC_TRAVERSE_SPEED = 105;
constexpr int ROLLER_SPEED = 145;
constexpr int PRESSURE_TARGET_RAW = 1500;  // calibrate before skin contact
constexpr int PRESSURE_MAX_RAW = 2200;
constexpr int PRESSURE_RELEASE_RAW = 250;
constexpr uint32_t CLAMP_TIMEOUT_MS = 5000;
constexpr uint32_t DEBOUNCE_MS = 40;
constexpr uint32_t CONTROL_PERIOD_MS = 10;
constexpr uint32_t TELEMETRY_PERIOD_MS = 500;
constexpr uint32_t DEFAULT_SESSION_MS = 10UL * 60UL * 1000UL;

// Change signs after the first no-load direction test if mechanics run opposite.
constexpr int8_t ARC_LEFT_CLOSE_SIGN = +1;
constexpr int8_t ARC_RIGHT_CLOSE_SIGN = -1;
constexpr int8_t ROLLER_LEFT_SIGN = +1;
constexpr int8_t ROLLER_RIGHT_SIGN = -1;
}  // namespace Control

namespace BleConfig {
constexpr char DEVICE_NAME[] = "CloudLift";
constexpr char SERVICE_UUID[] = "7e400001-b5a3-f393-e0a9-e50e24dcca9e";
constexpr char COMMAND_UUID[] = "7e400002-b5a3-f393-e0a9-e50e24dcca9e";
constexpr char TELEMETRY_UUID[] = "7e400003-b5a3-f393-e0a9-e50e24dcca9e";
constexpr size_t MAX_COMMAND_BYTES = 192;
}  // namespace BleConfig

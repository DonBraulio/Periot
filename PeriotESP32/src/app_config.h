#pragma once

#include <Arduino.h>

namespace AppConfig {

constexpr char WIFI_SSID[] = "Peru";
constexpr char WIFI_PASSWORD[] = "BM875301";

constexpr uint8_t RF_RX_PIN = 3;
constexpr int DIMMING_PER_DETENT = 5;
constexpr int DEFAULT_DIMMING = 50;

}  // namespace AppConfig

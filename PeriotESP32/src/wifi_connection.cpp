#include "wifi_connection.h"

#include <Arduino.h>
#include <WiFi.h>

bool connectWifi(const char* ssid, const char* password,
                 unsigned long timeoutMs) {
  Serial.print("Connecting to Wi-Fi network ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  const unsigned long startMs = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startMs < timeoutMs) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wi-Fi connection timed out; lamp control is unavailable");
    return false;
  }

  Serial.print("Wi-Fi connected: IP=");
  Serial.print(WiFi.localIP());
  Serial.print(" RSSI=");
  Serial.print(WiFi.RSSI());
  Serial.println(" dBm");
  return true;
}

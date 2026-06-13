#include <Arduino.h>
#include <WiFi.h>

#define WIFI_CHECK_PERIOD 5000
#define TEST_PIN 2

const char* WIFI_SSID = "Peru";
const char* WIFI_PASSWORD = "BM875301";
unsigned long lastWifiCheck = 0;

void setupWIFI(int maxRetry) {
  Serial.println();
  Serial.println("Connecting WiFi...");
  Serial.print("SSID: ");
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < maxRetry) {
    delay(1000);
    Serial.print(".");
    retries++;
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi connected!");
    Serial.print("IP local: ");
    Serial.println(WiFi.localIP());

    Serial.print("RSSI: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
  } else {
    Serial.println("Could not connect WiFi.");
    Serial.print("WiFi status: ");
    Serial.println(WiFi.status());
  }
}

void wifiCheck() {
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WiFi Connected -> IP: ");
    Serial.print(WiFi.localIP());
    Serial.print(" | RSSI: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
  } else {
    Serial.println("WiFi disconnected. Retrying...");
    WiFi.disconnect();
    setupWIFI(30);
  }
}

void setup() {
  pinMode(TEST_PIN, OUTPUT);

  delay(2000);
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("Serial OK");
}

void loop() {
  if ((millis() - lastWifiCheck) >= WIFI_CHECK_PERIOD) {
    lastWifiCheck = millis();
    wifiCheck();
  }

  Serial.print("Uptime: ");
  Serial.print(millis());
  Serial.println(" ms");

  digitalWrite(TEST_PIN, HIGH);
  delay(1000);

  digitalWrite(TEST_PIN, LOW);
  delay(1000);
}
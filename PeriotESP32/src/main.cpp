#include <Arduino.h>
#include <WiFi.h>
#include <wiz_control.h>

#define WIFI_CHECK_PERIOD 5000
#define TEST_PIN 2
#define MAC_LAMP_LIVING "D8A011E712FD"

const char* WIFI_SSID = "Peru";
const char* WIFI_PASSWORD = "BM875301";
unsigned long lastWifiCheck = 0;

std::vector<String> targetWizMacs = {MAC_LAMP_LIVING};

WizIpMap wizLights;

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

bool wifiReconnect() {
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WiFi Connected -> IP: ");
    Serial.print(WiFi.localIP());
    Serial.print(" | RSSI: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
    return false;
  } else {
    Serial.println("WiFi disconnected. Retrying...");
    WiFi.disconnect();
    setupWIFI(30);
    return true;
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
    if (wifiReconnect() && WiFi.status() == WL_CONNECTED) {
      wizLights = getWizIPs(targetWizMacs);
    }

    for (const auto& entry : wizLights) {
      IPAddress ip = entry.second;
      for (int intensity = 0; intensity < 100; intensity += 25) {
        setWizWarm(intensity, ip);
        delay(100);
      }
    }
  }

  Serial.print("Uptime: ");
  Serial.print(millis());
  Serial.println(" ms");

  digitalWrite(TEST_PIN, HIGH);
  delay(1000);

  digitalWrite(TEST_PIN, LOW);
  delay(1000);
}
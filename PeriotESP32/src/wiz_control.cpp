#include "wiz_control.h"

#include <ArduinoJson.h>
#include <WiFi.h>
#include <WiFiUdp.h>

static WiFiUDP wizUdp;

static const uint16_t WIZ_PORT = 38899;
static const uint16_t LOCAL_UDP_PORT = 38900;

static bool udpStarted = false;

static void ensureUdpStarted() {
  if (!udpStarted) {
    wizUdp.begin(LOCAL_UDP_PORT);
    udpStarted = true;

    Serial.print("WiZ UDP listener started on local port ");
    Serial.println(LOCAL_UDP_PORT);
  }
}

static String normalizeMac(String mac) {
  mac.toUpperCase();
  mac.replace(":", "");
  mac.replace("-", "");
  mac.replace(" ", "");
  return mac;
}

static bool isRequestedMac(const String& mac,
                           const std::vector<String>& requestedMacs) {
  for (String requested : requestedMacs) {
    requested = normalizeMac(requested);

    if (mac == requested) {
      return true;
    }
  }

  return false;
}

static void sendUdpJson(IPAddress ip, const char* json) {
  ensureUdpStarted();

  Serial.print("Sending UDP to ");
  Serial.print(ip);
  Serial.print(":");
  Serial.print(WIZ_PORT);
  Serial.print(" -> ");
  Serial.println(json);

  wizUdp.beginPacket(ip, WIZ_PORT);
  wizUdp.write((const uint8_t*)json, strlen(json));
  wizUdp.endPacket();
}

static void sendDiscoveryBroadcast() {
  IPAddress broadcastIp(255, 255, 255, 255);

  // getPilot usually returns useful data such as mac, state, dimming, rssi,
  // etc.
  const char* discoveryJson = "{\"method\":\"getPilot\",\"params\":{}}";

  Serial.println("Sending WiZ discovery broadcast...");

  wizUdp.beginPacket(broadcastIp, WIZ_PORT);
  wizUdp.write((const uint8_t*)discoveryJson, strlen(discoveryJson));
  wizUdp.endPacket();
}

void setWizWarm(int dimming, IPAddress ip) {
  ensureUdpStarted();

  if (dimming < 10) {
    dimming = 10;
  }

  if (dimming > 100) {
    dimming = 100;
  }

  // Warm white. 2700K is a typical warm color temperature.
  char json[160];

  snprintf(json, sizeof(json),
           "{\"method\":\"setPilot\",\"params\":{\"state\":true,\"temp\":2700,"
           "\"dimming\":%d}}",
           dimming);

  sendUdpJson(ip, json);
}

WizIpMap getWizIPs(const std::vector<String>& macAddresses) {
  ensureUdpStarted();

  WizIpMap requestedLights;

  const unsigned long timeoutMs = 3000;
  const unsigned long checkIntervalMs = 500;

  unsigned long start = millis();
  unsigned long lastBroadcast = 0;

  Serial.println("Starting WiZ discovery...");
  Serial.print("Looking for ");
  Serial.print(macAddresses.size());
  Serial.println(" requested MAC address(es).");

  while (millis() - start < timeoutMs) {
    // Re-send discovery every 500 ms.
    if (millis() - lastBroadcast >= checkIntervalMs || lastBroadcast == 0) {
      sendDiscoveryBroadcast();
      lastBroadcast = millis();
    }

    int packetSize = wizUdp.parsePacket();

    if (packetSize > 0) {
      char buffer[768];

      int len = wizUdp.read(buffer, sizeof(buffer) - 1);
      buffer[len] = '\0';

      IPAddress remoteIp = wizUdp.remoteIP();

      StaticJsonDocument<768> doc;
      DeserializationError error = deserializeJson(doc, buffer);

      if (error) {
        Serial.print("Received invalid JSON from ");
        Serial.print(remoteIp);
        Serial.print(": ");
        Serial.println(buffer);
        continue;
      }

      const char* method = doc["method"] | "";
      JsonObject result = doc["result"];

      const char* macRaw = result["mac"] | "";

      if (strlen(macRaw) == 0) {
        Serial.print("Received WiZ response without MAC from ");
        Serial.print(remoteIp);
        Serial.print(": ");
        Serial.println(buffer);
        continue;
      }

      String mac = normalizeMac(String(macRaw));

      Serial.print("Found WiZ lamp - MAC: ");
      Serial.print(mac);
      Serial.print(" | IP: ");
      Serial.print(remoteIp);
      Serial.print(" | method: ");
      Serial.println(method);

      if (isRequestedMac(mac, macAddresses)) {
        requestedLights[mac] = remoteIp;

        Serial.print("Matched requested WiZ lamp - MAC: ");
        Serial.print(mac);
        Serial.print(" | IP: ");
        Serial.println(remoteIp);

        if (requestedLights.size() == macAddresses.size()) {
          Serial.println("All requested WiZ lamps were found.");
          return requestedLights;
        }
      }
    }

    delay(10);
  }

  Serial.println("WiZ discovery finished due to timeout.");
  Serial.print("Requested lamps found: ");
  Serial.print(requestedLights.size());
  Serial.print("/");
  Serial.println(macAddresses.size());

  return requestedLights;
}
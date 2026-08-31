#include "wiz_control.h"

#include <ArduinoJson.h>
#include <WiFiUdp.h>

namespace {

constexpr uint16_t WIZ_PORT = 38899;
constexpr uint16_t LOCAL_UDP_PORT = 38900;
constexpr unsigned long DISCOVERY_INTERVAL_MS = 500;

WiFiUDP wizUdp;
bool udpStarted = false;

void ensureUdpStarted() {
  if (udpStarted) {
    return;
  }

  udpStarted = wizUdp.begin(LOCAL_UDP_PORT) == 1;
  if (udpStarted) {
    Serial.print("WiZ UDP listener started on local port ");
    Serial.println(LOCAL_UDP_PORT);
  } else {
    Serial.println("WiZ UDP listener could not be started");
  }
}

bool sendUdpJson(const IPAddress& ip, const char* json) {
  ensureUdpStarted();
  if (!udpStarted || wizUdp.beginPacket(ip, WIZ_PORT) != 1) {
    return false;
  }

  wizUdp.write(reinterpret_cast<const uint8_t*>(json), strlen(json));
  return wizUdp.endPacket() == 1;
}

void sendDiscoveryBroadcast() {
  static const char discoveryJson[] =
      "{\"method\":\"getPilot\",\"params\":{}}";

  if (!sendUdpJson(IPAddress(255, 255, 255, 255), discoveryJson)) {
    Serial.println("WiZ discovery broadcast failed");
  }
}

void updateDiscoveredLight(WizLightList& lights, const String& mac,
                           const IPAddress& ip, bool state, int dimming,
                           int rssi) {
  WizLight* existing = findWizLightByMac(lights, mac);
  if (existing != nullptr) {
    existing->ip = ip;
    existing->state = state;
    existing->dimming = dimming;
    existing->rssi = rssi;
    return;
  }

  lights.push_back({mac, ip, state, dimming, rssi});

  Serial.print("Found WiZ lamp: MAC=");
  Serial.print(mac);
  Serial.print(" IP=");
  Serial.print(ip);
  Serial.print(" state=");
  Serial.print(state ? "on" : "off");
  Serial.print(" dimming=");
  if (dimming >= 0) {
    Serial.print(dimming);
  } else {
    Serial.print("unknown");
  }
  Serial.print(" rssi=");
  Serial.println(rssi);
}

}  // namespace

String normalizeWizMac(String mac) {
  mac.toUpperCase();
  mac.replace(":", "");
  mac.replace("-", "");
  mac.replace(" ", "");
  return mac;
}

WizLightList discoverWizLights(unsigned long timeoutMs) {
  ensureUdpStarted();

  WizLightList lights;
  const unsigned long startMs = millis();
  unsigned long lastBroadcastMs = 0;

  Serial.println("Starting WiZ discovery...");

  while (millis() - startMs < timeoutMs) {
    const unsigned long now = millis();
    if (lastBroadcastMs == 0 ||
        now - lastBroadcastMs >= DISCOVERY_INTERVAL_MS) {
      sendDiscoveryBroadcast();
      lastBroadcastMs = now;
    }

    const int packetSize = wizUdp.parsePacket();
    if (packetSize <= 0) {
      delay(5);
      continue;
    }

    char buffer[768];
    const int length = wizUdp.read(buffer, sizeof(buffer) - 1);
    if (length <= 0) {
      continue;
    }
    buffer[length] = '\0';

    JsonDocument document;
    const DeserializationError error = deserializeJson(document, buffer);
    if (error) {
      Serial.print("Ignoring invalid WiZ JSON from ");
      Serial.println(wizUdp.remoteIP());
      continue;
    }

    const JsonObjectConst result = document["result"].as<JsonObjectConst>();
    const char* rawMac = result["mac"] | "";
    const String mac = normalizeWizMac(String(rawMac));
    if (mac.length() != 12) {
      continue;
    }

    const bool state = result["state"] | false;
    const int dimming = result["dimming"] | -1;
    const int rssi = result["rssi"] | 0;
    updateDiscoveredLight(lights, mac, wizUdp.remoteIP(), state, dimming,
                          rssi);
  }

  Serial.print("WiZ discovery finished: ");
  Serial.print(lights.size());
  Serial.println(" lamp(s) found");
  return lights;
}

WizLight* findWizLightByMac(WizLightList& lights, const String& mac) {
  const String normalizedMac = normalizeWizMac(mac);
  for (WizLight& light : lights) {
    if (light.mac == normalizedMac) {
      return &light;
    }
  }
  return nullptr;
}

const WizLight* findWizLightByMac(const WizLightList& lights,
                                 const String& mac) {
  const String normalizedMac = normalizeWizMac(mac);
  for (const WizLight& light : lights) {
    if (light.mac == normalizedMac) {
      return &light;
    }
  }
  return nullptr;
}

bool setWizDimming(WizLight& light, int dimming) {
  dimming = constrain(dimming, 10, 100);

  char json[112];
  snprintf(json, sizeof(json),
           "{\"method\":\"setPilot\",\"params\":{\"state\":true,"
           "\"dimming\":%d}}",
           dimming);

  if (!sendUdpJson(light.ip, json)) {
    return false;
  }

  light.state = true;
  light.dimming = dimming;
  return true;
}

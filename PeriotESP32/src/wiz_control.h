#pragma once

#include <Arduino.h>
#include <IPAddress.h>

#include <vector>

struct WizLight {
  String mac;
  IPAddress ip;
  bool state;
  int dimming;
  int rssi;
};

using WizLightList = std::vector<WizLight>;

String normalizeWizMac(String mac);
WizLightList discoverWizLights(unsigned long timeoutMs = 3000);
WizLight* findWizLightByMac(WizLightList& lights, const String& mac);
const WizLight* findWizLightByMac(const WizLightList& lights,
                                 const String& mac);

// Level zero turns the lamp off. A positive level turns it on without changing
// its color mode and is clamped to the WiZ dimming range of 10..100.
bool setWizDimming(WizLight& light, int dimming);

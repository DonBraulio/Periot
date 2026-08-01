#pragma once

#include <Arduino.h>
#include <IPAddress.h>

#include <map>
#include <vector>

// Type alias: this is our "dictionary" type.
// Example:
//   WizIpMap lights;
//   lights["a8bb50a4f94d"] = IPAddress(192, 168, 1, 40);
using WizIpMap = std::map<String, IPAddress>;

// Sets a WiZ lamp to warm white with the requested dimming.
// dimming is clamped to the 10..100 range.
void setWizWarm(int dimming, IPAddress ip);

// Discovers WiZ lamps and returns a MAC -> IP map for the requested MAC
// addresses. It also prints every WiZ lamp it finds through Serial.
WizIpMap getWizIPs(const std::vector<String>& macAddresses);
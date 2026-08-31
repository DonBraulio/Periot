#pragma once

#include <Arduino.h>

constexpr uint8_t WIZ_NODE_COUNT = 16;
constexpr uint8_t MAX_WIZ_LIGHTS_PER_NODE = 4;

void setupWizPairings();
uint8_t getWizPairingCount(uint8_t nodeId);
String getWizPairedMac(uint8_t nodeId, uint8_t pairingIndex);
bool isWizPaired(uint8_t nodeId, const String& mac);
bool addWizPairing(uint8_t nodeId, const String& mac);
bool removeWizPairing(uint8_t nodeId, const String& mac);
bool clearWizPairings(uint8_t nodeId);

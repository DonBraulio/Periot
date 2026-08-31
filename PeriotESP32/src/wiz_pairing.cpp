#include "wiz_pairing.h"

#include <Preferences.h>

#include <cctype>
#include <cstring>

#include "wiz_control.h"

namespace {

constexpr uint32_t STORAGE_MAGIC = 0x57495A50;  // "WIZP"
constexpr uint8_t STORAGE_VERSION = 1;
constexpr char STORAGE_NAMESPACE[] = "wiz_pairing";
constexpr char STORAGE_KEY[] = "mappings";

struct StoredPairing {
  uint8_t used;
  char mac[13];
};

struct StoredPairingState {
  uint32_t magic;
  uint8_t version;
  StoredPairing nodes[WIZ_NODE_COUNT][MAX_WIZ_LIGHTS_PER_NODE];
};

StoredPairingState pairingState = {};

void initializeEmptyState() {
  pairingState = {};
  pairingState.magic = STORAGE_MAGIC;
  pairingState.version = STORAGE_VERSION;
}

bool isValidMac(const String& mac) {
  if (mac.length() != 12) {
    return false;
  }

  for (size_t index = 0; index < mac.length(); ++index) {
    if (!std::isxdigit(static_cast<unsigned char>(mac[index]))) {
      return false;
    }
  }
  return true;
}

bool persistPairings() {
  Preferences preferences;
  if (!preferences.begin(STORAGE_NAMESPACE, false)) {
    Serial.println("WiZ pairing: could not open NVS for writing");
    return false;
  }

  const size_t written =
      preferences.putBytes(STORAGE_KEY, &pairingState, sizeof(pairingState));
  preferences.end();

  if (written != sizeof(pairingState)) {
    Serial.println("WiZ pairing: failed to persist all mappings");
    return false;
  }
  return true;
}

void copyMac(StoredPairing& pairing, const String& mac) {
  pairing.used = 1;
  mac.toCharArray(pairing.mac, sizeof(pairing.mac));
}

}  // namespace

void setupWizPairings() {
  initializeEmptyState();

  Preferences preferences;
  if (!preferences.begin(STORAGE_NAMESPACE, true)) {
    Serial.println("WiZ pairing: NVS unavailable; starting with no mappings");
    return;
  }

  if (preferences.getBytesLength(STORAGE_KEY) == sizeof(pairingState)) {
    StoredPairingState stored = {};
    preferences.getBytes(STORAGE_KEY, &stored, sizeof(stored));
    if (stored.magic == STORAGE_MAGIC &&
        stored.version == STORAGE_VERSION) {
      pairingState = stored;
      Serial.println("WiZ pairing: restored mappings from NVS");
    }
  }

  preferences.end();
}

uint8_t getWizPairingCount(uint8_t nodeId) {
  if (nodeId >= WIZ_NODE_COUNT) {
    return 0;
  }

  uint8_t count = 0;
  for (const StoredPairing& pairing : pairingState.nodes[nodeId]) {
    if (pairing.used) {
      ++count;
    }
  }
  return count;
}

String getWizPairedMac(uint8_t nodeId, uint8_t pairingIndex) {
  if (nodeId >= WIZ_NODE_COUNT ||
      pairingIndex >= MAX_WIZ_LIGHTS_PER_NODE) {
    return String();
  }

  const StoredPairing& pairing = pairingState.nodes[nodeId][pairingIndex];
  return pairing.used ? String(pairing.mac) : String();
}

bool isWizPaired(uint8_t nodeId, const String& mac) {
  if (nodeId >= WIZ_NODE_COUNT) {
    return false;
  }

  const String normalizedMac = normalizeWizMac(mac);
  for (const StoredPairing& pairing : pairingState.nodes[nodeId]) {
    if (pairing.used && normalizedMac == pairing.mac) {
      return true;
    }
  }
  return false;
}

bool addWizPairing(uint8_t nodeId, const String& mac) {
  const String normalizedMac = normalizeWizMac(mac);
  if (nodeId >= WIZ_NODE_COUNT || !isValidMac(normalizedMac)) {
    return false;
  }
  if (isWizPaired(nodeId, normalizedMac)) {
    return true;
  }

  for (StoredPairing& pairing : pairingState.nodes[nodeId]) {
    if (!pairing.used) {
      const StoredPairing previous = pairing;
      copyMac(pairing, normalizedMac);
      if (persistPairings()) {
        return true;
      }
      pairing = previous;
      return false;
    }
  }
  return false;
}

bool removeWizPairing(uint8_t nodeId, const String& mac) {
  if (nodeId >= WIZ_NODE_COUNT) {
    return false;
  }

  const String normalizedMac = normalizeWizMac(mac);
  StoredPairing* nodePairings = pairingState.nodes[nodeId];
  for (uint8_t index = 0; index < MAX_WIZ_LIGHTS_PER_NODE; ++index) {
    if (!nodePairings[index].used || normalizedMac != nodePairings[index].mac) {
      continue;
    }

    StoredPairing previous[MAX_WIZ_LIGHTS_PER_NODE];
    memcpy(previous, nodePairings, sizeof(previous));
    for (uint8_t move = index; move + 1 < MAX_WIZ_LIGHTS_PER_NODE; ++move) {
      nodePairings[move] = nodePairings[move + 1];
    }
    nodePairings[MAX_WIZ_LIGHTS_PER_NODE - 1] = {};
    if (persistPairings()) {
      return true;
    }
    memcpy(nodePairings, previous, sizeof(previous));
    return false;
  }
  return false;
}

bool clearWizPairings(uint8_t nodeId) {
  if (nodeId >= WIZ_NODE_COUNT) {
    return false;
  }

  StoredPairing previous[MAX_WIZ_LIGHTS_PER_NODE];
  memcpy(previous, pairingState.nodes[nodeId], sizeof(previous));
  memset(pairingState.nodes[nodeId], 0, sizeof(pairingState.nodes[nodeId]));
  if (persistPairings()) {
    return true;
  }
  memcpy(pairingState.nodes[nodeId], previous, sizeof(previous));
  return false;
}

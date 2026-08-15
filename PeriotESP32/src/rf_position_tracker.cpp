#include "rf_position_tracker.h"

#include <Preferences.h>

namespace {

constexpr uint8_t MAX_NODES = 16;
constexpr uint32_t PERSISTENCE_DELAY_MS = 10000;
constexpr uint32_t STORAGE_MAGIC = 0x50455249;  // "PERI"
constexpr uint8_t STORAGE_VERSION = 1;
constexpr char STORAGE_NAMESPACE[] = "periot_rf";
constexpr char STORAGE_KEY[] = "nodes";

struct StoredNodeState {
  uint8_t valid;
  uint8_t bootId;
  uint8_t position;
};

struct StoredTrackerState {
  uint32_t magic;
  uint8_t version;
  StoredNodeState nodes[MAX_NODES];
};

StoredTrackerState trackerState = {};
bool persistenceDirty = false;
unsigned long lastPositionChangeMs = 0;

void initializeEmptyState() {
  trackerState = {};
  trackerState.magic = STORAGE_MAGIC;
  trackerState.version = STORAGE_VERSION;
}

void persistState() {
  Preferences preferences;
  if (!preferences.begin(STORAGE_NAMESPACE, false)) {
    Serial.println("RF tracker: could not open NVS for writing");
    return;
  }

  const size_t written =
      preferences.putBytes(STORAGE_KEY, &trackerState, sizeof(trackerState));
  preferences.end();

  if (written == sizeof(trackerState)) {
    persistenceDirty = false;
    Serial.println("RF tracker: node positions persisted");
  } else {
    Serial.println("RF tracker: failed to persist all node positions");
  }
}

}  // namespace

void setupRfPositionTracker() {
  initializeEmptyState();

  Preferences preferences;
  if (!preferences.begin(STORAGE_NAMESPACE, true)) {
    Serial.println("RF tracker: NVS unavailable; starting with empty state");
    return;
  }

  if (preferences.getBytesLength(STORAGE_KEY) == sizeof(trackerState)) {
    StoredTrackerState stored = {};
    preferences.getBytes(STORAGE_KEY, &stored, sizeof(stored));

    if (stored.magic == STORAGE_MAGIC &&
        stored.version == STORAGE_VERSION) {
      trackerState = stored;
      Serial.println("RF tracker: restored node positions from NVS");
    }
  }

  preferences.end();
}

RfPositionUpdate trackRfPosition(const RfFrame& frame) {
  StoredNodeState& previous = trackerState.nodes[frame.nodeId & 0x0F];
  const bool newBoot = !previous.valid || previous.bootId != frame.bootId;
  const uint8_t previousPosition = newBoot ? 0 : previous.position;
  const int16_t delta =
      RfProtocolSpec::positionDelta(frame.position, previousPosition);

  if (newBoot || previous.position != frame.position) {
    previous.valid = 1;
    previous.bootId = frame.bootId;
    previous.position = frame.position;
    persistenceDirty = true;
    lastPositionChangeMs = millis();
  }

  return {delta, newBoot};
}

void updateRfPositionPersistence() {
  if (!persistenceDirty ||
      millis() - lastPositionChangeMs < PERSISTENCE_DELAY_MS) {
    return;
  }

  persistState();
}

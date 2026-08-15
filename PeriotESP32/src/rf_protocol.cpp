#include "rf_protocol.h"

namespace {

// Set this to true if Saleae shows that receiver DAT inverts TX DAT.
constexpr bool RECEIVER_OUTPUT_INVERTED = false;

constexpr uint16_t MIN_PULSE_US = 150;
constexpr uint16_t MAX_PULSE_US = 5000;

struct TimingWindow {
  uint16_t minimumUs;
  uint16_t maximumUs;
};

constexpr TimingWindow MARK_WINDOW = {
    RfProtocolSpec::MARK_US - 150,
    RfProtocolSpec::MARK_US + 300,
};
constexpr TimingWindow ZERO_SPACE_WINDOW = {
    RfProtocolSpec::ZERO_SPACE_US - 150,
    RfProtocolSpec::ZERO_SPACE_US + 300,
};
constexpr TimingWindow ONE_SPACE_WINDOW = {
    RfProtocolSpec::ONE_SPACE_US - 300,
    RfProtocolSpec::ONE_SPACE_US + 400,
};
constexpr TimingWindow SYNC_SPACE_WINDOW = {
    RfProtocolSpec::SYNC_SPACE_US - 800,
    RfProtocolSpec::SYNC_SPACE_US + 1200,
};

constexpr uint16_t EDGE_BUFFER_SIZE = 128;
constexpr uint32_t STATS_PERIOD_MS = 5000;

struct EdgePulse {
  uint16_t durationUs;
  bool wasMark;
};

enum class DecoderState : uint8_t {
  WaitingPreamble,
  WaitingSyncSpace,
  ReadingBitMark,
  ReadingBitSpace,
};

struct DecoderStats {
  uint32_t decodedFrames = 0;
  uint32_t crcFailures = 0;
  uint32_t syncFailures = 0;
  uint32_t invalidPulses = 0;
  uint32_t incompleteFrames = 0;
};

struct TimingStats {
  uint16_t minUs = UINT16_MAX;
  uint16_t maxUs = 0;
  uint32_t totalUs = 0;
  uint32_t samples = 0;
};

uint8_t receiverPin = 0;
EdgePulse edgeBuffer[EDGE_BUFFER_SIZE];
volatile uint16_t edgeWriteIndex = 0;
volatile uint16_t edgeReadIndex = 0;
volatile uint32_t isrBufferOverflows = 0;
volatile uint32_t lastEdgeUs = 0;
portMUX_TYPE edgeBufferMux = portMUX_INITIALIZER_UNLOCKED;

DecoderState decoderState = DecoderState::WaitingPreamble;
DecoderStats decoderStats;
TimingStats validMarkTimings;
TimingStats validZeroSpaceTimings;
TimingStats validOneSpaceTimings;
TimingStats validSyncSpaceTimings;

uint8_t preambleCycles = 0;
bool preambleMarkSeen = false;
uint32_t payload = 0;
uint8_t bitsRead = 0;
uint16_t candidateTimings[RF_PAYLOAD_BITS * 2] = {};
uint8_t candidateTimingCount = 0;
uint16_t candidateSyncSpaceUs = 0;
unsigned long lastStatsMs = 0;

bool inRange(uint16_t value, uint16_t minimum, uint16_t maximum) {
  return value >= minimum && value <= maximum;
}

bool inWindow(uint16_t value, const TimingWindow& window) {
  return inRange(value, window.minimumUs, window.maximumUs);
}

bool isMark(uint16_t durationUs) {
  return inWindow(durationUs, MARK_WINDOW);
}

bool isZeroSpace(uint16_t durationUs) {
  return inWindow(durationUs, ZERO_SPACE_WINDOW);
}

bool isOneSpace(uint16_t durationUs) {
  return inWindow(durationUs, ONE_SPACE_WINDOW);
}

bool isSyncSpace(uint16_t durationUs) {
  return inWindow(durationUs, SYNC_SPACE_WINDOW);
}

void IRAM_ATTR onRfEdge() {
  const uint32_t nowUs = micros();
  const uint32_t durationUs = nowUs - lastEdgeUs;
  lastEdgeUs = nowUs;

  const bool completedPulseWasHigh = digitalRead(receiverPin) == LOW;
  const bool completedPulseWasMark =
      RECEIVER_OUTPUT_INVERTED ? !completedPulseWasHigh
                               : completedPulseWasHigh;

  portENTER_CRITICAL_ISR(&edgeBufferMux);
  const uint16_t nextWriteIndex = (edgeWriteIndex + 1) % EDGE_BUFFER_SIZE;
  if (nextWriteIndex == edgeReadIndex) {
    ++isrBufferOverflows;
  } else {
    edgeBuffer[edgeWriteIndex] = {
        static_cast<uint16_t>(durationUs > UINT16_MAX ? UINT16_MAX
                                                      : durationUs),
        completedPulseWasMark,
    };
    edgeWriteIndex = nextWriteIndex;
  }
  portEXIT_CRITICAL_ISR(&edgeBufferMux);
}

bool popEdgePulse(EdgePulse& pulse) {
  bool hasPulse = false;
  portENTER_CRITICAL(&edgeBufferMux);
  if (edgeReadIndex != edgeWriteIndex) {
    pulse = edgeBuffer[edgeReadIndex];
    edgeReadIndex = (edgeReadIndex + 1) % EDGE_BUFFER_SIZE;
    hasPulse = true;
  }
  portEXIT_CRITICAL(&edgeBufferMux);
  return hasPulse;
}

void resetDecoder() {
  decoderState = DecoderState::WaitingPreamble;
  preambleCycles = 0;
  preambleMarkSeen = false;
  payload = 0;
  bitsRead = 0;
  candidateTimingCount = 0;
  candidateSyncSpaceUs = 0;
}

void restartPreambleWith(const EdgePulse& pulse) {
  resetDecoder();
  if (pulse.wasMark && isMark(pulse.durationUs)) {
    preambleMarkSeen = true;
  }
}

void addTimingSample(TimingStats& stats, uint16_t durationUs) {
  stats.minUs = min(stats.minUs, durationUs);
  stats.maxUs = max(stats.maxUs, durationUs);
  stats.totalUs += durationUs;
  ++stats.samples;
}

void recordValidFrameTimings() {
  addTimingSample(validSyncSpaceTimings, candidateSyncSpaceUs);

  for (uint8_t i = 0; i < candidateTimingCount; ++i) {
    const uint16_t durationUs = candidateTimings[i];
    if ((i & 0x01) == 0) {
      addTimingSample(validMarkTimings, durationUs);
    } else if (isOneSpace(durationUs)) {
      addTimingSample(validOneSpaceTimings, durationUs);
    } else {
      addTimingSample(validZeroSpaceTimings, durationUs);
    }
  }
}

bool parsePayload(RfFrame& frame) {
  if (!RfProtocolSpec::parseFrame(payload, frame)) {
    ++decoderStats.crcFailures;
    return false;
  }

  ++decoderStats.decodedFrames;
  recordValidFrameTimings();
  return true;
}

bool processPulse(const EdgePulse& pulse, RfFrame& frame) {
  if (!inRange(pulse.durationUs, MIN_PULSE_US, MAX_PULSE_US)) {
    ++decoderStats.invalidPulses;
    resetDecoder();
    return false;
  }

  switch (decoderState) {
    case DecoderState::WaitingPreamble:
      if (pulse.wasMark) {
        if (isMark(pulse.durationUs)) {
          preambleMarkSeen = true;
          if (preambleCycles >=
              RfProtocolSpec::MIN_RECEIVED_PREAMBLE_CYCLES) {
            decoderState = DecoderState::WaitingSyncSpace;
          }
        } else {
          restartPreambleWith(pulse);
        }
      } else if (preambleMarkSeen && isZeroSpace(pulse.durationUs)) {
        if (preambleCycles < UINT8_MAX) {
          ++preambleCycles;
        }
        preambleMarkSeen = false;
      } else {
        resetDecoder();
      }
      break;

    case DecoderState::WaitingSyncSpace:
      if (pulse.wasMark || !preambleMarkSeen) {
        ++decoderStats.syncFailures;
        restartPreambleWith(pulse);
      } else if (isSyncSpace(pulse.durationUs)) {
        candidateSyncSpaceUs = pulse.durationUs;
        decoderState = DecoderState::ReadingBitMark;
        preambleMarkSeen = false;
        payload = 0;
        bitsRead = 0;
        candidateTimingCount = 0;
      } else if (isZeroSpace(pulse.durationUs)) {
        if (preambleCycles < UINT8_MAX) {
          ++preambleCycles;
        }
        preambleMarkSeen = false;
        decoderState = DecoderState::WaitingPreamble;
      } else {
        ++decoderStats.syncFailures;
        resetDecoder();
      }
      break;

    case DecoderState::ReadingBitMark:
      if (pulse.wasMark && isMark(pulse.durationUs)) {
        candidateTimings[candidateTimingCount++] = pulse.durationUs;
        decoderState = DecoderState::ReadingBitSpace;
      } else {
        ++decoderStats.invalidPulses;
        ++decoderStats.incompleteFrames;
        restartPreambleWith(pulse);
      }
      break;

    case DecoderState::ReadingBitSpace:
      if (pulse.wasMark ||
          (!isZeroSpace(pulse.durationUs) &&
           !isOneSpace(pulse.durationUs))) {
        ++decoderStats.invalidPulses;
        ++decoderStats.incompleteFrames;
        restartPreambleWith(pulse);
        break;
      }

      candidateTimings[candidateTimingCount++] = pulse.durationUs;
      payload =
          (payload << 1) | (isOneSpace(pulse.durationUs) ? 1 : 0);
      ++bitsRead;

      if (bitsRead == RF_PAYLOAD_BITS) {
        const bool validFrame = parsePayload(frame);
        resetDecoder();
        return validFrame;
      }

      decoderState = DecoderState::ReadingBitMark;
      break;
  }

  return false;
}

uint32_t readBufferOverflows() {
  uint32_t overflows;
  portENTER_CRITICAL(&edgeBufferMux);
  overflows = isrBufferOverflows;
  portEXIT_CRITICAL(&edgeBufferMux);
  return overflows;
}

void printTimingStats(const char* label, const TimingStats& stats) {
  if (stats.samples == 0) {
    return;
  }

  Serial.print("RF timings ");
  Serial.print(label);
  Serial.print(" us[min/avg/max]=");
  Serial.print(stats.minUs);
  Serial.print('/');
  Serial.print(stats.totalUs / stats.samples);
  Serial.print('/');
  Serial.println(stats.maxUs);
}

}  // namespace

void setupRfReceiver(uint8_t rxPin) {
  receiverPin = rxPin;
  pinMode(receiverPin, INPUT);
  lastEdgeUs = micros();
  attachInterrupt(digitalPinToInterrupt(receiverPin), onRfEdge, CHANGE);
}

bool receiveRfFrame(RfFrame& frame) {
  EdgePulse pulse;
  while (popEdgePulse(pulse)) {
    if (processPulse(pulse, frame)) {
      return true;
    }
  }

  return false;
}

void printRfDiagnostics() {
  const unsigned long now = millis();
  if (now - lastStatsMs < STATS_PERIOD_MS) {
    return;
  }
  lastStatsMs = now;

  Serial.print("RF stats: frames=");
  Serial.print(decoderStats.decodedFrames);
  Serial.print(" crc_failures=");
  Serial.print(decoderStats.crcFailures);
  Serial.print(" sync_failures=");
  Serial.print(decoderStats.syncFailures);
  Serial.print(" invalid_pulses=");
  Serial.print(decoderStats.invalidPulses);
  Serial.print(" incomplete_frames=");
  Serial.print(decoderStats.incompleteFrames);
  Serial.print(" buffer_overflows=");
  Serial.println(readBufferOverflows());

  printTimingStats("mark", validMarkTimings);
  printTimingStats("zero_space", validZeroSpaceTimings);
  printTimingStats("one_space", validOneSpaceTimings);
  printTimingStats("sync_space", validSyncSpaceTimings);
}

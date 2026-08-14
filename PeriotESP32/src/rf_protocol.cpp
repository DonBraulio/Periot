#include "rf_protocol.h"

namespace {

constexpr uint8_t MIN_PREAMBLE_CYCLES = 12;

constexpr uint16_t MIN_PULSE_US = 150;
constexpr uint16_t MAX_PULSE_US = 5000;
constexpr uint16_t SHORT_MIN_US = 250;
constexpr uint16_t SHORT_MAX_US = 700;
constexpr uint16_t LONG_MIN_US = 900;
constexpr uint16_t LONG_MAX_US = 1600;
constexpr uint16_t SYNC_LOW_MIN_US = 2200;
constexpr uint16_t SYNC_LOW_MAX_US = 4200;

constexpr uint16_t EDGE_BUFFER_SIZE = 128;
constexpr uint32_t STATS_PERIOD_MS = 5000;

struct EdgePulse {
  uint16_t durationUs;
  bool wasHigh;
};

enum class DecoderState : uint8_t {
  WaitingPreamble,
  WaitingSyncLow,
  ReadingBitHigh,
  ReadingBitLow,
};

struct DecoderStats {
  uint32_t decodedFrames = 0;
  uint32_t checksumFailures = 0;
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
TimingStats validHighTimings;
TimingStats validShortLowTimings;
TimingStats validLongLowTimings;
TimingStats validSyncLowTimings;

uint8_t preambleCycles = 0;
bool preambleHighSeen = false;
uint16_t payload = 0;
uint8_t bitsRead = 0;
uint16_t candidateTimings[RF_PAYLOAD_BITS * 2] = {};
uint8_t candidateTimingCount = 0;
uint16_t candidateSyncLowUs = 0;
unsigned long lastStatsMs = 0;

bool inRange(uint16_t value, uint16_t minimum, uint16_t maximum) {
  return value >= minimum && value <= maximum;
}

bool isShort(uint16_t durationUs) {
  return inRange(durationUs, SHORT_MIN_US, SHORT_MAX_US);
}

bool isLong(uint16_t durationUs) {
  return inRange(durationUs, LONG_MIN_US, LONG_MAX_US);
}

bool isSyncLow(uint16_t durationUs) {
  return inRange(durationUs, SYNC_LOW_MIN_US, SYNC_LOW_MAX_US);
}

void IRAM_ATTR onRfEdge() {
  const uint32_t nowUs = micros();
  const uint32_t durationUs = nowUs - lastEdgeUs;
  lastEdgeUs = nowUs;

  const bool completedPulseWasHigh = digitalRead(receiverPin) == LOW;

  portENTER_CRITICAL_ISR(&edgeBufferMux);
  const uint16_t nextWriteIndex = (edgeWriteIndex + 1) % EDGE_BUFFER_SIZE;
  if (nextWriteIndex == edgeReadIndex) {
    ++isrBufferOverflows;
  } else {
    edgeBuffer[edgeWriteIndex] = {
        static_cast<uint16_t>(durationUs > UINT16_MAX ? UINT16_MAX
                                                      : durationUs),
        completedPulseWasHigh,
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
  preambleHighSeen = false;
  payload = 0;
  bitsRead = 0;
  candidateTimingCount = 0;
  candidateSyncLowUs = 0;
}

void restartPreambleWith(const EdgePulse& pulse) {
  resetDecoder();
  if (pulse.wasHigh && isShort(pulse.durationUs)) {
    preambleHighSeen = true;
  }
}

uint8_t calculateChecksum(uint8_t nodeId, uint8_t directionBit,
                          uint8_t sequence) {
  return (nodeId ^ (directionBit << 1) ^ (sequence << 2) ^ 0b101) & 0x07;
}

void addTimingSample(TimingStats& stats, uint16_t durationUs) {
  stats.minUs = min(stats.minUs, durationUs);
  stats.maxUs = max(stats.maxUs, durationUs);
  stats.totalUs += durationUs;
  ++stats.samples;
}

void recordValidFrameTimings() {
  addTimingSample(validSyncLowTimings, candidateSyncLowUs);

  for (uint8_t i = 0; i < candidateTimingCount; ++i) {
    const uint16_t durationUs = candidateTimings[i];
    if ((i & 0x01) == 0) {
      addTimingSample(validHighTimings, durationUs);
    } else if (isLong(durationUs)) {
      addTimingSample(validLongLowTimings, durationUs);
    } else {
      addTimingSample(validShortLowTimings, durationUs);
    }
  }
}

bool parsePayload(RfFrame& frame) {
  const uint8_t nodeId = (payload >> 6) & 0x0F;
  const uint8_t directionBit = (payload >> 5) & 0x01;
  const uint8_t sequence = (payload >> 3) & 0x03;
  const uint8_t receivedChecksum = payload & 0x07;
  const uint8_t expectedChecksum =
      calculateChecksum(nodeId, directionBit, sequence);

  if (receivedChecksum != expectedChecksum) {
    ++decoderStats.checksumFailures;
    return false;
  }

  const int8_t direction = directionBit ? int8_t{1} : int8_t{-1};
  frame = {nodeId, direction, sequence, payload};
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
      if (pulse.wasHigh) {
        if (isShort(pulse.durationUs)) {
          preambleHighSeen = true;
          if (preambleCycles >= MIN_PREAMBLE_CYCLES) {
            decoderState = DecoderState::WaitingSyncLow;
          }
        } else {
          restartPreambleWith(pulse);
        }
      } else if (preambleHighSeen && isShort(pulse.durationUs)) {
        if (preambleCycles < UINT8_MAX) {
          ++preambleCycles;
        }
        preambleHighSeen = false;
      } else {
        resetDecoder();
      }
      break;

    case DecoderState::WaitingSyncLow:
      Serial.print("sync low wait...\n");
      if (pulse.wasHigh || !preambleHighSeen) {
        ++decoderStats.syncFailures;
        restartPreambleWith(pulse);
      } else if (isSyncLow(pulse.durationUs)) {
        candidateSyncLowUs = pulse.durationUs;
        decoderState = DecoderState::ReadingBitHigh;
        preambleHighSeen = false;
        payload = 0;
        bitsRead = 0;
        candidateTimingCount = 0;
      } else if (isShort(pulse.durationUs)) {
        if (preambleCycles < UINT8_MAX) {
          ++preambleCycles;
        }
        preambleHighSeen = false;
        decoderState = DecoderState::WaitingPreamble;
      } else {
        ++decoderStats.syncFailures;
        resetDecoder();
      }
      break;

    case DecoderState::ReadingBitHigh:
      if (pulse.wasHigh && isShort(pulse.durationUs)) {
        candidateTimings[candidateTimingCount++] = pulse.durationUs;
        decoderState = DecoderState::ReadingBitLow;
      } else {
        ++decoderStats.invalidPulses;
        ++decoderStats.incompleteFrames;
        restartPreambleWith(pulse);
      }
      break;

    case DecoderState::ReadingBitLow:
      if (pulse.wasHigh ||
          (!isShort(pulse.durationUs) && !isLong(pulse.durationUs))) {
        ++decoderStats.invalidPulses;
        ++decoderStats.incompleteFrames;
        restartPreambleWith(pulse);
        break;
      }

      candidateTimings[candidateTimingCount++] = pulse.durationUs;
      payload = (payload << 1) | (isLong(pulse.durationUs) ? 1 : 0);
      ++bitsRead;

      if (bitsRead == RF_PAYLOAD_BITS) {
        const bool validFrame = parsePayload(frame);
        resetDecoder();
        return validFrame;
      }

      decoderState = DecoderState::ReadingBitHigh;
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
  Serial.print(" checksum_failures=");
  Serial.print(decoderStats.checksumFailures);
  Serial.print(" sync_failures=");
  Serial.print(decoderStats.syncFailures);
  Serial.print(" invalid_pulses=");
  Serial.print(decoderStats.invalidPulses);
  Serial.print(" incomplete_frames=");
  Serial.print(decoderStats.incompleteFrames);
  Serial.print(" buffer_overflows=");
  Serial.println(readBufferOverflows());

  printTimingStats("high", validHighTimings);
  printTimingStats("short_low", validShortLowTimings);
  printTimingStats("long_low", validLongLowTimings);
  printTimingStats("sync_low", validSyncLowTimings);
}

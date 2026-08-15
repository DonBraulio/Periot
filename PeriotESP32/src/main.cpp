#include <Arduino.h>

#include "rf_position_tracker.h"
#include "rf_protocol.h"

constexpr uint8_t RF_RX_PIN = 3;

void printRfFrame(const RfFrame& frame, const RfPositionUpdate& update) {
  Serial.print("RF frame: node=");
  Serial.print(frame.nodeId);
  Serial.print(" boot_id=");
  Serial.print(frame.bootId);
  Serial.print(" position=");
  Serial.print(frame.position);
  Serial.print(" delta=");
  if (update.delta > 0) {
    Serial.print('+');
  }
  Serial.print(update.delta);
  Serial.print(" new_boot=");
  Serial.print(update.newBoot ? "yes" : "no");
  Serial.print(" payload=0b");

  for (int8_t bit = RF_PAYLOAD_BITS - 1; bit >= 0; --bit) {
    Serial.print((frame.payload >> bit) & 0x01);
  }

  Serial.println();
}

void setup() {
  Serial.begin(115200);
  delay(500);

  setupRfPositionTracker();
  setupRfReceiver(RF_RX_PIN);

  Serial.println();
  Serial.print("433 MHz receiver ready on GPIO ");
  Serial.println(RF_RX_PIN);
}

void loop() {
  RfFrame frame;
  while (receiveRfFrame(frame)) {
    const RfPositionUpdate update = trackRfPosition(frame);
    printRfFrame(frame, update);
  }

  updateRfPositionPersistence();
  printRfDiagnostics();
}

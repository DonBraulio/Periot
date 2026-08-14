#include <Arduino.h>

#include "rf_protocol.h"

constexpr uint8_t RF_RX_PIN = 3;

void printRfFrame(const RfFrame& frame) {
  Serial.print("RF frame: node=");
  Serial.print(frame.nodeId);
  Serial.print(" direction=");
  Serial.print(frame.direction > 0 ? "+1" : "-1");
  Serial.print(" sequence=");
  Serial.print(frame.sequence);
  Serial.print(" payload=0b");

  for (int8_t bit = RF_PAYLOAD_BITS - 1; bit >= 0; --bit) {
    Serial.print((frame.payload >> bit) & 0x01);
  }

  Serial.println();
}

void setup() {
  Serial.begin(115200);
  delay(500);

  setupRfReceiver(RF_RX_PIN);

  Serial.println();
  Serial.print("433 MHz receiver ready on GPIO ");
  Serial.println(RF_RX_PIN);
}

void loop() {
  RfFrame frame;
  while (receiveRfFrame(frame)) {
    printRfFrame(frame);
  }

  printRfDiagnostics();
}

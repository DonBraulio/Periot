#include <Arduino.h>

#include "app_config.h"
#include "rf_position_tracker.h"
#include "rf_protocol.h"
#include "serial_console.h"
#include "wifi_connection.h"
#include "wiz_control.h"
#include "wiz_lamp_controller.h"
#include "wiz_pairing.h"

WizLightList wizLights;

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
  setupWizPairings();

  if (connectWifi(AppConfig::WIFI_SSID, AppConfig::WIFI_PASSWORD)) {
    wizLights = discoverWizLights();
  }

  setupSerialConsole(wizLights);
  setupRfReceiver(AppConfig::RF_RX_PIN);

  Serial.println();
  Serial.print("433 MHz receiver ready on GPIO ");
  Serial.println(AppConfig::RF_RX_PIN);
}

void loop() {
  RfFrame frame;
  while (receiveRfFrame(frame)) {
    const RfPositionUpdate update = trackRfPosition(frame);
    printRfFrame(frame, update);
    applyWizPositionDelta(frame.nodeId, update.delta, wizLights);
  }

  updateSerialConsole();
  updateRfPositionPersistence();
  printRfDiagnostics();
}

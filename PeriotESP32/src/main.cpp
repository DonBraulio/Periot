#include <Arduino.h>

#define TEST_PIN 2  // Cambiar por el GPIO que quieras usar

void setup() {
  pinMode(TEST_PIN, OUTPUT);

  delay(2000);
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("Serial OK");
}

void loop() {
  Serial.print("Uptime: ");
  Serial.print(millis());
  Serial.println(" ms");

  digitalWrite(TEST_PIN, HIGH);
  delay(500);

  digitalWrite(TEST_PIN, LOW);
  delay(100);
}
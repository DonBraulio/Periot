#include <Arduino.h>

#define LED_PIN 4

void setup() {
  pinMode(LED_PIN, OUTPUT);  // P1 suele tener LED onboard en Digispark
}

void loop() {
  digitalWrite(LED_PIN, HIGH);
  delay(1000);
  digitalWrite(LED_PIN, LOW);
  delay(500);
}
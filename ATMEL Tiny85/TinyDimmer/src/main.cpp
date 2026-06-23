#include <Arduino.h>

void setup() {
  pinMode(1, OUTPUT);  // P1 suele tener LED onboard en Digispark
}

void loop() {
  digitalWrite(1, HIGH);
  delay(500);
  digitalWrite(1, LOW);
  delay(500);
}
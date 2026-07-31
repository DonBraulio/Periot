#include <Arduino.h>

// Note: PB0, 1 and 2 -> MOSI/MISO/SCK
constexpr uint8_t ENC_A_PIN = 3;
constexpr uint8_t ENC_B_PIN = 4;
constexpr uint8_t LED_PIN = 0;

int8_t encoderAccum = 0;
uint8_t lastState = 0;

uint8_t readEncoderState() {
  uint8_t a = digitalRead(ENC_A_PIN);
  uint8_t b = digitalRead(ENC_B_PIN);

  // Two-bit encoder state: AB
  return (a << 1) | b;
}

void flashLed(uint8_t times) {
  for (uint8_t i = 0; i < times; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(60);
    digitalWrite(LED_PIN, LOW);
    delay(80);
  }
}

void setup() {
  pinMode(ENC_A_PIN, INPUT_PULLUP);
  pinMode(ENC_B_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);

  digitalWrite(LED_PIN, LOW);

  lastState = readEncoderState();
}

void loop() {
  uint8_t currentState = readEncoderState();

  if (currentState != lastState) {
    uint8_t transition = (lastState << 2) | currentState;

    // Quadrature transition table.
    // Valid transitions increment or decrement depending on rotation direction.
    switch (transition) {
      case 0b1110:
      case 0b1000:
      case 0b0001:
      case 0b0111:
        encoderAccum++;
        break;

      case 0b1101:
      case 0b0100:
      case 0b0010:
      case 0b1011:
        encoderAccum--;
        break;

      default:
        // Invalid transition, likely caused by contact bounce.
        break;
    }

    lastState = currentState;

    if (encoderAccum >= 4) {
      encoderAccum = 0;

      // Later this will send a relative +1 RF message.
      flashLed(1);
    }

    if (encoderAccum <= -4) {
      encoderAccum = 0;

      // Later this will send a relative -1 RF message.
      flashLed(2);
    }
  }
}
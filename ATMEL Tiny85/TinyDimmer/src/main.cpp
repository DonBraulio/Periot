#include <Arduino.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>

#include "rf_protocol.h"

// Note: PB0, 1 and 2 -> MOSI/MISO/SCK
constexpr uint8_t ENC_A_PIN = 3;
constexpr uint8_t ENC_B_PIN = 4;
constexpr uint8_t LED_PIN = 0;
constexpr uint8_t RF_TX_PIN = 1;
constexpr uint8_t NODE_ID = 1;
constexpr uint8_t STEP_QUEUE_SIZE = 16;
constexpr uint8_t STEP_QUEUE_MASK = STEP_QUEUE_SIZE - 1;

volatile int8_t encoderAccum = 0;
volatile uint8_t lastState = 0;
volatile int8_t stepQueue[STEP_QUEUE_SIZE] = {};
volatile uint8_t stepQueueHead = 0;
volatile uint8_t stepQueueTail = 0;

uint8_t sequence = 0;

uint8_t readEncoderState() {
  const uint8_t pins = PINB;
  const uint8_t a = (pins & _BV(PB3)) != 0;
  const uint8_t b = (pins & _BV(PB4)) != 0;
  return (a << 1) | b;
}

void enqueueEncoderStep(int8_t direction) {
  const uint8_t nextHead = (stepQueueHead + 1) & STEP_QUEUE_MASK;
  if (nextHead == stepQueueTail) {
    return;
  }

  stepQueue[stepQueueHead] = direction;
  stepQueueHead = nextHead;
}

ISR(PCINT0_vect) {
  const uint8_t currentState = readEncoderState();
  if (currentState == lastState) {
    return;
  }

  const uint8_t transition = (lastState << 2) | currentState;
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
      break;
  }

  lastState = currentState;

  if (encoderAccum >= 4) {
    encoderAccum = 0;
    enqueueEncoderStep(+1);
  } else if (encoderAccum <= -4) {
    encoderAccum = 0;
    enqueueEncoderStep(-1);
  }
}

bool dequeueEncoderStep(int8_t& direction) {
  const uint8_t savedStatus = SREG;
  cli();

  if (stepQueueTail == stepQueueHead) {
    SREG = savedStatus;
    return false;
  }

  direction = stepQueue[stepQueueTail];
  stepQueueTail = (stepQueueTail + 1) & STEP_QUEUE_MASK;
  SREG = savedStatus;
  return true;
}

void sleepUntilEncoderInterrupt() {
  cli();

  if (stepQueueTail != stepQueueHead) {
    sei();
    return;
  }

  sleep_enable();
#if defined(BODS) && defined(BODSE)
  sleep_bod_disable();
#endif
  sei();
  sleep_cpu();
  sleep_disable();
}

void flashLed(uint8_t times) {
  for (uint8_t i = 0; i < times; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(60);
    digitalWrite(LED_PIN, LOW);
    delay(80);
  }
}

void handleEncoderStep(int8_t direction) {
  RfFrame frame = createRfFrame(NODE_ID, direction, sequence);
  sendRfFrame(RF_TX_PIN, frame);
  sequence = (sequence + 1) & 0x03;

  flashLed(direction > 0 ? 1 : 2);
}

void setup() {
  pinMode(ENC_A_PIN, INPUT_PULLUP);
  pinMode(ENC_B_PIN, INPUT_PULLUP);

  pinMode(LED_PIN, OUTPUT);
  pinMode(RF_TX_PIN, OUTPUT);

  digitalWrite(LED_PIN, LOW);
  digitalWrite(RF_TX_PIN, LOW);

  lastState = readEncoderState();

  // Wake on changes from either encoder phase: PB3/PCINT3 or PB4/PCINT4.
  PCMSK |= _BV(PCINT3) | _BV(PCINT4);
  GIFR |= _BV(PCIF);
  GIMSK |= _BV(PCIE);

  // The ADC and comparator are unused and only consume power while awake.
  ADCSRA &= ~_BV(ADEN);
  ACSR |= _BV(ACD);
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
}

void loop() {
  int8_t direction;
  if (dequeueEncoderStep(direction)) {
    handleEncoderStep(direction);
  }

  sleepUntilEncoderInterrupt();
}

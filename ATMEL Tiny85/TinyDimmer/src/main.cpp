#include <Arduino.h>
#include <EEPROM.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <avr/wdt.h>

#include "rf_protocol.h"

// Note: PB0, 1 and 2 -> MOSI/MISO/SCK
constexpr uint8_t ENC_A_PIN = 3;
constexpr uint8_t ENC_B_PIN = 4;
constexpr uint8_t LED_PIN = 0;
constexpr uint8_t RF_TX_PIN = 1;
constexpr uint8_t NODE_ID = 1;
constexpr uint8_t BOOT_ID_EEPROM_ADDRESS = 0;
constexpr uint8_t STEP_QUEUE_SIZE = 16;
constexpr uint8_t STEP_QUEUE_MASK = STEP_QUEUE_SIZE - 1;

volatile int8_t encoderAccum = 0;
volatile uint8_t lastEncoderState = 0;
volatile int8_t stepQueue[STEP_QUEUE_SIZE] = {};
volatile uint8_t stepQueueHead = 0;
volatile uint8_t stepQueueTail = 0;
volatile bool cooldownExpired = true;

uint8_t bootId = 0;
uint8_t encoderPosition = 0;
uint8_t lastSentPosition = 0;

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
  if (currentState == lastEncoderState) {
    return;
  }

  const uint8_t transition = (lastEncoderState << 2) | currentState;
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

  lastEncoderState = currentState;

  if (encoderAccum >= 4) {
    encoderAccum = 0;
    enqueueEncoderStep(+1);
  } else if (encoderAccum <= -4) {
    encoderAccum = 0;
    enqueueEncoderStep(-1);
  }
}

ISR(WDT_vect) {
  wdt_disable();
  cooldownExpired = true;
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

void processQueuedSteps() {
  int8_t direction;
  while (dequeueEncoderStep(direction)) {
    encoderPosition += direction;
  }
}

uint8_t nextBootId() {
  const uint8_t previousBootId = EEPROM.read(BOOT_ID_EEPROM_ADDRESS);
  const uint8_t newBootId =
      previousBootId <= 3 ? (previousBootId + 1) & 0x03 : 0;
  EEPROM.update(BOOT_ID_EEPROM_ADDRESS, newBootId);
  return newBootId;
}

void armCooldownWatchdog() {
  const uint8_t savedStatus = SREG;
  cli();

  cooldownExpired = false;
  wdt_reset();
  MCUSR &= ~_BV(WDRF);
  WDTCR = _BV(WDCE) | _BV(WDE);
  WDTCR = _BV(WDIE) | _BV(WDP2);  // Approximately 250 ms.

  SREG = savedStatus;
}

bool frameIsReady() {
  const uint8_t savedStatus = SREG;
  cli();
  const bool ready = cooldownExpired && encoderPosition != lastSentPosition;
  SREG = savedStatus;
  return ready;
}

void sleepUntilInterrupt() {
  cli();

  const bool queueHasSteps = stepQueueTail != stepQueueHead;
  const bool frameReady =
      cooldownExpired && encoderPosition != lastSentPosition;
  if (queueHasSteps || frameReady) {
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

void flashLed(int16_t delta) {
  const uint8_t times = delta > 0 ? 1 : 2;
  for (uint8_t i = 0; i < times; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(60);
    digitalWrite(LED_PIN, LOW);
    delay(80);
  }
}

void sendCurrentPosition() {
  const uint8_t positionToSend = encoderPosition;
  const int16_t delta =
      RfProtocolSpec::positionDelta(positionToSend, lastSentPosition);
  const RfFrame frame = createRfFrame(NODE_ID, bootId, positionToSend);

  sendRfFrame(RF_TX_PIN, frame);
  lastSentPosition = positionToSend;
  armCooldownWatchdog();
  flashLed(delta);
}

void setup() {
  pinMode(ENC_A_PIN, INPUT_PULLUP);
  pinMode(ENC_B_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  pinMode(RF_TX_PIN, OUTPUT);

  digitalWrite(LED_PIN, LOW);
  digitalWrite(RF_TX_PIN, LOW);

  bootId = nextBootId();
  lastEncoderState = readEncoderState();

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
  processQueuedSteps();

  if (frameIsReady()) {
    sendCurrentPosition();
  }

  sleepUntilInterrupt();
}

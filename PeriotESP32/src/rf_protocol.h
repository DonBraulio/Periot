#pragma once

#include <Arduino.h>
#include <rf_protocol_spec.h>

constexpr uint8_t RF_PAYLOAD_BITS = RfProtocolSpec::PAYLOAD_BITS;

using RfFrame = RfProtocolSpec::Frame;

void setupRfReceiver(uint8_t rxPin);
bool receiveRfFrame(RfFrame& frame);
void printRfDiagnostics();

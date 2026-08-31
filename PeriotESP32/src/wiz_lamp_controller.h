#pragma once

#include <Arduino.h>

#include "wiz_control.h"

void applyWizPositionDelta(uint8_t nodeId, int16_t delta,
                           WizLightList& lights);

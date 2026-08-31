#include "wiz_lamp_controller.h"

#include "app_config.h"
#include "wiz_pairing.h"

void applyWizPositionDelta(uint8_t nodeId, int16_t delta,
                           WizLightList& lights) {
  if (delta == 0) {
    return;
  }

  const uint8_t pairingCount = getWizPairingCount(nodeId);
  if (pairingCount == 0) {
    Serial.print("Node ");
    Serial.print(nodeId);
    Serial.println(" is not paired. Type 'lights' and 'help' over Serial.");
    return;
  }

  for (uint8_t index = 0; index < pairingCount; ++index) {
    const String mac = getWizPairedMac(nodeId, index);
    WizLight* light = findWizLightByMac(lights, mac);
    if (light == nullptr) {
      Serial.print("Cannot control paired lamp ");
      Serial.print(mac);
      Serial.println(": it was not discovered during boot");
      continue;
    }

    const int currentDimming =
        light->dimming >= 10 ? light->dimming : AppConfig::DEFAULT_DIMMING;
    const int targetDimming = constrain(
        currentDimming + delta * AppConfig::DIMMING_PER_DETENT, 10, 100);

    // Send even when already at the limit so turning the encoder also turns an
    // off lamp back on without changing its color mode.
    if (setWizDimming(*light, targetDimming)) {
      Serial.print("WiZ update: node=");
      Serial.print(nodeId);
      Serial.print(" MAC=");
      Serial.print(light->mac);
      Serial.print(" dimming=");
      Serial.println(targetDimming);
    } else {
      Serial.print("WiZ UDP send failed for ");
      Serial.println(light->mac);
    }
  }
}

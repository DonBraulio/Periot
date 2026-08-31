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
        light->state && light->dimming >= 10 ? light->dimming : 0;
    int targetDimming = constrain(
        currentDimming + delta * AppConfig::DIMMING_PER_DETENT, 0, 100);

    // WiZ supports dimming from 10 to 100. Map the otherwise unreachable gap
    // so one negative detent from 10 turns off and one positive detent from 0
    // turns on at 10.
    if (targetDimming > 0 && targetDimming < 10) {
      targetDimming = delta > 0 ? 10 : 0;
    }

    // Send even when already at a limit to keep the command behavior explicit.
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

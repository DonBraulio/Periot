#include "serial_console.h"

#include <Arduino.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>

#include "rf_protocol.h"
#include "wiz_pairing.h"

namespace {

constexpr size_t COMMAND_BUFFER_SIZE = 96;

WizLightList* discoveredLights = nullptr;
char commandBuffer[COMMAND_BUFFER_SIZE] = {};
size_t commandLength = 0;
bool serialWasConnected = false;
bool ignoreNextLineFeed = false;

bool parseNumber(const char* token, long minimum, long maximum, long& value) {
  if (token == nullptr || *token == '\0') {
    return false;
  }

  errno = 0;
  char* end = nullptr;
  const long parsed = strtol(token, &end, 10);
  if (errno != 0 || end == token || *end != '\0' || parsed < minimum ||
      parsed > maximum) {
    return false;
  }

  value = parsed;
  return true;
}

int findDiscoveredIndex(const String& mac) {
  if (discoveredLights == nullptr) {
    return -1;
  }

  for (size_t index = 0; index < discoveredLights->size(); ++index) {
    if ((*discoveredLights)[index].mac == normalizeWizMac(mac)) {
      return static_cast<int>(index);
    }
  }
  return -1;
}

bool parseNodeAndLight(char* nodeToken, char* lightToken, uint8_t& nodeId,
                       size_t& lightIndex) {
  long parsedNode = 0;
  long parsedLight = 0;
  const long maximumLight = discoveredLights == nullptr ||
                                    discoveredLights->empty()
                                ? -1
                                : static_cast<long>(discoveredLights->size() -
                                                    1);

  if (!parseNumber(nodeToken, 0, WIZ_NODE_COUNT - 1, parsedNode) ||
      !parseNumber(lightToken, 0, maximumLight, parsedLight)) {
    return false;
  }

  nodeId = static_cast<uint8_t>(parsedNode);
  lightIndex = static_cast<size_t>(parsedLight);
  return true;
}

void handlePairCommand(char* nodeToken, char* lightToken) {
  uint8_t nodeId = 0;
  size_t lightIndex = 0;
  if (!parseNodeAndLight(nodeToken, lightToken, nodeId, lightIndex)) {
    Serial.println("Usage: pair <node_id 0..15> <light_index>");
    return;
  }

  const WizLight& light = (*discoveredLights)[lightIndex];
  if (isWizPaired(nodeId, light.mac)) {
    Serial.println("That lamp is already paired with this node");
    return;
  }
  if (getWizPairingCount(nodeId) >= MAX_WIZ_LIGHTS_PER_NODE) {
    Serial.println("That node already has the maximum of four lamps");
    return;
  }
  if (!addWizPairing(nodeId, light.mac)) {
    Serial.println("Pairing failed while writing NVS");
    return;
  }

  Serial.print("Paired node ");
  Serial.print(nodeId);
  Serial.print(" with lamp [");
  Serial.print(lightIndex);
  Serial.print("] ");
  Serial.println(light.mac);
}

void handleUnpairCommand(char* nodeToken, char* lightToken) {
  uint8_t nodeId = 0;
  size_t lightIndex = 0;
  if (!parseNodeAndLight(nodeToken, lightToken, nodeId, lightIndex)) {
    Serial.println("Usage: unpair <node_id 0..15> <light_index>");
    return;
  }

  const WizLight& light = (*discoveredLights)[lightIndex];
  if (!isWizPaired(nodeId, light.mac)) {
    Serial.println("That lamp is not paired with this node");
    return;
  }
  if (!removeWizPairing(nodeId, light.mac)) {
    Serial.println("Unpairing failed while writing NVS");
    return;
  }

  Serial.print("Unpaired node ");
  Serial.print(nodeId);
  Serial.print(" from lamp ");
  Serial.println(light.mac);
}

void handleClearCommand(char* nodeToken) {
  long parsedNode = 0;
  if (!parseNumber(nodeToken, 0, WIZ_NODE_COUNT - 1, parsedNode)) {
    Serial.println("Usage: clear <node_id 0..15>");
    return;
  }

  const uint8_t nodeId = static_cast<uint8_t>(parsedNode);
  if (!clearWizPairings(nodeId)) {
    Serial.println("Could not clear pairings from NVS");
    return;
  }

  Serial.print("Cleared all pairings for node ");
  Serial.println(nodeId);
}

void handleCommand() {
  char* command = strtok(commandBuffer, " \t");
  if (command == nullptr) {
    printSerialConsoleHelp();
    return;
  }

  char* firstArgument = strtok(nullptr, " \t");
  char* secondArgument = strtok(nullptr, " \t");
  char* extraArgument = strtok(nullptr, " \t");

  if (strcmp(command, "help") == 0 && firstArgument == nullptr) {
    printSerialConsoleHelp();
  } else if (strcmp(command, "lights") == 0 && firstArgument == nullptr) {
    printDiscoveredWizLights();
  } else if (strcmp(command, "pairs") == 0 && firstArgument == nullptr) {
    printWizPairings();
  } else if (strcmp(command, "stats") == 0 && firstArgument == nullptr) {
    printRfDiagnostics();
  } else if (strcmp(command, "pair") == 0 && extraArgument == nullptr) {
    handlePairCommand(firstArgument, secondArgument);
  } else if (strcmp(command, "unpair") == 0 && extraArgument == nullptr) {
    handleUnpairCommand(firstArgument, secondArgument);
  } else if (strcmp(command, "clear") == 0 && secondArgument == nullptr) {
    handleClearCommand(firstArgument);
  } else {
    Serial.println("Unknown command or invalid arguments. Type 'help'.");
  }
}

}  // namespace

void setupSerialConsole(WizLightList& lights) {
  discoveredLights = &lights;
  serialWasConnected = false;
}

void updateSerialConsole() {
  const bool serialConnected = static_cast<bool>(Serial);
  if (serialConnected && !serialWasConnected) {
    Serial.println();
    printSerialConsoleHelp();
  }
  serialWasConnected = serialConnected;

  while (Serial.available() > 0) {
    const char character = static_cast<char>(Serial.read());

    if (character == '\n' && ignoreNextLineFeed) {
      ignoreNextLineFeed = false;
      continue;
    }
    if (character == '\r' || character == '\n') {
      commandBuffer[commandLength] = '\0';
      handleCommand();
      commandLength = 0;
      ignoreNextLineFeed = character == '\r';
      continue;
    }
    ignoreNextLineFeed = false;
    if (character == '\b' || character == 0x7F) {
      if (commandLength > 0) {
        --commandLength;
      }
      continue;
    }
    if (commandLength + 1 >= COMMAND_BUFFER_SIZE) {
      commandLength = 0;
      Serial.println("Serial command was too long and has been discarded");
      continue;
    }

    commandBuffer[commandLength++] = character;
  }
}

void printSerialConsoleHelp() {
  Serial.println("Serial pairing commands:");
  Serial.println("  lights                         list boot-discovered lamps");
  Serial.println("  pairs                          list persistent mappings");
  Serial.println("  stats                          show cumulative RF statistics");
  Serial.println("  pair <node_id> <light_index>   add a lamp to a node");
  Serial.println("  unpair <node_id> <light_index> remove a lamp from a node");
  Serial.println("  clear <node_id>                remove every lamp from a node");
  Serial.println("  help                           show this help");
  Serial.println("Reboot the ESP32 to run WiZ discovery again.");
}

void printDiscoveredWizLights() {
  Serial.println("Discovered WiZ lamps:");
  if (discoveredLights == nullptr || discoveredLights->empty()) {
    Serial.println("  (none)");
    return;
  }

  for (size_t index = 0; index < discoveredLights->size(); ++index) {
    const WizLight& light = (*discoveredLights)[index];
    Serial.print("  [");
    Serial.print(index);
    Serial.print("] MAC=");
    Serial.print(light.mac);
    Serial.print(" IP=");
    Serial.print(light.ip);
    Serial.print(" state=");
    Serial.print(light.state ? "on" : "off");
    Serial.print(" dimming=");
    if (light.dimming >= 0) {
      Serial.print(light.dimming);
    } else {
      Serial.print("unknown");
    }
    Serial.print(" rssi=");
    Serial.println(light.rssi);
  }
}

void printWizPairings() {
  Serial.println("Persistent WiZ pairings:");
  bool foundAny = false;

  for (uint8_t nodeId = 0; nodeId < WIZ_NODE_COUNT; ++nodeId) {
    const uint8_t count = getWizPairingCount(nodeId);
    for (uint8_t pairingIndex = 0; pairingIndex < count; ++pairingIndex) {
      const String mac = getWizPairedMac(nodeId, pairingIndex);
      const int lightIndex = findDiscoveredIndex(mac);
      Serial.print("  node ");
      Serial.print(nodeId);
      Serial.print(" -> ");
      Serial.print(mac);
      if (lightIndex >= 0) {
        Serial.print(" [light ");
        Serial.print(lightIndex);
        Serial.print(']');
      } else {
        Serial.print(" [not discovered]");
      }
      Serial.println();
      foundAny = true;
    }
  }

  if (!foundAny) {
    Serial.println("  (none)");
  }
}

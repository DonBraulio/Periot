#pragma once

bool connectWifi(const char* ssid, const char* password,
                 unsigned long timeoutMs = 15000);

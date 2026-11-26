#pragma once

#include <WiFi.h>

// The reader's names on a network: its web page answers at http://ashadsbook.local/, and
// routers list it as AshadsBook-<MAC>.
constexpr const char* DEVICE_MDNS_NAME = "ashadsbook";
constexpr const char* DEVICE_HOSTNAME_PREFIX = "AshadsBook-";

inline bool hasActiveStationWifiConnection() {
  return (WiFi.getMode() & WIFI_MODE_STA) && WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0);
}

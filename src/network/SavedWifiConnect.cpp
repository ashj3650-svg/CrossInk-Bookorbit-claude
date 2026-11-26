#include "SavedWifiConnect.h"

#include <Arduino.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <Logging.h>
#include <WiFi.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "WifiCredentialStore.h"
#include "WifiUtils.h"

namespace {
// WifiSelectionActivity's own figures: its auto-connect timeout, and the heap below
// which bringing the Wi-Fi driver up panics inside the SDK.
constexpr unsigned long JOIN_TIMEOUT_MS = 7000;
constexpr uint32_t MIN_FREE_HEAP_TO_START_WIFI = 60U * 1024U;
constexpr uint32_t MIN_MAX_ALLOC_TO_START_WIFI = 24U * 1024U;
constexpr unsigned long SCAN_TIMEOUT_MS = 10000;
constexpr unsigned long POLL_MS = 50;

// Where the last network joined answered: its router (BSSID) and channel, so the next join
// goes straight there. One record in /.crosspoint/wifi_fast.bin, read once per boot and
// written only when it changes.
constexpr char FAST_JOIN_PATH[] = "/.crosspoint/wifi_fast.bin";
constexpr uint32_t FAST_JOIN_MAGIC = 0x31484657;  // "WFH1"
#pragma pack(push, 1)
struct FastJoin {
  uint32_t magic = 0;
  char ssid[33] = {};
  uint8_t channel = 0;
  uint8_t bssid[6] = {};
};
#pragma pack(pop)
FastJoin fastJoin;
bool fastJoinLoaded = false;

void loadFastJoin() {
  if (fastJoinLoaded) return;
  fastJoinLoaded = true;
  HalFile file;
  if (!Storage.openFileForRead("WIFI", FAST_JOIN_PATH, file)) return;
  FastJoin read;
  const bool ok = file.read(reinterpret_cast<uint8_t*>(&read), sizeof(read)) == sizeof(read);
  file.close();
  if (ok && read.magic == FAST_JOIN_MAGIC && read.channel >= 1 && read.channel <= 14) {
    read.ssid[sizeof(read.ssid) - 1] = '\0';
    fastJoin = read;
  }
}

void saveFastJoin() {
  HalFile file;
  if (!Storage.openFileForWrite("WIFI", FAST_JOIN_PATH, file)) {
    LOG_ERR("WIFI", "Cannot save the fast-join record");
    return;
  }
  if (file.write(reinterpret_cast<const uint8_t*>(&fastJoin), sizeof(fastJoin)) != sizeof(fastJoin)) {
    LOG_ERR("WIFI", "Cannot write the fast-join record");
  }
  file.close();
}

bool hasFastJoin(const std::string& ssid) {
  loadFastJoin();
  return fastJoin.magic == FAST_JOIN_MAGIC && ssid == fastJoin.ssid;
}

bool join(const WifiCredential& credential, const SavedWifiConnect::StopCheck shouldStop) {
  SavedWifiConnect::beginJoin(credential);

  const unsigned long start = millis();
  wl_status_t status = WiFi.status();
  while (millis() - start < JOIN_TIMEOUT_MS && !shouldStop()) {
    status = WiFi.status();
    if (status == WL_CONNECTED) {
      SavedWifiConnect::rememberJoined(credential.ssid);
      return true;
    }
    if (SavedWifiConnect::isJoinFailure(status)) break;
    delay(POLL_MS);
  }
  if (shouldStop()) {
    LOG_INF("WIFI", "Stopped joining %s after %lums", credential.ssid.c_str(), millis() - start);
  } else {
    LOG_INF("WIFI", "Could not join %s (status=%d, %lums)", credential.ssid.c_str(), static_cast<int>(status),
            millis() - start);
  }
  WiFi.disconnect();
  return false;
}

struct VisibleNetwork {
  std::string ssid;
  int32_t rssi;
};

// Saved networks the scan can see, strongest first, leaving out `skipSsid`.
bool scanSavedNetworks(const std::string& skipSsid, const SavedWifiConnect::StopCheck shouldStop,
                       std::vector<VisibleNetwork>& out) {
  WiFi.disconnect();
  delay(100);
  if (WiFi.scanNetworks(true) != WIFI_SCAN_RUNNING) {
    LOG_ERR("WIFI", "Background scan did not start");
    return false;
  }
  const unsigned long start = millis();
  int found = WiFi.scanComplete();
  while (found == WIFI_SCAN_RUNNING) {
    if (shouldStop() || millis() - start >= SCAN_TIMEOUT_MS) {
      WiFi.scanDelete();
      return false;
    }
    delay(POLL_MS);
    found = WiFi.scanComplete();
  }
  if (found < 0) {
    LOG_INF("WIFI", "Background scan failed (%d)", found);
    return false;
  }

  out.reserve(WIFI_STORE.getCredentialCount());
  for (int i = 0; i < found; i++) {
    char ssid[33];
    strlcpy(ssid, WiFi.SSID(i).c_str(), sizeof(ssid));
    if (ssid[0] == '\0' || skipSsid == ssid) continue;
    const int32_t rssi = WiFi.RSSI(i);
    const auto seen = std::find_if(out.begin(), out.end(), [&ssid](const VisibleNetwork& n) { return n.ssid == ssid; });
    if (seen != out.end()) {
      seen->rssi = std::max(seen->rssi, rssi);
    } else if (WIFI_STORE.hasSavedCredential(ssid)) {
      out.push_back({ssid, rssi});
    }
  }
  WiFi.scanDelete();
  std::sort(out.begin(), out.end(), [](const VisibleNetwork& a, const VisibleNetwork& b) { return a.rssi > b.rssi; });
  return true;
}
}  // namespace

bool SavedWifiConnect::connect(const StopCheck shouldStop) {
  if (WiFi.status() == WL_CONNECTED) return true;
  if (WIFI_STORE.getCredentialCount() == 0) {
    LOG_INF("WIFI", "No saved network to join in the background");
    return false;
  }
  if (!startStation()) return false;

  const std::string lastSsid = WIFI_STORE.getLastConnectedSsid();
  if (!lastSsid.empty()) {
    const auto credential = WIFI_STORE.findCredential(lastSsid);
    if (credential && join(*credential, shouldStop)) return true;
    // A direct join that failed (the router changed channel, or another one of the same
    // network is nearer) gets one try the usual way before the scan for the others.
    if (credential && !shouldStop() && dropFastJoin(lastSsid) && join(*credential, shouldStop)) return true;
  }
  if (shouldStop()) return false;

  std::vector<VisibleNetwork> visible;
  if (!scanSavedNetworks(lastSsid, shouldStop, visible)) return false;
  for (const VisibleNetwork& network : visible) {
    if (shouldStop()) return false;
    const auto credential = WIFI_STORE.findCredential(network.ssid);
    if (credential && join(*credential, shouldStop)) return true;
  }
  LOG_INF("WIFI", "No saved network joined in the background (%u visible)", static_cast<unsigned>(visible.size()));
  return false;
}

bool SavedWifiConnect::startStation() {
  if (WiFi.getMode() == WIFI_OFF &&
      (ESP.getFreeHeap() < MIN_FREE_HEAP_TO_START_WIFI || ESP.getMaxAllocHeap() < MIN_MAX_ALLOC_TO_START_WIFI)) {
    LOG_ERR("WIFI", "Refusing to start WiFi on low memory: free=%u maxAlloc=%u", ESP.getFreeHeap(),
            ESP.getMaxAllocHeap());
    return false;
  }

  // Full CPU speed before the radio starts. Home drops the CPU to its idle speed after 3
  // seconds without a press, which is just when Wi-Fi at Home starts the radio; the main
  // loop then put it back to full speed (setPowerSaving() refuses the idle speed while
  // Wi-Fi is on) in the middle of the join. Started at full speed, the radio keeps one
  // CPU clock for as long as it is on.
  powerManager.setPowerSaving(false);
  WiFi.persistent(false);  // WifiCredentialStore owns the credentials; keep the SDK's NVS copy out of it
  if (!WiFi.mode(WIFI_STA)) {
    LOG_ERR("WIFI", "Failed to set station mode for a background join");
    return false;
  }
  // The name routers show, as WifiSelectionActivity sets it.
  uint8_t mac[6] = {};
  WiFi.macAddress(mac);
  char hostname[32];
  snprintf(hostname, sizeof(hostname), "%s%02X%02X%02X%02X%02X%02X", DEVICE_HOSTNAME_PREFIX, mac[0], mac[1], mac[2],
           mac[3], mac[4], mac[5]);
  WiFi.setHostname(hostname);
  return true;
}

void SavedWifiConnect::beginJoin(const WifiCredential& credential) {
  LOG_INF("WIFI", "Joining saved network %s in the background", credential.ssid.c_str());
  if (!WiFi.disconnect(false, false, 1000)) {
    LOG_DBG("WIFI", "Disconnect before begin timed out; continuing with begin");
  }
  delay(100);
  const char* password = credential.password.empty() ? nullptr : credential.password.c_str();
  if (hasFastJoin(credential.ssid)) {
    LOG_INF("WIFI", "Joining %s directly on channel %u", credential.ssid.c_str(),
            static_cast<unsigned>(fastJoin.channel));
    WiFi.setScanMethod(WIFI_FAST_SCAN);
#ifdef SIMULATOR
    WiFi.begin(credential.ssid.c_str(), password);  // the simulator's stub has no channel/BSSID join
#else
    WiFi.begin(credential.ssid.c_str(), password, fastJoin.channel, fastJoin.bssid);
#endif
    return;
  }
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
  WiFi.begin(credential.ssid.c_str(), password);
}

bool SavedWifiConnect::dropFastJoin(const std::string& ssid) {
  if (!hasFastJoin(ssid)) return false;
  LOG_INF("WIFI", "Direct join to %s failed; scanning next time", ssid.c_str());
  fastJoin = FastJoin{};
  Storage.remove(FAST_JOIN_PATH);
  return true;
}

bool SavedWifiConnect::isJoinFailure(const int status) {
  if (status == WL_CONNECT_FAILED || status == WL_NO_SSID_AVAIL) return true;
#ifndef SIMULATOR
  return status == WL_CONNECTION_LOST;
#else
  return false;
#endif
}

void SavedWifiConnect::rememberJoined(const std::string& ssid) {
  LOG_INF("WIFI", "Joined %s (rssi=%d)", ssid.c_str(), WiFi.RSSI());
  WIFI_STORE.setLastConnectedSsid(ssid);
  const uint8_t* bssid = WiFi.BSSID();
  const int32_t channel = WiFi.channel();
  if (bssid == nullptr || channel < 1 || channel > 14 || ssid.size() >= sizeof(fastJoin.ssid)) return;
  loadFastJoin();
  if (hasFastJoin(ssid) && fastJoin.channel == channel && memcmp(fastJoin.bssid, bssid, sizeof(fastJoin.bssid)) == 0) {
    return;
  }
  fastJoin = FastJoin{};
  fastJoin.magic = FAST_JOIN_MAGIC;
  memcpy(fastJoin.ssid, ssid.c_str(), ssid.size());
  fastJoin.channel = static_cast<uint8_t>(channel);
  memcpy(fastJoin.bssid, bssid, sizeof(fastJoin.bssid));
  saveFastJoin();
}

void SavedWifiConnect::disconnect() {
  // No SNTP stop here: syncSystemTimeFromNTP() releases the client before it returns.
  WiFi.disconnect(false);
  delay(100);
  WiFi.mode(WIFI_OFF);
  delay(100);
}

#include "HomeWifi.h"

#include <WiFi.h>

bool HomeWifi::linkUp() { return WiFi.getMode() != WIFI_OFF && WiFi.status() == WL_CONNECTED; }

#if CROSSINK_APP_HOME_WIFI

#include <Arduino.h>
#include <HalClock.h>
#include <Logging.h>
#include <WallClock.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "CrossPointSettings.h"
#include "WifiCredentialStore.h"
#include "network/BookOrbitBackgroundDownloads.h"
#include "network/HomeWebServer.h"
#include "network/SavedWifiConnect.h"

namespace {
// Off: radio down (or never up). Joining/Scanning: Home is bringing it up, polled from
// tick() so the Home screen keeps answering buttons. Connected: up, and Home's to take
// down. Released: left up for the activity Home handed over to, which now owns it.
enum class State : uint8_t { Off, Joining, Scanning, Connected, Released };

constexpr unsigned long JOIN_TIMEOUT_MS = 10000;
constexpr unsigned long SCAN_TIMEOUT_MS = 10000;
constexpr unsigned long IDLE_OFF_MS = 10UL * 60UL * 1000UL;
// After arriving on Home: the screen draws first, and a quick pass through Home on the
// way to a book does not start the radio at all.
constexpr unsigned long START_DELAY_MS = 3000;
// Failed attempts back off: 1, 5, then 15 minutes, so a reader out of range of every
// saved network does not keep the radio scanning.
constexpr unsigned long RETRY_DELAYS_MS[] = {60UL * 1000UL, 5UL * 60UL * 1000UL, 15UL * 60UL * 1000UL};
constexpr size_t RETRY_DELAY_COUNT = sizeof(RETRY_DELAYS_MS) / sizeof(RETRY_DELAYS_MS[0]);
// Saved networks tried after a scan, strongest first.
constexpr size_t MAX_SCANNED_CANDIDATES = 3;

State state = State::Off;
unsigned long stateSince = 0;
unsigned long nextAttemptAt = 0;
bool attemptScheduled = false;
uint8_t failures = 0;
bool scanned = false;
bool stopForBook = false;
bool idleOff = false;
bool wasOnHome = false;
// Networks to try in order, filled per attempt and freed once it settles.
std::vector<std::string> candidates;
size_t nextCandidate = 0;

void setState(const State next) {
  state = next;
  stateSince = millis();
}

bool owningRadio() { return state == State::Joining || state == State::Scanning || state == State::Connected; }

void releaseCandidates() {
  candidates.clear();
  candidates.shrink_to_fit();
  nextCandidate = 0;
}

void radioDown(const char* reason) {
  LOG_INF("HWIFI", "Wi-Fi off: %s", reason);
  HomeWebServer::stop();
  if (state == State::Scanning) WiFi.scanDelete();
  SavedWifiConnect::disconnect();
  releaseCandidates();
  setState(State::Off);
}

// Without a clock chip the time is only known once NTP has set it, and reading progress
// is dated with it (ProgressSaveTime). Asked once a connection is up while the clock is
// still unset; that wait (a few seconds at most) happens on Home, not in a book.
void syncClockIfUnset() {
#ifndef SIMULATOR
  uint32_t before = 0;
  if (halClock.isAvailable() || WallClock::now(before)) return;
  if (halClock.syncSystemTimeFromNTP()) WallClock::markNtpSynced(before);
#endif
}

void scheduleAttempt(const unsigned long delayMs) {
  nextAttemptAt = millis() + delayMs;
  attemptScheduled = true;
}

void scheduleRetry() {
  const size_t index = std::min<size_t>(failures, RETRY_DELAY_COUNT - 1);
  if (failures < UINT8_MAX) failures++;
  LOG_INF("HWIFI", "Trying again in %lu s", RETRY_DELAYS_MS[index] / 1000UL);
  scheduleAttempt(RETRY_DELAYS_MS[index]);
}

bool joinNextCandidate() {
  while (nextCandidate < candidates.size()) {
    const auto credential = WIFI_STORE.findCredential(candidates[nextCandidate++]);
    if (!credential) continue;
    SavedWifiConnect::beginJoin(*credential);
    setState(State::Joining);
    return true;
  }
  return false;
}

bool startScan() {
  scanned = true;
  WiFi.disconnect();
  if (WiFi.scanNetworks(true) != WIFI_SCAN_RUNNING) {
    LOG_ERR("HWIFI", "Scan did not start");
    return false;
  }
  setState(State::Scanning);
  return true;
}

// Every queued network failed: scan for the other saved ones once, then give up for now.
void onCandidatesExhausted() {
  if (!scanned && startScan()) return;
  radioDown("no saved network answered");
  scheduleRetry();
}

void startAttempt() {
  if (WIFI_STORE.getCredentialCount() == 0) {
    // Nothing to join until a network is saved, which happens in another activity:
    // arriving back on Home schedules the next look.
    LOG_DBG("HWIFI", "No saved network to join");
    return;
  }
  LOG_INF("HWIFI", "Joining a saved network from Home (free=%u maxAlloc=%u)", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  if (!SavedWifiConnect::startStation()) {
    scheduleRetry();
    return;
  }
  releaseCandidates();
  scanned = false;
  const std::string lastSsid = WIFI_STORE.getLastConnectedSsid();
  if (!lastSsid.empty()) candidates.push_back(lastSsid);
  if (!joinNextCandidate()) onCandidatesExhausted();
}

void collectScanResults(const int found) {
  // Networks tried already are not queued again, except the last one joined: a first join
  // that fails on its own (a router slow to answer the handshake) gets one more try when
  // the scan sees it, after any other saved network in range.
  std::vector<std::pair<int32_t, std::string>> visible;
  visible.reserve(MAX_SCANNED_CANDIDATES + 1);
  bool lastNetworkSeen = false;
  for (int i = 0; i < found; i++) {
    std::string ssid = WiFi.SSID(i).c_str();
    if (ssid.empty()) continue;
    if (std::find(candidates.begin(), candidates.end(), ssid) != candidates.end()) {
      if (!candidates.empty() && ssid == candidates.front()) lastNetworkSeen = true;
      continue;
    }
    const int32_t rssi = WiFi.RSSI(i);
    const auto seen = std::find_if(visible.begin(), visible.end(),
                                   [&ssid](const std::pair<int32_t, std::string>& n) { return n.second == ssid; });
    if (seen != visible.end()) {
      seen->first = std::max(seen->first, rssi);
      continue;
    }
    if (!WIFI_STORE.hasSavedCredential(ssid)) continue;
    visible.emplace_back(rssi, std::move(ssid));
    // Keep only the strongest few: a crowded scan should not grow this list.
    std::sort(visible.begin(), visible.end(),
              [](const std::pair<int32_t, std::string>& a, const std::pair<int32_t, std::string>& b) {
                return a.first > b.first;
              });
    if (visible.size() > MAX_SCANNED_CANDIDATES) visible.pop_back();
  }
  WiFi.scanDelete();
  const std::string lastNetwork = lastNetworkSeen ? candidates.front() : std::string();
  for (auto& network : visible) candidates.push_back(std::move(network.second));
  if (lastNetworkSeen) candidates.push_back(lastNetwork);
  LOG_INF("HWIFI", "Scan found %d networks; %u saved ones to try", found,
          static_cast<unsigned>(candidates.size() - nextCandidate));
}

// Arriving on Home: adopt a connection left up by the activity Home handed over to, or
// schedule a join.
void onArrivedHome() {
  // A book opened while a download from Home ran, and Home is back before it ended.
  stopForBook = false;
  if (state == State::Connected) HomeWebServer::start();
  if (state == State::Released) {
    if (WiFi.status() == WL_CONNECTED) {
      LOG_INF("HWIFI", "Keeping the connection that is already up");
      setState(State::Connected);
      HomeWebServer::start();
      return;
    }
    setState(State::Off);
  }
  if (state == State::Off) {
    failures = 0;
    scheduleAttempt(START_DELAY_MS);
  }
}

void tickOnHome(const unsigned long idleMs) {
  const unsigned long now = millis();
  switch (state) {
    case State::Off:
      if (idleMs >= IDLE_OFF_MS) {
        idleOff = true;
        break;
      }
      if (idleOff) {
        // A button press after the idle switch-off: connect again straight away.
        idleOff = false;
        failures = 0;
        scheduleAttempt(0);
      }
      if (!attemptScheduled || static_cast<long>(now - nextAttemptAt) < 0) break;
      attemptScheduled = false;
      startAttempt();
      break;

    case State::Joining: {
      const int status = WiFi.status();
      if (status == WL_CONNECTED) {
        SavedWifiConnect::rememberJoined(candidates[nextCandidate - 1]);
        releaseCandidates();
        failures = 0;
        setState(State::Connected);
        syncClockIfUnset();
        HomeWebServer::start();
        break;
      }
      if (SavedWifiConnect::isJoinFailure(status) || now - stateSince >= JOIN_TIMEOUT_MS) {
        LOG_INF("HWIFI", "Could not join %s (status=%d)", candidates[nextCandidate - 1].c_str(), status);
        // A direct join on the remembered router and channel gets one more try with a scan.
        if (SavedWifiConnect::dropFastJoin(candidates[nextCandidate - 1])) nextCandidate--;
        if (!joinNextCandidate()) onCandidatesExhausted();
      }
      break;
    }

    case State::Scanning: {
      const int found = WiFi.scanComplete();
      if (found == WIFI_SCAN_RUNNING) {
        if (now - stateSince >= SCAN_TIMEOUT_MS) {
          radioDown("scan timed out");
          scheduleRetry();
        }
        break;
      }
      if (found < 0) {
        radioDown("scan failed");
        scheduleRetry();
        break;
      }
      collectScanResults(found);
      if (!joinNextCandidate()) onCandidatesExhausted();
      break;
    }

    case State::Connected:
      if (WiFi.status() != WL_CONNECTED) {
        radioDown("connection lost");
        scheduleRetry();
      } else if (idleMs >= IDLE_OFF_MS && !BookOrbitBackgroundDownloads::busy() && !HomeWebServer::uploading()) {
        radioDown("10 minutes on Home without a button press");
        idleOff = true;
      } else {
        HomeWebServer::handle();
      }
      break;

    case State::Released:
      break;
  }
}
}  // namespace

void HomeWifi::tick(const bool onHome, const unsigned long idleMs) {
  // A book from Home still downloading keeps the radio until it is in.
  if (stopForBook && !BookOrbitBackgroundDownloads::busy()) {
    stopForBook = false;
    if (state == State::Connected || (state == State::Released && WiFi.getMode() != WIFI_OFF)) {
      radioDown("a book opened");
    } else if (state == State::Released) {
      setState(State::Off);
    }
  }

  if (!SETTINGS.wifiAtHome) {
    if (owningRadio()) radioDown("Wi-Fi at Home turned off");
    attemptScheduled = false;
    wasOnHome = onHome;
    return;
  }

  if (!onHome) {
    wasOnHome = false;
    return;
  }
  if (!wasOnHome) {
    wasOnHome = true;
    onArrivedHome();
  }
  tickOnHome(idleMs);
}

void HomeWifi::onActivityEntering(const bool isHome, const bool isReader) {
  if (isHome) return;
  // The web page is Home's: the next screen may need the radio (or port 80) its own way.
  HomeWebServer::stop();
  switch (state) {
    case State::Joining:
    case State::Scanning:
      // The next activity may start the radio its own way; a half-made join would fight it.
      radioDown("left Home while joining");
      break;
    case State::Connected:
      if (isReader) {
        stopForBook = true;
      } else {
        LOG_INF("HWIFI", "Leaving the connection up for the next screen");
        setState(State::Released);
      }
      break;
    case State::Released:
      if (isReader) stopForBook = true;
      break;
    case State::Off:
      break;
  }
  attemptScheduled = false;
}

void HomeWifi::stop(const char* reason) {
  stopForBook = false;
  attemptScheduled = false;
  if (owningRadio()) {
    radioDown(reason);
  } else if (state == State::Released) {
    setState(State::Off);
  }
}

bool HomeWifi::isConnected() { return state == State::Connected && WiFi.status() == WL_CONNECTED; }

#else

void HomeWifi::tick(bool, unsigned long) {}
void HomeWifi::onActivityEntering(bool, bool) {}
void HomeWifi::stop(const char*) {}
bool HomeWifi::isConnected() { return false; }

#endif

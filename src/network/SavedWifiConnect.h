#pragma once

#include <string>

struct WifiCredential;

// Joins a saved Wi-Fi network with no UI, for the BookOrbit sync that runs behind the
// sleep screen. WifiSelectionActivity does the same with its screens: the last network
// joined first, then any other saved network a scan finds, strongest first.
namespace SavedWifiConnect {

// Polled while waiting on the radio; returning true abandons the attempt at once.
using StopCheck = bool (*)();

bool connect(StopCheck shouldStop);

// Takes the radio down, handing back the heap Wi-Fi holds.
void disconnect();

// The non-blocking pieces connect() is made of, for callers that poll the radio from
// their own loop (HomeWifi) instead of waiting on it.

// Brings the radio up in station mode, refusing on a heap too low to start the driver.
bool startStation();
// Starts joining one saved network and returns at once; poll WiFi.status(). The network
// joined last is joined directly on the router and channel it answered on, which skips the
// scan of every channel (a second or two); a failed direct join falls back to the scan on
// the next try (dropFastJoin).
void beginJoin(const WifiCredential& credential);
// A status that means this join will not succeed.
bool isJoinFailure(int status);
// After a join: the network that answered is the first one tried next time, on the same
// router and channel (see beginJoin).
void rememberJoined(const std::string& ssid);
// After a failed join: the next one to this network scans every channel again. Returns
// whether that join was a direct one, worth a second, scanning try.
bool dropFastJoin(const std::string& ssid);

}  // namespace SavedWifiConnect

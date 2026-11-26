#pragma once

#include <BoardConfig.h>

#include <cstdint>
#include <string>

// Download Missing Books: behind the sleep screen, on the connection the sleep sync already
// holds, puts every book of the BookOrbit library that is not on the card yet onto it,
// newest first. A book it once saw on the card and that is gone now was deleted on purpose,
// so it stays gone (the catalog still downloads it by hand).
//
// Boards with PSRAM only: on the X3 and X4 (ESP32-C3) the sleep sync's boot has no room to
// spare for a run of multi-MB TLS downloads after the sync itself.
#define CROSSINK_APP_SLEEP_AUTO_DOWNLOAD (!FREEINK_MCU_C3)

namespace BookOrbitAutoDownload {

// Polled between books and during each transfer; returning true ends the run at once.
using StopCheck = bool (*)();

// Below this battery level, away from USB, no further book starts.
constexpr uint16_t MIN_BATTERY_PERCENT = 15;
// Downloads the sleep sync prepares behind the sleep screen (BookPrep::prepareEpub), newest
// first; the rest are prepared when first opened.
constexpr uint8_t MAX_PREPARED = 5;

// Turning it on forgets the books it remembers as deleted, so the next sleep brings back
// every book missing from the card. Saves the store.
void setEnabled(bool enabled);

struct Summary {
  uint16_t downloaded = 0;
  bool cardFull = false;
  bool lowBattery = false;
  // The newest downloads (the first `preparedCount` entries), for the sleep sync to prepare
  // once Wi-Fi is off.
  uint8_t preparedCount = 0;
  std::string paths[MAX_PREPARED];
};

// One pass, on a connection that is already up: lists the whole library, newest first, and
// downloads each book that is neither on the card nor remembered as deleted from it. A
// wake, an unreachable server, a nearly full card or a low battery ends the pass; the next
// sleep carries on from what is still missing.
Summary run(StopCheck shouldStop);

}  // namespace BookOrbitAutoDownload

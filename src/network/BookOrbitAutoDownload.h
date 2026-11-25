#pragma once

#include <BoardConfig.h>

#include <cstdint>

// Downloads the books added to BookOrbit's Recently Added since the option was turned on,
// behind the sleep screen, on the connection the sleep sync already holds.
//
// Boards with PSRAM only: on the X3 and X4 (ESP32-C3) the sleep sync's boot has no room to
// spare for a run of multi-MB TLS downloads after the sync itself.
#define CROSSINK_APP_SLEEP_AUTO_DOWNLOAD (!FREEINK_MCU_C3)

namespace BookOrbitAutoDownload {

// Polled between books and during each transfer; returning true ends the run at once.
using StopCheck = bool (*)();

// Books downloaded per sleep at most; the rest wait for the next one.
constexpr uint8_t MAX_BOOKS_PER_SLEEP = 5;

// Turning it on takes only books added from now on: the watermark starts at the current
// time when the clock can be trusted, otherwise at the newest book the next run finds.
// Saves the store.
void setEnabled(bool enabled);

struct Summary {
  uint8_t downloaded = 0;
  bool cardFull = false;
};

// One pass, on a connection that is already up. Every book it takes, downloaded or found
// already on the card, moves the watermark past it, so a book deleted afterwards is not
// fetched again. A wake, a server error or a nearly full card ends the pass; the next
// sleep picks up from the watermark.
Summary run(StopCheck shouldStop);

}  // namespace BookOrbitAutoDownload

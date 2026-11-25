#pragma once

#include <BoardConfig.h>

#include <cstdint>
#include <string>

class GfxRenderer;

// Reading positions of the books other than the one the sync is about, on a connection that
// is already up: the other recent books behind the sleep screen, and a book being opened
// with Wi-Fi on at Home. Positions only (highlights, bookmarks and stats stay with a book's
// own sync), and the position saved most recently wins (BookOrbitSmartSync::decideNewestSave).
//
// Boards with PSRAM only: each book loads its EPUB while the TLS session is open, which the
// X3 and X4 (ESP32-C3) cannot hold together.
#define CROSSINK_APP_RECENT_BOOKS_SYNC (!FREEINK_MCU_C3)

namespace BookOrbitRecentSync {

// Polled between books; returning true ends the run at once.
using StopCheck = bool (*)();

// Recent EPUBs synced per sleep, most recent first, besides the book the sleep sync is for.
constexpr uint8_t MAX_BOOKS = 10;

struct Summary {
  uint8_t checked = 0;
  uint8_t applied = 0;
  uint8_t uploaded = 0;
};

// Syncs the recent books, skipping `skipPath` (already synced). Stops early on a network or
// account error, as every following book would meet the same one.
Summary syncRecentBooks(GfxRenderer& renderer, const std::string& skipPath, StopCheck shouldStop);

// For a book about to open: takes the server position when it is the newer save. Never
// uploads; the next sync does that. True when the saved position changed.
bool pullBeforeOpen(GfxRenderer& renderer, const std::string& bookPath);

}  // namespace BookOrbitRecentSync

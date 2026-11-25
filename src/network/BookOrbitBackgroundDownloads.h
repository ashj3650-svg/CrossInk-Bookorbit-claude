#pragma once

#include <BoardConfig.h>

#include <cstdint>
#include <string>

#include "network/BookOrbitBookDownload.h"

// Catalog books downloaded on a task of their own while the catalog stays usable: no
// downloading screen, the row shows the progress, and more books can be queued meanwhile.
//
// Boards with PSRAM only. The X3 and X4 (ESP32-C3) restart into a dedicated download boot
// for the heap a TLS download needs; a second task beside the browsing session would not
// fit next to it.
#define CROSSINK_APP_BACKGROUND_DOWNLOADS (!FREEINK_MCU_C3)

namespace BookOrbitBackgroundDownloads {

// Books waiting or downloading at once; a further request is refused.
constexpr size_t MAX_JOBS = 8;

struct Request {
  int64_t bookId = 0;
  // Non-empty: replace the copy at this path (a re-download) instead of placing the book
  // where the server's naming template puts it.
  std::string replacePath;
  // A copy already at the book's place, of the library file's size, is taken as it is.
  bool takeExisting = true;
};

struct Finished {
  int64_t bookId = 0;
  BookOrbitBookDownload::Result result = BookOrbitBookDownload::Result::Failed;
  std::string path;  // where the book is, or would have gone
};

// Starts the worker task when it is not running. False (logged) when the queue is full or
// the task cannot be created. Main task only.
bool enqueue(const Request& request);

// -1 when the book is neither waiting nor downloading; otherwise its percent (0 while it
// waits its turn).
int progressOf(int64_t bookId);

// True while any book is waiting or downloading.
bool busy();

// Bumped each time a book starts or ends, and at each 10% of the one downloading: the
// screen repaints when it changes.
uint32_t changeCount();

// The next book that ended, oldest first. Its bookkeeping belongs to the main task: a
// Downloaded one still needs BookOrbitBookDownload::finishTransfer(), an AlreadyOnCard one
// its download index record.
bool takeFinished(Finished& out);

// Drops the waiting books, stops the one downloading (its staged file is removed and a
// copy it would have replaced stays) and waits for the task to end. Books that already
// ended stay to be taken. Main task only.
void cancelAll();

}  // namespace BookOrbitBackgroundDownloads

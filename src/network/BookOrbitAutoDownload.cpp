#include "BookOrbitAutoDownload.h"

#include <HalStorage.h>
#include <Logging.h>
#include <WallClock.h>

#include <algorithm>
#include <string>
#include <vector>

#include "BookOrbitCredentialStore.h"
#include "BookOrbitDownloadIndex.h"
#include "network/BookOrbitBookDownload.h"
#include "network/BookOrbitCatalogClient.h"
#include "util/CardSpace.h"

namespace {

// Recently Added pages read per run while every book on them is still new: enough for a
// large import (the pass only takes MAX_BOOKS_PER_SLEEP of them anyway), and a bound on
// the requests a run makes before its first download.
constexpr int MAX_LISTING_PAGES = 5;
// Room the pass leaves on the card: 5% of it, at most 512 MB.
constexpr uint64_t MAX_RESERVE_BYTES = 512ull * 1024 * 1024;

// 16 bytes a book, at most MAX_LISTING_PAGES * PAGE_SIZE of them: the titles stay on the
// server until a book is actually downloaded.
struct Candidate {
  int64_t addedAt;
  int64_t id;
};

bool isNewer(const int64_t addedAt, const int64_t id, const int64_t afterAt, const int64_t afterId) {
  return addedAt > afterAt || (addedAt == afterAt && id > afterId);
}

// True when the card is known to be within the reserve. Only asked where the count is
// quick (exFAT); elsewhere the transfer's own check tells a full card apart.
bool cardNearlyFull() {
  if (!CardSpace::isQuickToCount()) return false;
  const uint64_t total = Storage.totalBytes();
  if (total == 0) return false;
  const uint64_t used = Storage.usedBytes();
  const uint64_t freeBytes = total > used ? total - used : 0;
  const uint64_t reserve = std::min(total / 20, MAX_RESERVE_BYTES);
  if (freeBytes >= reserve) return false;
  LOG_INF("BOAD", "Card nearly full (%llu bytes free): no more downloads", static_cast<unsigned long long>(freeBytes));
  return true;
}

void moveWatermark(const Candidate& book) {
  BOOKORBIT_STORE.setAutoDownload(true, book.addedAt, book.id);
  BOOKORBIT_STORE.saveToFile();
}

}  // namespace

void BookOrbitAutoDownload::setEnabled(const bool enabled) {
  int64_t after = 0;
  if (enabled) {
    uint32_t now = 0;
    if (WallClock::now(now)) after = now;
  }
  BOOKORBIT_STORE.setAutoDownload(enabled, after, 0);
  BOOKORBIT_STORE.saveToFile();
  LOG_INF("BOAD", "New-book downloads %s (after %lld)", enabled ? "on" : "off", static_cast<long long>(after));
}

BookOrbitAutoDownload::Summary BookOrbitAutoDownload::run(const StopCheck shouldStop) {
  Summary summary;
  if (!BOOKORBIT_STORE.getAutoDownloadNew() || !BOOKORBIT_STORE.hasCredentials()) return summary;

  const int64_t afterAt = BOOKORBIT_STORE.getAutoDownloadAfter();
  const int64_t afterId = BOOKORBIT_STORE.getAutoDownloadAfterId();
  const BookOrbitBookQuery query = BookOrbitCatalogClient::sectionBookQuery("recent");

  // The listing runs newest first: read it until a book is no longer new.
  std::vector<Candidate> candidates;
  candidates.reserve(BookOrbitCatalogClient::PAGE_SIZE);
  int64_t newestAt = 0;
  int64_t newestId = 0;
  bool reachedOld = false;
  for (int page = 1; page <= MAX_LISTING_PAGES && !reachedOld; page++) {
    if (shouldStop && shouldStop()) return summary;
    BookOrbitBookPage listing;
    if (!BookOrbitCatalogClient::fetchBooks(query, page, listing)) {
      LOG_ERR("BOAD", "Recently Added page %d could not be fetched", page);
      return summary;
    }
    for (const BookOrbitCatalogBook& book : listing.books) {
      if (book.addedAt == 0) continue;  // undated: cannot be placed against the watermark
      if (isNewer(book.addedAt, book.id, newestAt, newestId)) {
        newestAt = book.addedAt;
        newestId = book.id;
      }
      if (afterAt == 0) continue;
      if (!isNewer(book.addedAt, book.id, afterAt, afterId)) {
        reachedOld = true;
        continue;
      }
      candidates.push_back({static_cast<int64_t>(book.addedAt), book.id});
    }
    if (afterAt == 0) break;  // the baseline only needs the newest book
    const int shown = page * BookOrbitCatalogClient::PAGE_SIZE;
    if (listing.books.size() < static_cast<size_t>(BookOrbitCatalogClient::PAGE_SIZE) || shown >= listing.total) break;
  }

  if (afterAt == 0) {
    // Turned on without a trusted clock: what the server lists now is the baseline.
    if (newestAt == 0) {
      LOG_INF("BOAD", "The server does not date its books: nothing to start from");
      return summary;
    }
    BOOKORBIT_STORE.setAutoDownload(true, newestAt, newestId);
    BOOKORBIT_STORE.saveToFile();
    LOG_INF("BOAD", "New-book downloads start after book %lld, added %lld", static_cast<long long>(newestId),
            static_cast<long long>(newestAt));
    return summary;
  }
  if (candidates.empty()) {
    LOG_INF("BOAD", "No books added since %lld", static_cast<long long>(afterAt));
    return summary;
  }

  // Oldest first, so the watermark only ever moves past books that were taken.
  std::sort(candidates.begin(), candidates.end(),
            [](const Candidate& a, const Candidate& b) { return isNewer(b.addedAt, b.id, a.addedAt, a.id); });
  LOG_INF("BOAD", "%u new books on the server", static_cast<unsigned>(candidates.size()));

  const HttpDownloader::CancelCallback cancel = [shouldStop] { return shouldStop && shouldStop(); };
  for (const Candidate& book : candidates) {
    if (summary.downloaded >= MAX_BOOKS_PER_SLEEP) break;
    if (shouldStop && shouldStop()) break;
    std::string path;
    if (BookOrbitDownloadIndex::lookup(book.id, path)) {
      LOG_INF("BOAD", "Book %lld is already on the card: %s", static_cast<long long>(book.id), path.c_str());
      moveWatermark(book);
      continue;
    }
    if (cardNearlyFull()) {
      summary.cardFull = true;
      break;
    }
    const BookOrbitBookDownload::Result result = BookOrbitBookDownload::downloadBook(book.id, path, cancel);
    LOG_INF("BOAD", "Book %lld -> %s: result %d", static_cast<long long>(book.id), path.c_str(),
            static_cast<int>(result));
    bool stop = false;
    switch (result) {
      case BookOrbitBookDownload::Result::Downloaded:
        summary.downloaded++;
        moveWatermark(book);
        break;
      case BookOrbitBookDownload::Result::AlreadyOnCard:
      case BookOrbitBookDownload::Result::NoEpub:
        moveWatermark(book);
        break;
      case BookOrbitBookDownload::Result::Failed:
        // Three resumed attempts already failed: a book that will not download (or open as
        // an EPUB) must not hold every later one back.
        moveWatermark(book);
        break;
      case BookOrbitBookDownload::Result::NoSpace:
        summary.cardFull = true;
        stop = true;
        break;
      case BookOrbitBookDownload::Result::Unreachable:
      case BookOrbitBookDownload::Result::Cancelled:
        stop = true;
        break;
    }
    if (stop) break;
  }
  // Writes the entries the downloads recorded and frees the in-RAM copy.
  BookOrbitDownloadIndex::unload();
  return summary;
}

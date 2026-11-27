#include "BookOrbitAutoDownload.h"

#include <Arduino.h>
#include <HalGPIO.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <string>
#include <vector>

#include "BookOrbitCredentialStore.h"
#include "BookOrbitDownloadIndex.h"
#include "network/BookOrbitBookDownload.h"
#include "network/BookOrbitCatalogClient.h"
#include "util/BookOrbitNewBooks.h"

namespace {

// Library pages read per run: a bound on the requests a run makes before its first
// download, well past a home server's library.
constexpr int MAX_LISTING_PAGES = 100;
// Book ids a listing request asks for: a 250-book library in 3 requests.
constexpr int LIST_PAGE_SIZE = 100;
// Books remembered as seen on the card: 8 bytes each in RAM during a run (64 KB at most, on
// a PSRAM board), past any library MAX_LISTING_PAGES lists.
constexpr size_t MAX_SEEN = 8192;
// Room the pass leaves on the card: 5% of it, at most 512 MB.
constexpr uint64_t MAX_RESERVE_BYTES = 512ull * 1024 * 1024;
// Books in a row that fail to download (after their own retries) before the pass gives up:
// past that the server or the connection is the problem, not the books.
constexpr uint8_t MAX_FAILURES_IN_A_ROW = 3;

constexpr char SEEN_PATH[] = "/.crosspoint/bookorbit_auto_download.bin";
constexpr char SEEN_TMP_PATH[] = "/.crosspoint/bookorbit_auto_download.tmp";
constexpr uint32_t SEEN_MAGIC = 0x44414F42;  // "BOAD"
constexpr uint16_t SEEN_VERSION = 1;

struct SeenHeader {
  uint32_t magic = SEEN_MAGIC;
  uint16_t version = SEEN_VERSION;
  uint16_t reserved = 0;
  uint32_t serverHash = 0;
  uint32_t count = 0;
};
static_assert(sizeof(SeenHeader) == 16, "bookorbit_auto_download.bin starts with 16 bytes");

uint32_t serverHash() { return BookOrbitDownloadIndex::pathHash(BOOKORBIT_STORE.getBaseUrl()); }

// The books this setting has seen on the card, sorted. Empty when the file is missing, from
// another server or unreadable: then nothing counts as deleted and every missing book comes.
void loadSeen(std::vector<int64_t>& seen) {
  seen.clear();
  HalFile file;
  if (!Storage.exists(SEEN_PATH) || !Storage.openFileForRead("BOAD", SEEN_PATH, file)) return;
  SeenHeader header;
  const bool valid = file.read(&header, sizeof(header)) == static_cast<int>(sizeof(header)) &&
                     header.magic == SEEN_MAGIC && header.version == SEEN_VERSION &&
                     header.serverHash == serverHash() && header.count <= MAX_SEEN;
  if (valid) {
    seen.resize(header.count);
    const int bytes = static_cast<int>(header.count * sizeof(int64_t));
    if (bytes > 0 && file.read(seen.data(), bytes) != bytes) {
      LOG_ERR("BOAD", "Cannot read the books seen on the card");
      seen.clear();
    }
  }
  file.close();
  std::sort(seen.begin(), seen.end());
}

bool saveSeen(const std::vector<int64_t>& seen) {
  HalFile file;
  if (!Storage.openFileForWrite("BOAD", SEEN_TMP_PATH, file)) {
    LOG_ERR("BOAD", "Cannot open %s for writing", SEEN_TMP_PATH);
    return false;
  }
  SeenHeader header;
  header.serverHash = serverHash();
  header.count = static_cast<uint32_t>(seen.size());
  const size_t bytes = seen.size() * sizeof(int64_t);
  const bool ok = file.write(reinterpret_cast<const uint8_t*>(&header), sizeof(header)) == sizeof(header) &&
                  (bytes == 0 || file.write(reinterpret_cast<const uint8_t*>(seen.data()), bytes) == bytes);
  file.close();
  if (!ok) {
    LOG_ERR("BOAD", "Cannot write the books seen on the card");
    Storage.remove(SEEN_TMP_PATH);
    return false;
  }
  Storage.remove(SEEN_PATH);
  if (!Storage.rename(SEEN_TMP_PATH, SEEN_PATH)) {
    LOG_ERR("BOAD", "Cannot put %s in place", SEEN_PATH);
    return false;
  }
  return true;
}

bool isSeen(const std::vector<int64_t>& seen, const int64_t id) {
  return std::binary_search(seen.begin(), seen.end(), id);
}

void markSeen(std::vector<int64_t>& seen, const int64_t id) {
  const auto at = std::lower_bound(seen.begin(), seen.end(), id);
  if (at != seen.end() && *at == id) return;
  if (seen.size() >= MAX_SEEN) return;  // past any real library: the book may come back
  seen.insert(at, id);
}

// Each book is a fresh TLS session (details, then the file). Below this much internal heap,
// a long pass has fragmented it too far to start another safely; the next sleep sync starts
// from a fresh boot and carries on.
constexpr uint32_t MIN_FREE_HEAP_PER_BOOK = 40 * 1024;

bool heapTooLow() {
  const uint32_t freeHeap = ESP.getFreeHeap();
  if (freeHeap >= MIN_FREE_HEAP_PER_BOOK) return false;
  LOG_INF("BOAD", "Free heap %u: the rest waits for the next sleep", static_cast<unsigned>(freeHeap));
  return true;
}

bool batteryTooLow() {
  if (gpio.isUsbConnected()) return false;
  const uint16_t battery = powerManager.getBatteryPercentage();
  if (battery >= BookOrbitAutoDownload::MIN_BATTERY_PERCENT) return false;
  LOG_INF("BOAD", "Battery at %u%%: no more downloads", static_cast<unsigned>(battery));
  return true;
}

// The card's free space, counted once per pass and then kept up to date by subtracting
// each download. On FAT32 the count reads the whole allocation table (seconds, see
// CardSpace), which is why it waits until a book is actually about to download and never
// runs twice. Zero total: the card did not say, and the transfer's own check is all there is.
struct CardRoom {
  bool counted = false;
  uint64_t total = 0;
  uint64_t freeBytes = 0;

  bool nearlyFull() {
    if (!counted) {
      counted = true;
      total = Storage.totalBytes();
      const uint64_t used = total > 0 ? Storage.usedBytes() : 0;
      freeBytes = total > used ? total - used : 0;
    }
    if (total == 0) return false;
    const uint64_t reserve = std::min(total / 20, MAX_RESERVE_BYTES);
    if (freeBytes >= reserve) return false;
    LOG_INF("BOAD", "Card nearly full (%llu bytes free): no more downloads",
            static_cast<unsigned long long>(freeBytes));
    return true;
  }

  void took(const std::string& path) {
    FsFile file = Storage.open(path.c_str());
    const uint64_t size = file ? file.fileSize64() : 0;
    file.close();
    freeBytes = freeBytes > size ? freeBytes - size : 0;
  }
};

// A download for the New on your reader shelf, kept until the pass ends so the newest
// lands first there.
struct Listed {
  int64_t id = 0;
  std::string path;
  std::string title;
};

}  // namespace

void BookOrbitAutoDownload::setEnabled(const bool enabled) {
  if (enabled && Storage.exists(SEEN_PATH) && !Storage.remove(SEEN_PATH)) {
    LOG_ERR("BOAD", "Cannot forget the books seen on the card");
  }
  BOOKORBIT_STORE.setAutoDownloadNew(enabled);
  BOOKORBIT_STORE.saveToFile();
  LOG_INF("BOAD", "Missing-book downloads %s", enabled ? "on" : "off");
}

BookOrbitAutoDownload::Summary BookOrbitAutoDownload::run(const StopCheck shouldStop) {
  Summary summary;
  if (!BOOKORBIT_STORE.getAutoDownloadNew() || !BOOKORBIT_STORE.hasCredentials()) return summary;
  if (batteryTooLow()) {
    summary.lowBattery = true;
    return summary;
  }

  std::vector<int64_t> seen;
  loadSeen(seen);
  const size_t seenBefore = seen.size();

  // The whole library, newest first: ids only, 8 bytes a book, LIST_PAGE_SIZE to a request.
  // The titles stay on the server until a book is actually downloaded.
  const BookOrbitBookQuery query = BookOrbitCatalogClient::sectionBookQuery("recent");
  std::vector<int64_t> ids;
  for (int page = 1; page <= MAX_LISTING_PAGES; page++) {
    const bool stopped = shouldStop && shouldStop();
    const size_t before = ids.size();
    int total = 0;
    int pageSize = LIST_PAGE_SIZE;
    if (stopped || !BookOrbitCatalogClient::fetchBookIds(query, page, LIST_PAGE_SIZE, ids, total, pageSize)) {
      if (!stopped) LOG_ERR("BOAD", "Library page %d could not be fetched", page);
      return summary;
    }
    if (page == 1 && total > 0) ids.reserve(std::min(static_cast<size_t>(total), MAX_SEEN));
    const size_t got = ids.size() - before;
    // A short page is the last; so is reaching the total, when the server sends one.
    if (pageSize <= 0 || got < static_cast<size_t>(pageSize)) break;
    if (total > 0 && ids.size() >= static_cast<size_t>(total)) break;
  }

  // Sorted out against the card: on it (seen now), deleted from it (seen before), or missing.
  std::vector<int64_t> missing;
  missing.reserve(ids.size());
  size_t deleted = 0;
  for (const int64_t id : ids) {
    std::string path;
    if (BookOrbitDownloadIndex::lookup(id, path)) {
      markSeen(seen, id);
    } else if (isSeen(seen, id)) {
      deleted++;
    } else {
      missing.push_back(id);
    }
  }
  const size_t listed = ids.size();
  std::vector<int64_t>().swap(ids);
  LOG_INF("BOAD", "Library: %u books, %u missing from the card, %u deleted from it", static_cast<unsigned>(listed),
          static_cast<unsigned>(missing.size()), static_cast<unsigned>(deleted));

  const HttpDownloader::CancelCallback cancel = [shouldStop] { return shouldStop && shouldStop(); };
  CardRoom room;
  // Heap, not the loop task's stack: eight paths and titles.
  std::vector<Listed> newest;
  newest.reserve(BookOrbitNewBooks::MAX_BOOKS);
  uint8_t failuresInARow = 0;
  for (const int64_t id : missing) {
    if (shouldStop && shouldStop()) break;
    if (batteryTooLow()) {
      summary.lowBattery = true;
      break;
    }
    if (heapTooLow()) break;
    if (room.nearlyFull()) {
      summary.cardFull = true;
      break;
    }
    std::string path;
    std::string title;
    const BookOrbitBookDownload::Result result = BookOrbitBookDownload::downloadBook(id, path, cancel, &title);
    LOG_INF("BOAD", "Book %lld -> %s: result %d", static_cast<long long>(id), path.c_str(), static_cast<int>(result));
    bool stop = false;
    switch (result) {
      case BookOrbitBookDownload::Result::Downloaded:
        failuresInARow = 0;
        summary.downloaded++;
        room.took(path);
        markSeen(seen, id);
        if (newest.size() < BookOrbitNewBooks::MAX_BOOKS) newest.push_back({id, std::move(path), std::move(title)});
        break;
      case BookOrbitBookDownload::Result::AlreadyOnCard:
      case BookOrbitBookDownload::Result::NoEpub:
        // On the card after all, or nothing this reader can download: not asked about again.
        failuresInARow = 0;
        markSeen(seen, id);
        break;
      case BookOrbitBookDownload::Result::Failed:
        // Left unseen, so the next sleep tries it again; the rest of the library goes on.
        stop = ++failuresInARow >= MAX_FAILURES_IN_A_ROW;
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

  if (seen.size() != seenBefore || !Storage.exists(SEEN_PATH)) saveSeen(seen);
  // Oldest of them first, so the newest download heads New on your reader.
  for (auto book = newest.rbegin(); book != newest.rend(); ++book)
    BookOrbitNewBooks::add(book->id, book->path, book->title);
  for (size_t i = 0; i < newest.size() && i < MAX_PREPARED; i++)
    summary.paths[summary.preparedCount++] = newest[i].path;
  return summary;
}

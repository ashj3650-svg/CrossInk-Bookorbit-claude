#include "BookOrbitBackgroundDownloads.h"

#include <Arduino.h>
#include <HalStorage.h>
#include <Logging.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <atomic>
#include <deque>
#include <utility>

#include "BookOrbitDownloadIndex.h"
#include "BookOrbitSyncClient.h"
#include "I18n.h"
#include "network/BookOrbitCatalogClient.h"

namespace {

// The TLS session runs on this stack, as it does on the main loop's; wolfSSL is built with
// WOLFSSL_SMALL_STACK, so its large buffers are on the heap.
constexpr uint32_t TASK_STACK_BYTES = 12288;
// The main loop's priority: equal tasks take turns, and the browser's own requests are
// served as soon as the download in progress hands the network lock back.
constexpr BaseType_t TASK_PRIORITY = 1;
#if defined(configNUM_CORES) && configNUM_CORES > 1
constexpr BaseType_t TASK_CORE = 1;  // beside the main loop, away from CPU 0's Wi-Fi work
#else
constexpr BaseType_t TASK_CORE = 0;
#endif
constexpr int PROGRESS_STEP_PERCENT = 10;
constexpr unsigned long CANCEL_WAIT_MS = 30000;
// After a pause for the browser, how long the network stays its own: opening a listing is
// often a few requests in a row.
constexpr unsigned long RESUME_AFTER_MS = 1500;

// Guards everything below that is not atomic. Created by the first enqueue, on the main
// task, before the worker exists.
SemaphoreHandle_t stateMutex = nullptr;
std::deque<BookOrbitBackgroundDownloads::Request> waiting;
std::deque<BookOrbitBackgroundDownloads::Finished> finished;
bool running = false;

std::atomic<int64_t> activeBookId{0};
std::atomic<int> activePercent{0};
std::atomic<uint32_t> changes{0};
std::atomic<bool> cancelRequested{false};

class StateLock {
 public:
  StateLock() { xSemaphoreTake(stateMutex, portMAX_DELAY); }
  ~StateLock() { xSemaphoreGive(stateMutex); }
  StateLock(const StateLock&) = delete;
  StateLock& operator=(const StateLock&) = delete;
};

using BookOrbitBookDownload::Result;

// The whole of one book on the worker, short of what belongs to the main task (see
// takeFinished). Mirrors BookOrbitBookDownload::downloadBook.
Result runRequest(const BookOrbitBackgroundDownloads::Request& request, std::string& outPath) {
  BookOrbitBookDetail detail;
  if (!BookOrbitCatalogClient::fetchBookDetail(request.bookId, detail)) return Result::Unreachable;
  BookOrbitCatalogFile epubFile;
  if (!BookOrbitBookDownload::hasEpubFile(detail, epubFile)) return Result::NoEpub;

  outPath =
      request.replacePath.empty() ? BookOrbitBookDownload::catalogBookPath(detail, epubFile) : request.replacePath;
  if (request.takeExisting && epubFile.sizeBytes > 0) {
    FsFile existing = Storage.open(outPath.c_str());
    const bool same = existing && !existing.isDirectory() && existing.fileSize64() == epubFile.sizeBytes;
    existing.close();
    if (same) return Result::AlreadyOnCard;
  }
  if (!BookOrbitBookDownload::ensureParentFolder(outPath)) return Result::Failed;

  BookOrbitBookDownload::prepareTransfer(outPath);
  int shownPercent = 0;
  const HttpDownloader::ProgressCallback progress = [&shownPercent](const size_t downloaded, const size_t total) {
    if (total == 0) return;
    const int percent = static_cast<int>(static_cast<uint64_t>(downloaded) * 100 / total);
    // A resumed transfer may count from where it picked up: the row never goes back.
    if (percent > activePercent.load()) activePercent.store(percent);
    if (percent >= shownPercent + PROGRESS_STEP_PERCENT) {
      shownPercent = percent - percent % PROGRESS_STEP_PERCENT;
      changes.fetch_add(1);
    }
  };
  // The browser's requests go first: one waiting for the network pauses the transfer,
  // which then resumes from its staged file.
  const HttpDownloader::CancelCallback yieldOrCancel = [] {
    return cancelRequested.load() || BookOrbitCatalogClient::requestWaiting();
  };
  HttpDownloader::DownloadError result = HttpDownloader::HTTP_ERROR;
  bool resume = false;
  for (;;) {
    result = BookOrbitBookDownload::transferFile(epubFile.id, outPath, progress, yieldOrCancel, nullptr,
                                                 /*pausable=*/true, resume);
    if (result != HttpDownloader::ABORTED || cancelRequested.load()) break;
    LOG_DBG("BOBG", "Book %lld paused for a catalog request", static_cast<long long>(request.bookId));
    // Until the browser's request and the ones right behind it are through.
    while (BookOrbitCatalogClient::requestWaiting() && !cancelRequested.load()) delay(50);
    delay(RESUME_AFTER_MS);
    resume = true;
  }
  if (result == HttpDownloader::ABORTED) Storage.remove(BookOrbitBookDownload::stagedPath(outPath).c_str());
  switch (result) {
    case HttpDownloader::OK:
      return Result::Downloaded;
    case HttpDownloader::ABORTED:
      return Result::Cancelled;
    case HttpDownloader::INSUFFICIENT_SPACE:
      return Result::NoSpace;
    default:
      return Result::Failed;
  }
}

void workerTask(void*) {
  LOG_INF("BOBG", "Background downloads started (free=%u maxAlloc=%u)", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  for (;;) {
    BookOrbitBackgroundDownloads::Request request;
    {
      const StateLock lock;
      if (waiting.empty() || cancelRequested.load()) {
        running = false;
        break;
      }
      request = std::move(waiting.front());
      waiting.pop_front();
      activePercent.store(0);
      activeBookId.store(request.bookId);
    }
    changes.fetch_add(1);

    BookOrbitBackgroundDownloads::Finished done;
    done.bookId = request.bookId;
    done.result = runRequest(request, done.path);
    LOG_INF("BOBG", "Book %lld -> %s: result %d", static_cast<long long>(done.bookId), done.path.c_str(),
            static_cast<int>(done.result));
    {
      const StateLock lock;
      finished.push_back(std::move(done));
      activeBookId.store(0);
    }
    changes.fetch_add(1);
  }
  changes.fetch_add(1);
  LOG_INF("BOBG", "Background downloads idle (free=%u)", ESP.getFreeHeap());
  vTaskDelete(nullptr);
}

}  // namespace

bool BookOrbitBackgroundDownloads::enqueue(const Request& request) {
  if (!stateMutex) {
    stateMutex = xSemaphoreCreateMutex();
    if (!stateMutex) {
      LOG_ERR("BOBG", "Cannot create the download queue lock");
      return false;
    }
  }
  // Before the worker's first request: the browser's requests and the worker's now take
  // turns, and the device id the detail request sends is computed on this task.
  BookOrbitCatalogClient::createNetworkLock();
  (void)BookOrbitSyncClient::deviceId();

  const StateLock lock;
  if (waiting.size() + (running ? 1 : 0) >= MAX_JOBS) {
    LOG_ERR("BOBG", "Download queue full: book %lld not queued", static_cast<long long>(request.bookId));
    return false;
  }
  waiting.push_back(request);
  if (!running) {
    cancelRequested.store(false);
    TaskHandle_t handle = nullptr;
    if (xTaskCreatePinnedToCore(&workerTask, "BookOrbitDownload", TASK_STACK_BYTES, nullptr, TASK_PRIORITY, &handle,
                                TASK_CORE) != pdPASS) {
      LOG_ERR("BOBG", "Cannot start the download task (free=%u)", ESP.getFreeHeap());
      waiting.pop_back();
      return false;
    }
    running = true;
  }
  LOG_INF("BOBG", "Book %lld queued (%u waiting)", static_cast<long long>(request.bookId),
          static_cast<unsigned>(waiting.size()));
  changes.fetch_add(1);
  return true;
}

int BookOrbitBackgroundDownloads::progressOf(const int64_t bookId) {
  if (!stateMutex) return -1;
  const StateLock lock;
  if (activeBookId.load() == bookId) return activePercent.load();
  for (const Request& request : waiting) {
    if (request.bookId == bookId) return 0;
  }
  return -1;
}

bool BookOrbitBackgroundDownloads::busy() {
  if (!stateMutex) return false;
  const StateLock lock;
  return running || !waiting.empty();
}

uint32_t BookOrbitBackgroundDownloads::changeCount() { return changes.load(); }

bool BookOrbitBackgroundDownloads::takeFinished(Finished& out) {
  if (!stateMutex) return false;
  const StateLock lock;
  if (finished.empty()) return false;
  out = std::move(finished.front());
  finished.pop_front();
  return true;
}

const char* BookOrbitBackgroundDownloads::settle(const Finished& done, bool& onCard) {
  using BookOrbitBookDownload::Result;
  onCard = false;
  switch (done.result) {
    case Result::Downloaded:
      BookOrbitBookDownload::finishTransfer(done.bookId, done.path);
      onCard = true;
      return tr(STR_BOOK_DOWNLOADED);
    case Result::AlreadyOnCard:
      BookOrbitDownloadIndex::record(done.bookId, done.path);
      onCard = true;
      return nullptr;
    case Result::NoEpub:
      return tr(STR_NO_EPUB_FORMAT);
    case Result::Unreachable:
      return tr(STR_BOOKORBIT_CATALOG_ERROR);
    case Result::NoSpace:
      return tr(STR_SD_CARD_FULL);
    case Result::Failed:
      return tr(STR_DOWNLOAD_FAILED);
    case Result::Cancelled:
      return nullptr;
  }
  return nullptr;
}

void BookOrbitBackgroundDownloads::cancelAll() {
  if (!stateMutex) return;
  {
    const StateLock lock;
    if (!running && waiting.empty()) return;
    LOG_INF("BOBG", "Cancelling %u waiting downloads and the one in progress", static_cast<unsigned>(waiting.size()));
    waiting.clear();
    cancelRequested.store(true);
  }
  const unsigned long started = millis();
  for (;;) {
    {
      const StateLock lock;
      if (!running) break;
    }
    if (millis() - started > CANCEL_WAIT_MS) {
      LOG_ERR("BOBG", "Download task still running after %lu ms", CANCEL_WAIT_MS);
      break;
    }
    delay(20);
  }
}

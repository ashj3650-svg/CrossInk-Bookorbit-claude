#include "BookOrbitRecentSync.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <ctime>
#include <memory>
#include <vector>

#include "BookOrbitCredentialStore.h"
#include "BookOrbitRemotePosition.h"
#include "BookOrbitSyncClient.h"
#include "BookOrbitSyncMarkerFile.h"
#include "CrossPointSettings.h"
#include "KOReaderDocumentId.h"
#include "RecentBooksStore.h"
#include "activities/home/RecentBookProgress.h"
#include "activities/reader/EpubReaderUtils.h"
#include "util/BookContentId.h"
#include "util/BookOrbitSmartSync.h"
#include "util/BookUpdateMigration.h"
#include "util/ProgressSaveTime.h"

namespace {

enum class Outcome : uint8_t { Skipped, Unchanged, Applied, Uploaded, Failed };

const char* outcomeName(const Outcome outcome) {
  switch (outcome) {
    case Outcome::Skipped:
      return "skipped";
    case Outcome::Unchanged:
      return "unchanged";
    case Outcome::Applied:
      return "took the server position";
    case Outcome::Uploaded:
      return "sent this position";
    case Outcome::Failed:
      return "failed";
  }
  return "?";
}

bool applyRemote(const std::shared_ptr<Epub>& epub, const CrossPointPosition& position, const int64_t remoteTimestamp,
                 const std::string& stateDir) {
  const int pageCount = std::max(position.totalPages, position.pageNumber + 1);
  const std::optional<uint32_t> offset =
      position.hasVisibleTextOffset ? std::optional<uint32_t>(position.visibleTextOffset) : std::nullopt;
  if (!EpubReaderUtils::saveProgress(*epub, position.spineIndex, position.pageNumber, pageCount, offset)) {
    LOG_ERR("BORS", "Could not save the server position for %s", epub->getPath().c_str());
    return false;
  }
  RecentBookProgress::saveCachedEpubPercent(*epub, position.spineIndex, position.pageNumber, pageCount);
  BookOrbitSyncMarkerFile::write(stateDir, remoteTimestamp, {position.spineIndex, position.pageNumber, pageCount});
  return true;
}

bool uploadLocal(const std::shared_ptr<Epub>& epub, const std::string& documentHash,
                 const EpubReaderUtils::Progress& local, const BookOrbitSmartSync::Position& localPos,
                 const std::string& stateDir) {
  CrossPointPosition position{local.spineIndex, local.pageNumber, std::max(1, local.pageCount)};
  if (local.hasVisibleTextOffset) {
    position.visibleTextOffset = local.visibleTextOffset;
    position.hasVisibleTextOffset = true;
  }
  const KOReaderPosition koPosition = ProgressMapper::toKOReader(epub, position);
  KOReaderProgress progress;
  progress.document = documentHash;
  progress.progress = koPosition.xpath;
  progress.percentage = koPosition.percentage;
  progress.device = SETTINGS.getEffectiveDeviceName();
  progress.timestamp = time(nullptr);
  const auto result = BookOrbitSyncClient::updateProgress(progress);
  if (result != BookOrbitSyncClient::OK) {
    LOG_ERR("BORS", "Upload for %s failed: %d", epub->getPath().c_str(), static_cast<int>(result));
    return false;
  }
  BookOrbitSyncMarkerFile::write(stateDir, progress.timestamp, localPos);
  return true;
}

// One book, on the connection (and Session) the caller holds open.
Outcome syncBook(GfxRenderer& renderer, const std::string& path, const bool allowUpload) {
  if (!FsHelpers::hasEpubExtension(path) || !Storage.exists(path.c_str())) return Outcome::Skipped;
  // A catalog update the reader has not finished yet: its own sync settles it.
  if (BookUpdateMigration::pending(path)) return Outcome::Skipped;

  const std::string documentHash = KOReaderDocumentId::calculate(path);
  if (documentHash.empty()) return Outcome::Skipped;

  KOReaderProgress remote;
  const auto fetched = BookOrbitSyncClient::getProgress(documentHash, remote);
  if (fetched != BookOrbitSyncClient::OK && fetched != BookOrbitSyncClient::NOT_FOUND) {
    LOG_ERR("BORS", "Progress fetch for %s failed: %d", path.c_str(), static_cast<int>(fetched));
    return Outcome::Failed;
  }

  // Loaded only now: a book the server has never seen and this device cannot upload for
  // needs no EPUB in memory at all.
  if (fetched == BookOrbitSyncClient::NOT_FOUND && !allowUpload) return Outcome::Unchanged;
  auto epub = std::make_shared<Epub>(path, "/.crosspoint");
  if (!epub->load(true, SETTINGS.embeddedStyle == 0)) {
    LOG_ERR("BORS", "Could not load %s", path.c_str());
    return Outcome::Skipped;
  }
  epub->setupCacheDir();

  EpubReaderUtils::Progress local;
  const bool hasLocal = EpubReaderUtils::loadProgress(*epub, local, "BORS");
  const std::string stateDir = BookContentId::bookStateDir(path);
  if (stateDir.empty()) return Outcome::Skipped;
  const int localPageCount = local.hasPageCount ? std::max(1, local.pageCount) : 1;
  const BookOrbitSmartSync::Position localPos{local.spineIndex, local.pageNumber, localPageCount};

  if (fetched == BookOrbitSyncClient::NOT_FOUND) {
    if (!hasLocal) return Outcome::Unchanged;
    return uploadLocal(epub, documentHash, local, localPos, stateDir) ? Outcome::Uploaded : Outcome::Failed;
  }

  const CrossPointPosition remotePosition =
      BookOrbitRemotePosition::toLocal(epub, renderer, remote, local.spineIndex, localPageCount);
  if (!remotePosition.valid || remotePosition.spineIndex < 0 ||
      remotePosition.spineIndex >= epub->getSpineItemsCount()) {
    LOG_INF("BORS", "Server position for %s does not map into this copy", path.c_str());
    return Outcome::Skipped;
  }
  if (!hasLocal) {
    return applyRemote(epub, remotePosition, remote.timestamp, stateDir) ? Outcome::Applied : Outcome::Failed;
  }

  const BookOrbitSmartSync::Position remotePos{remotePosition.spineIndex, remotePosition.pageNumber,
                                               remotePosition.totalPages};
  const BookOrbitSmartSync::Marker marker = BookOrbitSyncMarkerFile::read(stateDir);
  int64_t localSavedAt = 0;
  ProgressSaveTime::load(path, localSavedAt);
  const BookOrbitSmartSync::Decision decision =
      BookOrbitSmartSync::decideNewestSave(localPos, remotePos, remote.timestamp, marker, localSavedAt);
  LOG_DBG("BORS", "%s: local %d/%d/%d saved %lld, server %d/%d/%d dated %lld -> %d", path.c_str(), localPos.spineIndex,
          localPos.pageNumber, localPos.pageCount, (long long)localSavedAt, remotePos.spineIndex, remotePos.pageNumber,
          remotePos.pageCount, (long long)remote.timestamp, static_cast<int>(decision));
  switch (decision) {
    case BookOrbitSmartSync::Decision::AlreadySynced:
      BookOrbitSyncMarkerFile::write(stateDir, remote.timestamp, BookOrbitSmartSync::furtherOf(localPos, remotePos));
      return Outcome::Unchanged;
    case BookOrbitSmartSync::Decision::ApplyRemote:
      return applyRemote(epub, remotePosition, remote.timestamp, stateDir) ? Outcome::Applied : Outcome::Failed;
    case BookOrbitSmartSync::Decision::UploadLocal:
      if (!allowUpload) return Outcome::Unchanged;
      return uploadLocal(epub, documentHash, local, localPos, stateDir) ? Outcome::Uploaded : Outcome::Failed;
    case BookOrbitSmartSync::Decision::AskNoHistory:
    case BookOrbitSmartSync::Decision::AskConflict:
      // No dates to compare: a manual sync of the book asks.
      return Outcome::Unchanged;
  }
  return Outcome::Unchanged;
}

}  // namespace

BookOrbitRecentSync::Summary BookOrbitRecentSync::syncRecentBooks(GfxRenderer& renderer, const std::string& skipPath,
                                                                  const StopCheck shouldStop) {
  Summary summary;
  if (!BOOKORBIT_STORE.hasCredentials()) return summary;

  // Copied first: syncing a book never reorders the list, but the copy keeps this loop
  // independent of the store. Paths only, at most MAX_BOOKS of them.
  std::vector<std::string> paths;
  paths.reserve(MAX_BOOKS);
  for (const RecentBook& book : RECENT_BOOKS.getBooks()) {
    if (paths.size() >= MAX_BOOKS) break;
    if (book.path == skipPath || !FsHelpers::hasEpubExtension(book.path)) continue;
    paths.push_back(book.path);
  }
  if (paths.empty()) return summary;

  LOG_INF("BORS", "Syncing %u other recent books", static_cast<unsigned>(paths.size()));
  BookOrbitSyncClient::Session session;
  for (const std::string& path : paths) {
    if (shouldStop && shouldStop()) {
      LOG_INF("BORS", "Recent-book sync stopped after %u books", static_cast<unsigned>(summary.checked));
      break;
    }
    const Outcome outcome = syncBook(renderer, path, /*allowUpload=*/true);
    LOG_INF("BORS", "%s: %s", path.c_str(), outcomeName(outcome));
    if (outcome == Outcome::Failed) break;
    if (outcome == Outcome::Skipped) continue;
    summary.checked++;
    if (outcome == Outcome::Applied) summary.applied++;
    if (outcome == Outcome::Uploaded) summary.uploaded++;
  }
  return summary;
}

bool BookOrbitRecentSync::pullBeforeOpen(GfxRenderer& renderer, const std::string& bookPath) {
  if (!BOOKORBIT_STORE.hasCredentials()) return false;
  const Outcome outcome = syncBook(renderer, bookPath, /*allowUpload=*/false);
  LOG_INF("BORS", "Sync on open, %s: %s", bookPath.c_str(), outcomeName(outcome));
  return outcome == Outcome::Applied;
}

#include "BookOrbitBookDownload.h"

#include <HalStorage.h>
#include <LibraryBuilder.h>
#include <Logging.h>
#include <ZipFile.h>

#include <algorithm>
#include <cctype>

#include "AppVersion.h"
#include "BookOrbitCredentialStore.h"
#include "BookOrbitDownloadIndex.h"
#include "util/BookCacheUtils.h"
#include "util/BookOrbitDevicePath.h"
#include "util/BookUpdateMigration.h"

namespace {
constexpr size_t DOWNLOAD_BUFFER_SIZE = 2048;
// Where HttpDownloader stages a download until it is complete and checked (see
// DownloadFileSwap). Not ".epub", so a leftover never shows up as a book.
constexpr char STAGED_DOWNLOAD_SUFFIX[] = ".part";
constexpr int MAX_DOWNLOAD_ATTEMPTS = 3;
}  // namespace

std::string BookOrbitBookDownload::catalogBookFilename(const std::string& title, const std::string& author) {
  return BookOrbitDevicePath::catalogFilename("", title, author);
}

std::string BookOrbitBookDownload::catalogBookPath(const BookOrbitBookDetail& detail,
                                                   const BookOrbitCatalogFile& file) {
  return BOOKORBIT_STORE.getDownloadFolder() +
         BookOrbitDevicePath::catalogFilename(file.devicePath, detail.title, detail.author);
}

bool BookOrbitBookDownload::hasEpubFile(const BookOrbitBookDetail& detail, BookOrbitCatalogFile& outFile) {
  for (const auto& file : detail.files) {
    std::string format = file.format;
    std::transform(format.begin(), format.end(), format.begin(), [](unsigned char c) { return std::tolower(c); });
    if (format == "epub") {
      outFile = file;
      return true;
    }
  }
  return false;
}

bool BookOrbitBookDownload::ensureParentFolder(const std::string& path) {
  // mkdir's pFlag creates the missing parents in one call, so a naming template that nests
  // the book ("Sprawl/<book>.epub") is covered too.
  const size_t lastSlash = path.find_last_of('/');
  if (lastSlash == 0 || lastSlash == std::string::npos) return true;
  const std::string folder = path.substr(0, lastSlash);
  if (Storage.exists(folder.c_str()) || Storage.mkdir(folder.c_str())) return true;
  LOG_ERR("BookOrbit", "Could not create download folder %s", folder.c_str());
  return false;
}

void BookOrbitBookDownload::prepareTransfer(const std::string& finalPath) {
  // A replacement a reset interrupted: this download would drop its old copy
  // (DownloadFileSwap::recover), so the state that copy holds moves first.
  if (BookUpdateMigration::pending(finalPath)) BookUpdateMigration::begin(finalPath, /*renderCacheIsOld=*/false);
}

HttpDownloader::DownloadError BookOrbitBookDownload::transferFile(const int64_t fileId, const std::string& finalPath,
                                                                  const HttpDownloader::ProgressCallback& progress,
                                                                  const HttpDownloader::CancelCallback& shouldCancel,
                                                                  bool* cancelFlag, const bool pausable,
                                                                  const bool resume) {
  // The heap the TLS session starts from: what tells a fragmented session from a clean
  // download boot in a field log. The version says which build wrote that log.
  LOG_INF("BookOrbit", "Downloading file %lld -> %s (free=%u maxAlloc=%u, firmware %s)", static_cast<long long>(fileId),
          finalPath.c_str(), ESP.getFreeHeap(), ESP.getMaxAllocHeap(), AppVersion::version());

  HttpDownloader::DownloadOptions options;
  options.shouldCancel = shouldCancel;
  options.bufferSize = DOWNLOAD_BUFFER_SIZE;
  // TLS runs with a few KB of headroom on the C3 and reads can drop mid-transfer;
  // keep the partial file and resume instead of restarting a multi-MB download.
  options.preservePartial = true;
  // Small client RX buffer (see BookOrbitCatalogClient::fetchJson) so the headers-time
  // body cache can't demand a large realloc next to the TLS buffers.
  options.clientRxBufferSize = 2048;
  options.largeBody = true;
  // CrossInk v1.6.1's download safeguards, as its OPDS browser uses them: stage the file
  // as "<book>.part", refuse a book the card cannot hold before writing any of it, and
  // only put the file in place once it opens as an EPUB.
  options.stageAsPart = true;
  // A re-download's highlights, bookmarks and sync state are keyed by the old copy's
  // content; it stays beside the new one until they have moved (finishTransfer).
  options.keepReplaced = true;
  options.checkFreeSpace = true;
  options.keepPartialOnAbort = pausable;
  options.validate = [](const std::string& path) {
    ZipFile zip(path);
    size_t size = 0;
    return zip.getInflatedFileSize("META-INF/container.xml", &size) && size > 0;
  };

  HttpDownloader::DownloadError result = HttpDownloader::HTTP_ERROR;
  for (int attempt = 0; attempt < MAX_DOWNLOAD_ATTEMPTS; attempt++) {
    // Honor a cancel between attempts too: a failing download otherwise runs all its
    // retries (TLS handshake included) with the request ignored.
    if ((shouldCancel && shouldCancel()) || (cancelFlag && *cancelFlag)) {
      result = HttpDownloader::ABORTED;
      break;
    }
    options.resumePartial = resume || attempt > 0;
    result = BookOrbitCatalogClient::downloadFile(fileId, finalPath, progress, cancelFlag, options);
    // A full card will not have more room on the next attempt.
    if (result == HttpDownloader::OK || result == HttpDownloader::ABORTED ||
        result == HttpDownloader::INSUFFICIENT_SPACE) {
      break;
    }
    LOG_ERR("BookOrbit", "Download attempt %d/%d failed (err=%d), retrying", attempt + 1, MAX_DOWNLOAD_ATTEMPTS,
            static_cast<int>(result));
  }

  if (result != HttpDownloader::OK && result != HttpDownloader::ABORTED) {
    // preservePartial kept the staged file for resuming between attempts; don't leave it
    // behind once we give up.
    Storage.remove((finalPath + STAGED_DOWNLOAD_SUFFIX).c_str());
  }
  return result;
}

std::string BookOrbitBookDownload::stagedPath(const std::string& finalPath) {
  return finalPath + STAGED_DOWNLOAD_SUFFIX;
}

void BookOrbitBookDownload::finishTransfer(const int64_t bookId, const std::string& finalPath) {
  // A copy it replaced hands its state to the new one; that also clears the render cache.
  if (BookUpdateMigration::pending(finalPath)) {
    BookUpdateMigration::begin(finalPath, /*renderCacheIsOld=*/true);
  } else {
    clearBookCache(finalPath);
  }
  BookOrbitDownloadIndex::record(bookId, finalPath);
  // A new book, or new contents under a known path: the Library's index is stale.
  library::invalidateLibraryIndex();
}

HttpDownloader::DownloadError BookOrbitBookDownload::transfer(const int64_t bookId, const int64_t fileId,
                                                              const std::string& finalPath,
                                                              const HttpDownloader::ProgressCallback& progress,
                                                              const HttpDownloader::CancelCallback& shouldCancel,
                                                              bool* cancelFlag) {
  prepareTransfer(finalPath);
  const HttpDownloader::DownloadError result = transferFile(fileId, finalPath, progress, shouldCancel, cancelFlag);
  if (result == HttpDownloader::OK) finishTransfer(bookId, finalPath);
  return result;
}

BookOrbitBookDownload::Result BookOrbitBookDownload::downloadBook(const int64_t bookId, std::string& outPath,
                                                                  const HttpDownloader::CancelCallback& shouldCancel) {
  BookOrbitBookDetail detail;
  if (!BookOrbitCatalogClient::fetchBookDetail(bookId, detail)) return Result::Unreachable;
  BookOrbitCatalogFile epubFile;
  if (!hasEpubFile(detail, epubFile)) return Result::NoEpub;

  outPath = catalogBookPath(detail, epubFile);
  // A copy already where the naming template puts it, of the library file's size, is
  // this book (sent by another app, or copied over USB): record it instead.
  if (epubFile.sizeBytes > 0) {
    FsFile existing = Storage.open(outPath.c_str());
    const bool same = existing && !existing.isDirectory() && existing.fileSize64() == epubFile.sizeBytes;
    existing.close();
    if (same) {
      BookOrbitDownloadIndex::record(bookId, outPath);
      return Result::AlreadyOnCard;
    }
  }
  if (!ensureParentFolder(outPath)) return Result::Failed;

  switch (transfer(bookId, epubFile.id, outPath, nullptr, shouldCancel, nullptr)) {
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

#include "BookOrbitBookDownload.h"

#include "BookOrbitDownloadIndex.h"
#include "network/BookOrbitCatalogClient.h"

BookOrbitBookDownload::Result BookOrbitBookDownload::resultOf(const HttpDownloader::DownloadError error) {
  switch (error) {
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

BookOrbitBookDownload::Result BookOrbitBookDownload::downloadBook(const int64_t bookId, std::string& outPath,
                                                                  const HttpDownloader::CancelCallback& shouldCancel,
                                                                  std::string* outTitle) {
  BookOrbitBookDetail detail;
  if (!BookOrbitCatalogClient::fetchBookDetail(bookId, detail)) return Result::Unreachable;
  if (outTitle) *outTitle = detail.title;
  BookOrbitCatalogFile epubFile;
  if (!BookOrbitCatalogDownload::findEpubFile(detail, epubFile)) return Result::NoEpub;

  outPath = BookOrbitCatalogDownload::destination(detail, epubFile);
  // A copy already where the naming template puts it, of the library file's size, is
  // this book (sent by another app, or copied over USB): record it instead.
  if (BookOrbitCatalogDownload::isAlreadyAt(outPath, epubFile)) {
    BookOrbitDownloadIndex::record(bookId, outPath);
    return Result::AlreadyOnCard;
  }
  if (!BookOrbitCatalogDownload::makeFolders(outPath)) return Result::Failed;

  CallbackReporter reporter(nullptr, shouldCancel);
  return resultOf(BookOrbitCatalogDownload::transfer(bookId, epubFile.id, epubFile.downloadVariant, outPath, reporter));
}

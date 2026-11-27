#pragma once

#include <cstdint>
#include <string>

#include "activities/browser/bookorbit/BookOrbitCatalogDownload.h"
#include "network/HttpDownloader.h"

// Putting one BookOrbit catalog book on the card with no screen of its own, for the
// background downloads the catalog starts and the sleep sync's automatic download of new
// books. The transfer itself, with its safeguards and resume, is the catalog's own
// (BookOrbitCatalogDownload); this adds a reporter made of callbacks and the whole of one
// book from its details.
namespace BookOrbitBookDownload {

// A BookOrbitCatalogDownload::Reporter from a progress and a cancel callback, either empty.
class CallbackReporter final : public BookOrbitCatalogDownload::Reporter {
 public:
  CallbackReporter(HttpDownloader::ProgressCallback progress, HttpDownloader::CancelCallback shouldCancel)
      : progress_(std::move(progress)), shouldCancel_(std::move(shouldCancel)) {}
  void releaseForTransfer() override {}
  void reportProgress(const size_t downloaded, const size_t total, bool) override {
    if (progress_) progress_(downloaded, total);
  }
  bool cancelRequested() override { return shouldCancel_ && shouldCancel_(); }

 private:
  HttpDownloader::ProgressCallback progress_;
  HttpDownloader::CancelCallback shouldCancel_;
};

enum class Result : uint8_t { Downloaded, AlreadyOnCard, NoEpub, Unreachable, NoSpace, Failed, Cancelled };

Result resultOf(HttpDownloader::DownloadError error);

// The whole of one download, from the book's details: nothing is fetched when the book is
// already where it belongs (same size), which the index then records. outPath is where it
// is or would have gone; outTitle, when given, the book's title.
Result downloadBook(int64_t bookId, std::string& outPath, const HttpDownloader::CancelCallback& shouldCancel,
                    std::string* outTitle = nullptr);

}  // namespace BookOrbitBookDownload

#pragma once

#include <cstdint>
#include <string>

#include "network/BookOrbitCatalogClient.h"
#include "network/HttpDownloader.h"

// Putting one BookOrbit catalog book on the card, with no screen of its own: where it
// belongs, the transfer with its safeguards, and the bookkeeping after. Shared by the
// catalog browser, the background downloads it starts, and the sleep sync's automatic
// download of newly added books.
namespace BookOrbitBookDownload {

// The SD filename (leading slash, no folder) this firmware falls back to when the server
// does not say where the file belongs.
std::string catalogBookFilename(const std::string& title, const std::string& author);

// Where a catalog file belongs under the download folder: BookOrbit resolves the account's
// KOReader file naming template and returns it as the file's devicePath, so a book lands
// where the same account's KOReader devices put it; servers that do not send one keep the
// title-author name. Leading slash included.
std::string catalogBookPath(const BookOrbitBookDetail& detail, const BookOrbitCatalogFile& file);

bool hasEpubFile(const BookOrbitBookDetail& detail, BookOrbitCatalogFile& outFile);

// Creates the folders a book path asks for. False (logged) when one cannot be made.
bool ensureParentFolder(const std::string& path);

// The three steps of transfer(), for a transfer run on another task: prepareTransfer and
// finishTransfer touch the book's saved state and the download index, which belong to the
// main task; transferFile only the network and the staged file.
void prepareTransfer(const std::string& finalPath);
// pausable: a cancel keeps the staged file, and resume continues it on the next call. The
// caller removes it (stagedPath) when it gives the download up instead.
HttpDownloader::DownloadError transferFile(int64_t fileId, const std::string& finalPath,
                                           const HttpDownloader::ProgressCallback& progress,
                                           const HttpDownloader::CancelCallback& shouldCancel, bool* cancelFlag,
                                           bool pausable = false, bool resume = false);
std::string stagedPath(const std::string& finalPath);
// After a transfer that returned OK.
void finishTransfer(int64_t bookId, const std::string& finalPath);

// Downloads a file to finalPath: staged as "<book>.part", resumed across up to three
// attempts, refused when the card cannot hold it, and only put in place once it opens as
// an EPUB. On success the book's state follows a replaced copy, the download index records
// it and the Library is told to rescan. `bookId` is the catalog book, for the index.
HttpDownloader::DownloadError transfer(int64_t bookId, int64_t fileId, const std::string& finalPath,
                                       const HttpDownloader::ProgressCallback& progress,
                                       const HttpDownloader::CancelCallback& shouldCancel, bool* cancelFlag);

enum class Result : uint8_t { Downloaded, AlreadyOnCard, NoEpub, Unreachable, NoSpace, Failed, Cancelled };

// The whole of one download, from the book's details: nothing is fetched when the book is
// already where it belongs (same size), which the index then records. outPath is where it
// is or would have gone.
Result downloadBook(int64_t bookId, std::string& outPath, const HttpDownloader::CancelCallback& shouldCancel);

}  // namespace BookOrbitBookDownload

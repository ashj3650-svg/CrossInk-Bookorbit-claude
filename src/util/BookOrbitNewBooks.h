#pragma once

#include <cstdint>
#include <string>

// The books Download New Books brought in that have not been opened yet, newest first:
// what the BookOrbit Home's "New on your reader" shelf lists. Opening a book takes it off.
//
// Backed by /.crosspoint/bookorbit_new_books.bin (see docs/file-formats.md): a few fixed
// records, read and rewritten whole. No copy stays in RAM.
namespace BookOrbitNewBooks {

constexpr uint8_t MAX_BOOKS = 8;

struct Book {
  int32_t id = 0;         // the catalog book
  uint32_t pathHash = 0;  // BookOrbitDownloadIndex::pathHash of where it landed
  char title[46] = {};
};

// Puts a downloaded book first; the oldest falls off past MAX_BOOKS.
void add(int64_t bookId, const std::string& path, const std::string& title);

// Takes the book at this path off the list, when it is on it. Cheap when the list is
// empty or absent: the reader calls it each time a book opens.
void markOpened(const std::string& path);

// The list, newest first. Returns how many books `out` received.
uint8_t load(Book (&out)[MAX_BOOKS]);

}  // namespace BookOrbitNewBooks

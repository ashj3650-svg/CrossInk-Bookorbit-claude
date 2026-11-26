#pragma once

#include <cstdint>
#include <string>

// The newest books Download Missing Books brought in, newest first, opened or not: what the
// BookOrbit Home's "Just downloaded" shelf lists. A book stays until newer downloads push
// it off the end, so the shelf always shows what the last syncs put on the card.
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

// The list, newest first. Returns how many books `out` received.
uint8_t load(Book (&out)[MAX_BOOKS]);

}  // namespace BookOrbitNewBooks

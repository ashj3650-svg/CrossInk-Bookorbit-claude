#pragma once

#include <cstdint>
#include <string>

/**
 * Where the catalog is entered from outside it: one book to download, or a root section to open.
 * The BookOrbit Home sends the first two; a sync that finds the open book's copy out of date
 * sends a book with `replacePath`, the copy to download over, and the reader reopens it after.
 * The End-of-Book screen sends a `series`: the catalog downloads and opens the book numbered
 * next after `seriesAfter` in it.
 */
struct BookOrbitCatalogLink {
  int64_t bookId = 0;
  std::string sectionId;
  std::string title;
  std::string replacePath;
  std::string series;
  float seriesAfter = 0;

  bool empty() const { return bookId == 0 && sectionId.empty() && series.empty(); }
};

#pragma once

// Series lookups over the index's Series order, for the End-of-Book "next in series" row.
//
// The Series order sorts by the folded series name, then the number, with books that have
// no series last (LibraryBuilder's loadMetadataSegment). So a series' books sit together,
// in number order, and the book after one of them is found with a binary search: a dozen
// row reads on a large library, nothing held in RAM.

#include <cstdint>
#include <string>

#include "LibraryIndexFile.h"

namespace library {

// A series number as the index stores it ("2", "2.5", " 3 "): float bits ordered as the
// numbers are. CLIX_UNKNOWN_SERIES_POSITION when the text is not one finite number.
uint32_t parseSeriesPosition(const std::string& text);
// The number a stored position stands for. Not meaningful for CLIX_UNKNOWN_SERIES_POSITION.
float seriesPositionValue(uint32_t position);

// The series and number the index lists for this book. False when the book is not in the
// index; an empty series when the index has none for it.
bool findSeriesOf(LibraryIndexFile& index, const BookIdentity& book, std::string& series, uint32_t& position);

struct SeriesBook {
  std::string path;
  std::string title;
  uint32_t position = CLIX_UNKNOWN_SERIES_POSITION;
};

// The first book after number `position` of `series` in the Series order: same series,
// a higher number. Copies of the same number, and the book at `excludePath`, are passed
// over. False when the series has no later numbered book on the card, or on a read error.
bool findNextInSeries(LibraryIndexFile& index, const std::string& series, uint32_t position,
                      const std::string& excludePath, SeriesBook& out);

}  // namespace library

#include "LibrarySeries.h"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>

#include "LibraryText.h"

namespace library {

namespace {
constexpr uint16_t NO_ROW = 0xFFFF;
// Rows looked at past the search's landing point: a few copies of one book at most.
constexpr int MAX_SCAN_ROWS = 4;

// One row's sort key: the folded series name, then the number. An empty name is a book
// with no series, which the order puts after every series.
struct SeriesKey {
  std::string folded;
  uint32_t position = CLIX_UNKNOWN_SERIES_POSITION;
};

int compareKeys(const SeriesKey& a, const SeriesKey& b) {
  if (a.folded.empty() || b.folded.empty()) return static_cast<int>(a.folded.empty()) - b.folded.empty();
  // The builder appends a NUL and the big-endian number to the fold, then compares bytes:
  // a name that is a prefix of another sorts first, as std::string's compare has it.
  const int byName = a.folded.compare(b.folded);
  if (byName != 0) return byName;
  return a.position < b.position ? -1 : a.position > b.position ? 1 : 0;
}

bool readRow(LibraryIndexFile& index, const uint16_t row, ClixRecord& record, SeriesKey& key, std::string& name) {
  const uint16_t ordinal = index.ordinalForRow(SortOrder::SeriesAsc, row);
  if (ordinal == NO_ROW || !index.readRecord(ordinal, record) || !index.readSeries(record, name) ||
      !index.readSeriesPosition(record, key.position)) {
    return false;
  }
  foldInto(name, key.folded);
  return true;
}
}  // namespace

uint32_t parseSeriesPosition(const std::string& text) {
  static_assert(std::numeric_limits<float>::is_iec559, "series positions require IEEE-754 floats");
  static_assert(sizeof(uint32_t) == sizeof(float), "series position needs a 32-bit float");
  if (text.empty()) return CLIX_UNKNOWN_SERIES_POSITION;
  char* end = nullptr;
  const float value = std::strtof(text.c_str(), &end);
  const bool parsed = end != text.c_str();
  while (std::isspace(static_cast<unsigned char>(*end))) ++end;
  if (!parsed || *end != '\0' || !std::isfinite(value)) return CLIX_UNKNOWN_SERIES_POSITION;
  // Flip the sign half of IEEE-754 so u32 order matches signed numeric order.
  const float normalized = value == 0 ? 0 : value;  // -0 and +0 have the same order.
  uint32_t bits = 0;
  memcpy(&bits, &normalized, sizeof(bits));
  return bits & 0x80000000u ? ~bits : bits ^ 0x80000000u;
}

float seriesPositionValue(uint32_t position) {
  position = position & 0x80000000u ? position ^ 0x80000000u : ~position;
  float value = 0;
  memcpy(&value, &position, sizeof(value));
  return value;
}

bool findSeriesOf(LibraryIndexFile& index, const BookIdentity& book, std::string& series, uint32_t& position) {
  uint16_t row = NO_ROW;
  if (!index.recentRowsFor(&book, 1, &row) || row == NO_ROW) return false;
  ClixRecord record{};
  return index.readRecord(index.ordinalForRow(SortOrder::RecentAsc, row), record) && index.readSeries(record, series) &&
         index.readSeriesPosition(record, position);
}

bool findNextInSeries(LibraryIndexFile& index, const std::string& series, const uint32_t position,
                      const std::string& excludePath, SeriesBook& out) {
  SeriesKey target;
  foldInto(series, target.folded);
  target.position = position;
  if (target.folded.empty() || position == CLIX_UNKNOWN_SERIES_POSITION || index.ranksDegraded()) return false;

  // Upper bound: the first row ordering after (series, position).
  ClixRecord record{};
  SeriesKey key;
  std::string name;
  uint16_t low = 0;
  uint16_t high = index.bookCount();
  while (low < high) {
    const uint16_t mid = low + (high - low) / 2;
    if (!readRow(index, mid, record, key, name)) return false;
    if (compareKeys(key, target) <= 0) {
      low = mid + 1;
    } else {
      high = mid;
    }
  }

  for (int scanned = 0; low < index.bookCount() && scanned < MAX_SCAN_ROWS; low++, scanned++) {
    // Another series, or this one's unnumbered books (which sort last in it): no next.
    if (!readRow(index, low, record, key, name) || key.folded != target.folded ||
        key.position == CLIX_UNKNOWN_SERIES_POSITION) {
      return false;
    }
    std::string author;
    if (!index.readPath(record, out.path) || out.path == excludePath) continue;
    if (!index.readDisplayText(record, out.title, author)) return false;
    out.position = key.position;
    return true;
  }
  return false;
}

}  // namespace library

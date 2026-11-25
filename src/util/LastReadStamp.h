#pragma once

#include <cstdint>
#include <string>

// When a book was last read on this device: one small file in the book's content-keyed
// state directory (book_<hash>/last_read.bin, see docs/file-formats.md), so it follows
// the book through moves and renames. Written once per reading session, when the reader
// closes, never per page turn. The Previously card reads it to tell how long the reader
// was away from a book.
namespace LastReadStamp {

// Stamps now as the time the book was last read. Skipped (false) when the wall clock is
// not plausible: a wrong date is worse than none.
bool save(const std::string& bookPath);

// When the book was last read, in UTC epoch seconds. False when it never was, or the
// stamp is damaged.
bool load(const std::string& bookPath, uint32_t& epochSeconds);

// Whole days from `thenEpoch` to `nowEpoch`; 0 when now is not after then (a clock that
// went back reads as no time away, never as a negative gap).
constexpr uint32_t daysBetween(const uint32_t thenEpoch, const uint32_t nowEpoch) {
  return nowEpoch > thenEpoch ? (nowEpoch - thenEpoch) / 86400u : 0u;
}

}  // namespace LastReadStamp

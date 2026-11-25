#pragma once

#include <cstdint>
#include <string>

// When a book's reading position last moved on this device, for "the most recently saved
// position wins" when BookOrbit sync compares it with the server's. Kept apart from the
// progress file so stock CrossInk keeps reading that one, and written only when the
// position moves: reopening a book or a relayout is not a newer save.
//
// File: book_<hash>/progress_time.bin (docs/file-formats.md).
namespace ProgressSaveTime {

// Records now as the moment this book's position moved. Skipped while the clock is not
// trusted, so an unset clock never makes an old position look new.
void record(const std::string& bookPath);

// The last recorded move in real time: corrected for the clock drift NTP measured since,
// when it was measured. False when nothing was recorded.
bool load(const std::string& bookPath, int64_t& outEpochSeconds);

}  // namespace ProgressSaveTime

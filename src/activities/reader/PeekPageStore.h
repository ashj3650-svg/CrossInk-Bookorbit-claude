#pragma once

#include <cstdint>
#include <string>

// A book's Peek page: one pinned place (a map, a cast list, a glossary) the Peek
// shortcut jumps to and back from. Kept in the book's content-keyed state directory
// (book_<hash>/peek.bin, see docs/file-formats.md) so it follows the book through
// moves and renames. The place is a chapter and the fraction of it the page starts at,
// like a bookmark, plus the paragraph when known, so it survives a change of font.
namespace PeekPageStore {

struct Place {
  uint16_t spineIndex = 0;
  uint16_t paragraphIndex = UINT16_MAX;  // UINT16_MAX = unknown
  float progress = 0.0f;                 // 0.0-1.0 within the chapter
};

bool load(const std::string& bookPath, Place& place);
bool save(const std::string& bookPath, const Place& place);
bool remove(const std::string& bookPath);

}  // namespace PeekPageStore

#pragma once

#include <string>

class GfxRenderer;

/**
 * Work done ahead of time, while nobody waits on it (the sync behind the sleep screen), so
 * the screens that would otherwise do it open at once: a newly downloaded book's index and
 * Home cover, the BookOrbit Home's cover thumbnails and the Library index.
 *
 * Everything lands in the caches those screens already read (`.crosspoint/epub_<hash>/`,
 * `library.idx`); nothing stays in RAM once a call returns. Each step is skipped when its
 * result already exists, so a sleep with nothing new costs a few file checks. `stopped` is
 * polled between books (and inside the Library build): a wake press ends the work, and the
 * screen that needs it does it as before.
 */
namespace BookPrep {

using StopCheck = bool (*)();

/**
 * Builds an EPUB's book index (book.bin, the spine and table of contents) and its CSS rules
 * cache, the first work opening the book does, then the cover thumbnail the BookOrbit Home
 * shows for the book being read. False when the book cannot be read.
 */
bool prepareEpub(const std::string& path, GfxRenderer& renderer);

/**
 * Generates the BookOrbit Home's cover thumbnails for the recent books it lists, in the
 * sizes it draws them, as Home would behind a Loading popup on its next visit.
 */
void prepareHomeCovers(GfxRenderer& renderer, StopCheck stopped);

/**
 * Rebuilds the Library index when the card changed since it was built (books downloaded,
 * files transferred), as the Library would on its next visit.
 */
void refreshLibraryIndex(StopCheck stopped);

/**
 * Generates one BookOrbit Home thumbnail (EPUB or XTC) at `width` x `height` when it is not
 * on the card yet. False when the book has no usable cover; `noCoverImage` then says whether
 * the book simply has none (so Home can stop asking).
 */
bool generateHomeThumb(const std::string& path, int width, int height, GfxRenderer& renderer,
                       bool* noCoverImage = nullptr);

}  // namespace BookPrep

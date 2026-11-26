#include "BookPrep.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <LibraryBuilder.h>
#include <Logging.h>
#include <Xtc.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "components/themes/bookorbit/BookOrbitHomeLayout.h"

namespace {

constexpr char CACHE_ROOT[] = "/.crosspoint";

BookPrep::StopCheck libraryStopCheck = nullptr;

bool libraryBuildCancelled(void*) { return libraryStopCheck != nullptr && libraryStopCheck(); }

// The BookOrbit Home's cover slot for the book at `index` (0 = the book being read).
void homeCoverSize(const GfxRenderer& renderer, const int index, int& width, int& height) {
  const auto slot =
      BookOrbitHomeLayout::coverSlot(BookOrbitHomeLayout::grid(renderer), SETTINGS.shouldTrackReadingStats(), index);
  width = slot.width;
  height = slot.height;
}

}  // namespace

namespace BookPrep {

bool generateHomeThumb(const std::string& path, const int width, const int height, GfxRenderer& renderer,
                       bool* noCoverImage) {
  if (noCoverImage) *noCoverImage = false;
  if (width <= 0 || height <= 0) return false;
  if (FsHelpers::hasEpubExtension(path)) {
    Epub epub(path, CACHE_ROOT);
    const std::string thumbPath = epub.getAdaptiveThumbBmpPath(width, height);
    if (!thumbPath.empty() && Storage.exists(thumbPath.c_str())) return true;
    if (!epub.load(true, true, Epub::XLocationLoadMode::Skip)) {
      LOG_ERR("PREP", "Cannot load EPUB for its thumbnail: %s", path.c_str());
      return false;
    }
    if (epub.generateAdaptiveThumbBmp(width, height, &renderer, SETTINGS.getReaderFontId())) return true;
    if (noCoverImage) *noCoverImage = !epub.hasCoverImage();
    return false;
  }
  if (FsHelpers::hasXtcExtension(path)) {
    Xtc xtc(path, CACHE_ROOT);
    const std::string thumbPath = xtc.getThumbBmpPath(static_cast<uint16_t>(width), static_cast<uint16_t>(height));
    if (!thumbPath.empty() && Storage.exists(thumbPath.c_str())) return true;
    return xtc.load() && xtc.generateThumbBmp(static_cast<uint16_t>(width), static_cast<uint16_t>(height));
  }
  return false;
}

bool prepareEpub(const std::string& path, GfxRenderer& renderer) {
  if (!FsHelpers::hasEpubExtension(path)) return false;
  const unsigned long start = millis();
  {
    // Builds book.bin and the CSS rules cache when missing; the reader then only loads them.
    Epub epub(path, CACHE_ROOT);
    if (!epub.load(true, false, Epub::XLocationLoadMode::Skip)) {
      LOG_ERR("PREP", "Cannot index %s", path.c_str());
      return false;
    }
  }
  // Opening the book makes it the Home's current book: its cover then shows at once.
  int width = 0;
  int height = 0;
  homeCoverSize(renderer, 0, width, height);
  generateHomeThumb(path, width, height, renderer);
  LOG_INF("PREP", "Prepared %s in %lu ms", path.c_str(), millis() - start);
  return true;
}

void prepareHomeCovers(GfxRenderer& renderer, StopCheck stopped) {
  // As HomeActivity::loadRecentBooks lists them: the first books still on the card.
  const auto& books = RECENT_BOOKS.getBooks();
  int index = 0;
  for (const RecentBook& book : books) {
    if (index >= BookOrbitHomeLayout::kMaxBooks || (stopped && stopped())) break;
    if (RecentBooksStore::isMissing(book)) continue;
    const int slot = index++;
    if (book.coverState == RecentBook::CoverState::Missing) continue;
    int width = 0;
    int height = 0;
    homeCoverSize(renderer, slot, width, height);
    bool noCoverImage = false;
    if (!generateHomeThumb(book.path, width, height, renderer, &noCoverImage) && noCoverImage) {
      // What Home records on the same outcome, so neither screen tries again.
      if (!RECENT_BOOKS.updateBook(book.path, book.title, book.author, "", RecentBook::CoverState::Missing)) {
        LOG_ERR("PREP", "Cannot record the missing cover of %s", book.path.c_str());
      }
      // updateBook may have rewritten the list: stop rather than walk a changed vector.
      break;
    }
  }
}

void refreshLibraryIndex(StopCheck stopped) {
  if (!library::libraryIndexNeedsRefresh()) return;
  const unsigned long start = millis();
  library::BuildStats stats;
  library::BuildCallbacks callbacks;
  libraryStopCheck = stopped;
  callbacks.cancelRequested = libraryBuildCancelled;
  const bool built = library::buildLibraryIndex("/", stats, SETTINGS.libraryUseMetadata != 0, &callbacks);
  libraryStopCheck = nullptr;
  LOG_INF("PREP", "Library index %s (%u books, %lu ms)", built ? "rebuilt" : "left for the Library",
          static_cast<unsigned>(stats.books), millis() - start);
}

}  // namespace BookPrep

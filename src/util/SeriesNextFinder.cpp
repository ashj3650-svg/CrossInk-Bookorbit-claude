#include "SeriesNextFinder.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <LibraryBuilder.h>
#include <LibraryFormat.h>
#include <LibraryIndexFile.h>
#include <LibrarySeries.h>
#include <Logging.h>
#include <Memory.h>

namespace {
// Fallback for a book the index does not list yet (or lists with no metadata): its OPF,
// read as the Library would.
bool seriesFromOpf(const std::string& path, std::string& series, uint32_t& position) {
  auto epub = makeUniqueNoThrow<Epub>(path, "/.crosspoint");  // heap: an Epub is too big for the stack
  if (!epub) {
    LOG_ERR("SNF", "OOM: Epub (%u bytes)", static_cast<unsigned>(sizeof(Epub)));
    return false;
  }
  std::string title;
  std::string author;
  std::string number;
  if (!epub->loadMetadata(title, author, /*allowCachedMetadata=*/true, &series, nullptr, &number)) return false;
  position = library::parseSeriesPosition(number);
  return true;
}
}  // namespace

bool SeriesNextFinder::find(const std::string& bookPath, Result& out) {
  out = Result{};
  // Only EPUBs carry series metadata; the index has none for other formats either.
  if (!FsHelpers::hasEpubExtension(bookPath)) return false;

  library::LibraryIndexFile index;
  const bool indexed = Storage.exists(library::libraryIndexPath()) && index.open(library::libraryIndexPath());
  library::BookIdentity identity{library::clixPathHash(bookPath.data(), bookPath.size()), 0};
  // The size spares the lookup a hash read per book; without it the hash alone decides.
  if (auto file = Storage.open(bookPath.c_str())) {
    identity.fileSize = static_cast<uint32_t>(file.fileSize());
    file.close();
  }

  uint32_t position = library::CLIX_UNKNOWN_SERIES_POSITION;
  const bool listed = indexed && library::findSeriesOf(index, identity, out.series, position) && !out.series.empty();
  if (!listed && !seriesFromOpf(bookPath, out.series, position)) out.series.clear();
  if (out.series.empty() || position == library::CLIX_UNKNOWN_SERIES_POSITION) {
    out.series.clear();
    return false;
  }
  out.position = library::seriesPositionValue(position);

  library::SeriesBook next;
  if (indexed && library::findNextInSeries(index, out.series, position, bookPath, next)) {
    out.nextPath = std::move(next.path);
    out.nextTitle = std::move(next.title);
    out.nextPosition = library::seriesPositionValue(next.position);
  }
  LOG_DBG("SNF", "%s #%g: next on card %s", out.series.c_str(), static_cast<double>(out.position),
          out.nextPath.empty() ? "(none)" : out.nextPath.c_str());
  return true;
}

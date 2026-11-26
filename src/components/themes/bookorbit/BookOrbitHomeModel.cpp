#include "BookOrbitHomeModel.h"

#include <Arduino.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <LibraryBuilder.h>
#include <LibraryIndexFile.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstdio>

#include "BookOrbitCredentialStore.h"
#include "BookOrbitDownloadIndex.h"
#include "activities/home/RecentBookProgress.h"
#include "util/BookOrbitNewBooks.h"
#include "util/CardSpace.h"

namespace {
uint8_t onCardMask(const BookOrbitHomeBook* books, const int count) {
  std::string path;
  uint8_t mask = 0;
  for (int i = 0; i < count; i++) {
    if (BookOrbitDownloadIndex::lookup(books[i].id, path)) mask |= static_cast<uint8_t>(1u << i);
  }
  return mask;
}

// A Just downloaded shelf lists the card's download list as it is now, not as of the last
// sync: a shelf that only became Just downloaded (Recently added before this firmware) shows
// it at once, and a book deleted from the card since leaves it. Covers come with the next
// sync; until then a book shows a blank cover with its title.
void fillJustDownloaded(BookOrbitHomeSnapshot& snapshot) {
  // 448 bytes of stack, once per Home load, on the main loop.
  BookOrbitNewBooks::Book downloads[BookOrbitNewBooks::MAX_BOOKS];
  int downloadCount = -1;  // read once, on the first such shelf
  std::string path;
  for (int s = 0; s < BookOrbitHomeSnapshot::SHELVES; s++) {
    const BookOrbitHomeShelfSource& chosen = BOOKORBIT_STORE.getHomeShelf(s);
    if (chosen.kind != BookOrbitHomeShelfKind::NEW_ON_READER) continue;
    if (downloadCount < 0) downloadCount = BookOrbitNewBooks::load(downloads);
    BookOrbitHomeShelf& shelf = snapshot.shelves[s];
    shelf.source = chosen;
    shelf.count = 0;
    for (int i = 0; i < downloadCount && shelf.count < BookOrbitHomeShelf::BOOK_COUNT; i++) {
      if (!BookOrbitDownloadIndex::lookup(downloads[i].id, path)) continue;
      BookOrbitHomeBook& out = shelf.books[shelf.count++];
      out = BookOrbitHomeBook{};
      out.id = downloads[i].id;
      out.hasCover = true;
      snprintf(out.title, sizeof(out.title), "%s", downloads[i].title);
    }
  }
}

// Counting the used space froze Home at every start on slow FAT32 cards (#110), so only
// exFAT cards get a storage bar (see CardSpace). The figures are counted once and kept
// across sleep until books change (CardSpace::usage), so a wake does not count again.
void readCardSpace(uint64_t& total, uint64_t& used) { CardSpace::usage(total, used); }
}  // namespace

void BookOrbitHomeModel::load(const BookOrbitHomeSource& source, const bool trackedStats) {
  data = source;
  showStats = trackedStats;
  hasAccount = BOOKORBIT_STORE.hasCredentials();
  hasToday = getCurrentLocalReadingStatsDateTimeAnyClock(today);

  snapshot.reset();
  if (hasAccount) {
    snapshot = makeUniqueNoThrow<BookOrbitHomeSnapshot>();
    if (snapshot && !BookOrbitHomeStore::load(*snapshot)) snapshot.reset();
    if (snapshot) fillJustDownloaded(*snapshot);
  }

  // Only the index header is read: the count as of the Library's last scan, no card walk.
  cardBookCount = -1;
  if (snapshot && Storage.exists(library::libraryIndexPath())) {
    library::LibraryIndexFile index;
    if (index.open(library::libraryIndexPath())) cardBookCount = index.bookCount();
  }
  if (snapshot) readCardSpace(cardTotalBytes, cardUsedBytes);

  bookProgress.fill(-1.0f);
  if (data.books != nullptr) {
    const size_t count = std::min<size_t>(data.books->size(), bookProgress.size());
    for (size_t i = 0; i < count; i++) {
      const RecentBook& book = (*data.books)[i];
      if (i == 0) {
        bookProgress[i] = data.currentBookProgress;
      } else {
        bookProgress[i] = FsHelpers::hasEpubExtension(book.path) ? RecentBookProgress::loadCachedEpubPercent(book)
                                                                 : RecentBookProgress::loadPercent(book);
      }
    }
  }
  computeRows();

  reading.compute(data, snapshot.get(), showStats, hasToday, today);
}

void BookOrbitHomeModel::computeRows() {
  rowCount = 0;
  rows.fill(Row{});
  const int localBooks = data.books != nullptr ? static_cast<int>(data.books->size()) : 0;
  for (int i = 1; i < localBooks && rowCount < static_cast<int>(rows.size()); i++) {
    Row& row = rows[static_cast<size_t>(rowCount++)];
    row.local = &(*data.books)[static_cast<size_t>(i)];
    row.progress = bookProgress[static_cast<size_t>(i)];
    row.onCard = true;
  }

  shelfOnCard.fill(0);
  if (!snapshot) return;

  const auto isRecentBook = [this, localBooks](const char* title, const std::string& path) {
    for (int i = 0; i < localBooks; i++) {
      const RecentBook& book = (*data.books)[static_cast<size_t>(i)];
      if (book.title == title || (!path.empty() && book.path == path)) return true;
    }
    return false;
  };
  std::string path;
  for (int i = 0; i < snapshot->continueCount && rowCount < static_cast<int>(rows.size()); i++) {
    const BookOrbitHomeBook& book = snapshot->continueReading[i];
    const bool onCard = BookOrbitDownloadIndex::lookup(book.id, path);
    if (isRecentBook(book.title, onCard ? path : std::string{})) continue;
    Row& row = rows[static_cast<size_t>(rowCount++)];
    row.remote = &book;
    row.remoteArg = i;
    row.progress = book.progress != BookOrbitHomeBook::NO_PROGRESS ? static_cast<float>(book.progress) : -1.0f;
    row.onCard = onCard;
  }
  for (int i = 0; i < BookOrbitHomeSnapshot::SHELVES; i++) {
    shelfOnCard[i] = onCardMask(snapshot->shelves[i].books, snapshot->shelves[i].count);
  }
  serverBookCount = static_cast<int32_t>(BookOrbitDownloadIndex::recordedCount());
  BookOrbitDownloadIndex::unload();
}

const BookOrbitHomeBook* BookOrbitHomeModel::catalogBook(const int arg) const {
  if (!snapshot || arg < 0) return nullptr;
  if (arg < kShelfArgBase) return &snapshot->continueReading[arg];
  const int shelf = (arg - kShelfArgBase) / BookOrbitHomeShelf::BOOK_COUNT;
  if (shelf >= BookOrbitHomeSnapshot::SHELVES) return nullptr;
  return &snapshot->shelves[shelf].books[(arg - kShelfArgBase) % BookOrbitHomeShelf::BOOK_COUNT];
}

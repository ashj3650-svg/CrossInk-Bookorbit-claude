#include "BookOrbitCatalogRows.h"

#include <I18n.h>

#include <utility>

#include "components/HeaderDate.h"

namespace BookOrbitCatalogRows {
namespace {
using Entry = BookOrbitCatalogEntry;

// Appends a row's " (count)" badge, remembering where the title itself ends.
void addCountBadge(Entry& entry, const int count) {
  entry.badgeAt = static_cast<uint16_t>(entry.title.size());
  entry.title += " (" + std::to_string(count) + ")";
}

// The root section's entry count, the way BookOrbit's own plugin badges its Browse tiles:
// -1 for a section it has none for.
int sectionCount(const BookOrbitCatalogCounts& counts, const std::string& id) {
  if (id == "all-books") return counts.totalBooks;
  if (id == "continue-reading") return counts.inProgress;
  if (id == "authors") return counts.authors;
  if (id == "series") return counts.series;
  return -1;
}

}  // namespace

bool isFacetSection(const std::string& sectionId) { return sectionId == "authors" || sectionId == "series"; }

std::string titleWithoutBadge(const Entry& entry) {
  return entry.badgeAt > 0 ? entry.title.substr(0, entry.badgeAt) : entry.title;
}

void appendRoot(std::vector<Entry>& rows, const std::vector<BookOrbitCatalogSection>& sections,
                const BookOrbitCatalogCounts& counts) {
  for (const auto& section : sections) {
    Entry entry;
    entry.type = isFacetSection(section.id) ? Entry::Type::FACET_SECTION : Entry::Type::SECTION;
    entry.title = section.title;
    const int count = sectionCount(counts, section.id);
    if (count >= 0) addCountBadge(entry, count);
    entry.sectionId = section.id;
    rows.push_back(std::move(entry));
  }
  // The want-to-read shelf BookOrbit's own plugin shows, which the server does not list
  // among its sections. After them, so the rows above keep their places.
  if (counts.wantToRead >= 0) {
    Entry wantToRead;
    wantToRead.type = Entry::Type::SECTION;
    wantToRead.title = tr(STR_BOOKORBIT_WANT_TO_READ);
    addCountBadge(wantToRead, counts.wantToRead);
    wantToRead.sectionId = BookOrbitCatalogClient::WANT_TO_READ_SHELF;
    rows.push_back(std::move(wantToRead));
  }
}

void setFacet(Entry& entry, BookOrbitFacetEntry&& facet) {
  entry.type = Entry::Type::FACET;
  entry.title = std::move(facet.title);
  if (facet.count > 0) addCountBadge(entry, facet.count);
  entry.sectionId = std::move(facet.id);
  entry.seriesId = std::move(facet.seriesId);
}

void setBook(Entry& entry, BookOrbitCatalogBook&& book, std::string&& devicePath) {
  entry.type = Entry::Type::BOOK;
  entry.bookId = book.id;
  entry.onDevice = !devicePath.empty();
  if (entry.onDevice) {
    entry.path = std::move(devicePath);
  } else {
    std::string().swap(entry.path);  // a reused row: release its old path too
  }
  entry.title = std::move(book.title);
  entry.subtitle = std::move(book.author);
  // The list section the sort files the book under. The day is formatted once, here:
  // short enough to fit the string without a heap block.
  if (!book.series.empty()) {
    entry.sectionId = std::move(book.series);
  } else if (book.addedAt != 0) {
    char day[24];
    if (formatLocalDateText(day, sizeof(day), book.addedAt)) entry.sectionId = day;
  }
}

BookOrbitBookQuery facetBookQuery(const std::string& facetSectionId, const Entry& facet) {
  // Mirror BookOrbit's own plugin (paramsForEntry): author filters by the
  // entry id; series prefers the numeric seriesId and sorts by series order.
  BookOrbitBookQuery query;
  if (facetSectionId == "series") {
    query.sort = "series";
    if (!facet.seriesId.empty()) {
      query.seriesId = facet.seriesId;
    } else {
      query.series = facet.sectionId;
    }
  } else {
    query.author = facet.sectionId;
  }
  return query;
}

}  // namespace BookOrbitCatalogRows

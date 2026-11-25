#pragma once

#include <string>

// The book after this one in its series, for the End-of-Book screen.
//
// The series and number come from the Library index (what the Library's Series order
// shows), or from the book's own OPF when the index does not list it yet (a book just
// downloaded or copied). The next book on the card is the first one after it in the
// index's Series order: same series, higher number.
namespace SeriesNextFinder {

struct Result {
  std::string series;  // empty: the book has no numbered series
  float position = 0;
  // The next book on the card; empty when there is none.
  std::string nextPath;
  std::string nextTitle;
  float nextPosition = 0;
};

// False when the book has no series number (and for anything but an EPUB). A missing or unreadable Library index only
// leaves nextPath empty.
bool find(const std::string& bookPath, Result& out);

}  // namespace SeriesNextFinder

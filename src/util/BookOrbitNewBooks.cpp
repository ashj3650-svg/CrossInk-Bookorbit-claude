#include "BookOrbitNewBooks.h"

#include <HalStorage.h>
#include <Logging.h>

#include <cstring>

#include "BookOrbitDownloadIndex.h"

namespace {

constexpr char PATH[] = "/.crosspoint/bookorbit_new_books.bin";
constexpr uint32_t MAGIC = 0x424E4F42;  // "BONB"
constexpr uint16_t VERSION = 1;

struct Header {
  uint32_t magic = MAGIC;
  uint16_t version = VERSION;
  uint16_t count = 0;
};
static_assert(sizeof(Header) == 8, "bookorbit_new_books.bin starts with 8 bytes");
static_assert(sizeof(BookOrbitNewBooks::Book) == 56, "bookorbit_new_books.bin holds 56-byte records");

bool save(const BookOrbitNewBooks::Book (&books)[BookOrbitNewBooks::MAX_BOOKS], const uint8_t count) {
  if (count == 0) {
    if (Storage.exists(PATH) && !Storage.remove(PATH)) {
      LOG_ERR("BONB", "Cannot remove the new-books list");
      return false;
    }
    return true;
  }
  HalFile file;
  if (!Storage.openFileForWrite("BONB", PATH, file)) {
    LOG_ERR("BONB", "Cannot open the new-books list for writing");
    return false;
  }
  Header header;
  header.count = count;
  const size_t bytes = sizeof(BookOrbitNewBooks::Book) * count;
  const bool ok = file.write(reinterpret_cast<const uint8_t*>(&header), sizeof(header)) == sizeof(header) &&
                  file.write(reinterpret_cast<const uint8_t*>(books), bytes) == bytes;
  file.close();
  if (!ok) LOG_ERR("BONB", "Cannot write the new-books list");
  return ok;
}

}  // namespace

uint8_t BookOrbitNewBooks::load(Book (&out)[MAX_BOOKS]) {
  HalFile file;
  if (!Storage.exists(PATH) || !Storage.openFileForRead("BONB", PATH, file)) return 0;
  Header header;
  uint8_t count = 0;
  if (file.read(&header, sizeof(header)) == static_cast<int>(sizeof(header)) && header.magic == MAGIC &&
      header.version == VERSION && header.count <= MAX_BOOKS) {
    const int bytes = static_cast<int>(sizeof(Book) * header.count);
    if (file.read(out, bytes) == bytes) count = static_cast<uint8_t>(header.count);
  }
  file.close();
  for (uint8_t i = 0; i < count; i++) out[i].title[sizeof(out[i].title) - 1] = '\0';
  return count;
}

void BookOrbitNewBooks::add(const int64_t bookId, const std::string& path, const std::string& title) {
  // 448 bytes of stack, once per downloaded book, on the sleep sync's main task.
  Book books[MAX_BOOKS];
  uint8_t count = load(books);
  const auto id = static_cast<int32_t>(bookId);
  uint8_t kept = 0;
  for (uint8_t i = 0; i < count; i++) {
    if (books[i].id != id) books[kept++] = books[i];
  }
  count = kept < MAX_BOOKS ? kept : MAX_BOOKS - 1;
  memmove(&books[1], &books[0], sizeof(Book) * count);
  books[0] = Book{};
  books[0].id = id;
  books[0].pathHash = BookOrbitDownloadIndex::pathHash(path);
  strncpy(books[0].title, title.c_str(), sizeof(books[0].title) - 1);
  save(books, static_cast<uint8_t>(count + 1));
}

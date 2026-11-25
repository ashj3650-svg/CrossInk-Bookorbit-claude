#include "PeekPageStore.h"

#include <HalStorage.h>
#include <Logging.h>

#include <cmath>
#include <cstring>

#include "util/BookContentId.h"

namespace {
constexpr uint32_t PEEK_MAGIC = 0x424F504B;  // "BOPK"
constexpr size_t PEEK_SIZE = sizeof(uint32_t) + 2 * sizeof(uint16_t) + sizeof(float);
constexpr const char* PEEK_FILE = "/peek.bin";
}  // namespace

bool PeekPageStore::load(const std::string& bookPath, Place& place) {
  // Name-only lookup: reading must not create the state directory.
  const std::string stateDir = BookContentId::bookStateDirName(bookPath);
  if (stateDir.empty()) return false;
  const std::string path = stateDir + PEEK_FILE;
  if (!Storage.exists(path.c_str())) return false;

  FsFile file;
  if (!Storage.openFileForRead("PEEK", path, file)) return false;
  uint8_t data[PEEK_SIZE];
  const int bytesRead = file.read(data, sizeof(data));
  file.close();
  if (bytesRead != static_cast<int>(sizeof(data))) {
    LOG_ERR("PEEK", "Peek page file has unexpected size: %d", bytesRead);
    return false;
  }
  uint32_t magic = 0;
  memcpy(&magic, data, sizeof(magic));
  if (magic != PEEK_MAGIC) {
    LOG_ERR("PEEK", "Peek page file has a bad magic");
    return false;
  }
  Place read;
  memcpy(&read.spineIndex, data + 4, sizeof(read.spineIndex));
  memcpy(&read.paragraphIndex, data + 6, sizeof(read.paragraphIndex));
  memcpy(&read.progress, data + 8, sizeof(read.progress));
  if (!std::isfinite(read.progress) || read.progress < 0.0f || read.progress > 1.0f) {
    LOG_ERR("PEEK", "Peek page file holds an impossible place");
    return false;
  }
  place = read;
  return true;
}

bool PeekPageStore::save(const std::string& bookPath, const Place& place) {
  const std::string stateDir = BookContentId::bookStateDir(bookPath);
  if (stateDir.empty()) return false;
  uint8_t data[PEEK_SIZE];
  memcpy(data, &PEEK_MAGIC, sizeof(PEEK_MAGIC));
  memcpy(data + 4, &place.spineIndex, sizeof(place.spineIndex));
  memcpy(data + 6, &place.paragraphIndex, sizeof(place.paragraphIndex));
  memcpy(data + 8, &place.progress, sizeof(place.progress));

  FsFile file;
  if (!Storage.openFileForWrite("PEEK", stateDir + PEEK_FILE, file)) {
    LOG_ERR("PEEK", "Could not open the Peek page file for %s", bookPath.c_str());
    return false;
  }
  const size_t written = file.write(data, sizeof(data));
  file.close();
  if (written != sizeof(data)) {
    LOG_ERR("PEEK", "Short write of the Peek page file: %u bytes", static_cast<unsigned>(written));
    return false;
  }
  return true;
}

bool PeekPageStore::remove(const std::string& bookPath) {
  const std::string stateDir = BookContentId::bookStateDirName(bookPath);
  if (stateDir.empty()) return true;
  const std::string path = stateDir + PEEK_FILE;
  return !Storage.exists(path.c_str()) || Storage.remove(path.c_str());
}

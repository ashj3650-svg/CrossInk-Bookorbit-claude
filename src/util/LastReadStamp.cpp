#include "LastReadStamp.h"

#include <HalStorage.h>
#include <Logging.h>
#include <WallClock.h>

#include <cstring>

#include "util/BookContentId.h"

namespace {
constexpr uint32_t STAMP_MAGIC = 0x424F4C52;  // "BOLR"
constexpr size_t STAMP_SIZE = 2 * sizeof(uint32_t);
constexpr const char* STAMP_FILE = "/last_read.bin";
}  // namespace

bool LastReadStamp::save(const std::string& bookPath) {
  uint32_t now = 0;
  if (!WallClock::now(now)) {
    LOG_DBG("LRS", "Clock not set; last-read time not recorded");
    return false;
  }
  const std::string stateDir = BookContentId::bookStateDir(bookPath);
  if (stateDir.empty()) return false;

  uint8_t data[STAMP_SIZE];
  memcpy(data, &STAMP_MAGIC, sizeof(STAMP_MAGIC));
  memcpy(data + sizeof(STAMP_MAGIC), &now, sizeof(now));

  FsFile file;
  if (!Storage.openFileForWrite("LRS", stateDir + STAMP_FILE, file)) {
    LOG_ERR("LRS", "Could not open the last-read stamp for %s", bookPath.c_str());
    return false;
  }
  const size_t written = file.write(data, sizeof(data));
  file.close();
  if (written != sizeof(data)) {
    LOG_ERR("LRS", "Short write of the last-read stamp: %u bytes", static_cast<unsigned>(written));
    return false;
  }
  return true;
}

bool LastReadStamp::load(const std::string& bookPath, uint32_t& epochSeconds) {
  // Name-only lookup: reading must not create the state directory.
  const std::string stateDir = BookContentId::bookStateDirName(bookPath);
  if (stateDir.empty()) return false;
  const std::string path = stateDir + STAMP_FILE;
  if (!Storage.exists(path.c_str())) return false;

  FsFile file;
  if (!Storage.openFileForRead("LRS", path, file)) return false;
  uint8_t data[STAMP_SIZE];
  const int bytesRead = file.read(data, sizeof(data));
  file.close();
  if (bytesRead != static_cast<int>(sizeof(data))) {
    LOG_ERR("LRS", "Last-read stamp has unexpected size: %d", bytesRead);
    return false;
  }
  uint32_t magic = 0;
  memcpy(&magic, data, sizeof(magic));
  if (magic != STAMP_MAGIC) {
    LOG_ERR("LRS", "Last-read stamp has a bad magic");
    return false;
  }
  memcpy(&epochSeconds, data + sizeof(magic), sizeof(epochSeconds));
  return epochSeconds >= WallClock::MIN_PLAUSIBLE_EPOCH && epochSeconds < WallClock::MAX_PLAUSIBLE_EPOCH;
}

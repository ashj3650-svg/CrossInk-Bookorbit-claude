#include "ProgressSaveTime.h"

#include <HalStorage.h>
#include <Logging.h>
#include <WallClock.h>

#include <cstring>

#include "util/BookContentId.h"

namespace {
constexpr uint32_t SAVE_TIME_MAGIC = 0x424F5054;  // "BOPT"
// magic, epoch, era (truncated to 16 bits like the stats queue), reserved.
constexpr size_t SAVE_TIME_SIZE = sizeof(uint32_t) * 2 + sizeof(uint16_t) * 2;
constexpr const char* SAVE_TIME_FILE = "/progress_time.bin";
}  // namespace

void ProgressSaveTime::record(const std::string& bookPath) {
  uint32_t now = 0;
  if (!WallClock::now(now)) {
    LOG_DBG("PST", "Clock not set; position save time not recorded");
    return;
  }
  const std::string stateDir = BookContentId::bookStateDir(bookPath);
  if (stateDir.empty()) return;

  const uint16_t era = static_cast<uint16_t>(WallClock::era());
  const uint16_t reserved = 0;
  uint8_t data[SAVE_TIME_SIZE];
  memcpy(data, &SAVE_TIME_MAGIC, sizeof(SAVE_TIME_MAGIC));
  memcpy(data + 4, &now, sizeof(now));
  memcpy(data + 8, &era, sizeof(era));
  memcpy(data + 10, &reserved, sizeof(reserved));

  FsFile file;
  if (!Storage.openFileForWrite("PST", stateDir + SAVE_TIME_FILE, file)) {
    LOG_ERR("PST", "Could not open the position save time for %s", bookPath.c_str());
    return;
  }
  const size_t written = file.write(data, sizeof(data));
  file.close();
  if (written != sizeof(data)) {
    LOG_ERR("PST", "Short write of the position save time: %u bytes", static_cast<unsigned>(written));
  }
}

bool ProgressSaveTime::load(const std::string& bookPath, int64_t& outEpochSeconds) {
  // Name-only lookup: reading must not create the state directory.
  const std::string stateDir = BookContentId::bookStateDirName(bookPath);
  if (stateDir.empty()) return false;
  const std::string path = stateDir + SAVE_TIME_FILE;
  if (!Storage.exists(path.c_str())) return false;

  FsFile file;
  if (!Storage.openFileForRead("PST", path, file)) return false;
  uint8_t data[SAVE_TIME_SIZE];
  const int bytesRead = file.read(data, sizeof(data));
  file.close();
  if (bytesRead != static_cast<int>(sizeof(data))) {
    LOG_ERR("PST", "Position save time has unexpected size: %d", bytesRead);
    return false;
  }
  uint32_t magic = 0;
  uint32_t epoch = 0;
  uint16_t era = 0;
  memcpy(&magic, data, sizeof(magic));
  memcpy(&epoch, data + 4, sizeof(epoch));
  memcpy(&era, data + 8, sizeof(era));
  if (magic != SAVE_TIME_MAGIC || epoch < WallClock::MIN_PLAUSIBLE_EPOCH || epoch > WallClock::MAX_PLAUSIBLE_EPOCH) {
    LOG_ERR("PST", "Position save time is damaged");
    return false;
  }
  int64_t delta = 0;
  if (!WallClock::correctionForEvent(era, epoch, delta)) delta = 0;
  outEpochSeconds = static_cast<int64_t>(epoch) + delta;
  return true;
}

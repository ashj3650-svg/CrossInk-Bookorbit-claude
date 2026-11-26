#include "CardSpace.h"

#include <Arduino.h>
#include <HalStorage.h>
#include <LibraryBuilder.h>
#include <Logging.h>

namespace {
struct Usage {
  uint64_t total = 0;
  uint64_t used = 0;
  uint32_t changes = 0;  // library::libraryChangeCount() when counted
  bool known = false;
};
Usage gUsage;

bool usageCurrent() { return gUsage.known && gUsage.changes == library::libraryChangeCount(); }

// Kept by deep sleep; filled with noise after power loss, hence the magic and check word.
struct SleepToken {
  uint32_t magic;
  uint32_t check;
  uint64_t total;
  uint64_t used;
  static constexpr uint32_t MAGIC = 0x43535043;  // "CSPC"
  uint32_t checkFor() const {
    return ~MAGIC ^ static_cast<uint32_t>(total) ^ static_cast<uint32_t>(total >> 32) ^ static_cast<uint32_t>(used) ^
           static_cast<uint32_t>(used >> 32);
  }
};
RTC_NOINIT_ATTR SleepToken gSleepToken;
}  // namespace

namespace CardSpace {

void usage(uint64_t& total, uint64_t& used) {
  if (!usageCurrent()) {
    gUsage.known = true;
    gUsage.changes = library::libraryChangeCount();
    gUsage.total = 0;
    gUsage.used = 0;
    const unsigned long start = millis();
#ifndef SIMULATOR
    if (isQuickToCount()) {
      gUsage.total = Storage.totalBytes();
      gUsage.used = Storage.usedBytes();
    }
#endif
    LOG_DBG("CSPC", "Card space counted in %lu ms (used=%llu total=%llu)", millis() - start,
            static_cast<unsigned long long>(gUsage.used), static_cast<unsigned long long>(gUsage.total));
  }
  total = gUsage.total;
  used = gUsage.used;
}

void countIfUnknown() {
  uint64_t total = 0;
  uint64_t used = 0;
  usage(total, used);
}

void saveForSleep() {
  // Only real figures are kept: a FAT32 card counts as zeros, and those are free to redo.
  if (!usageCurrent() || gUsage.total == 0) {
    gSleepToken.magic = gSleepToken.check = 0;
    return;
  }
  gSleepToken.total = gUsage.total;
  gSleepToken.used = gUsage.used;
  gSleepToken.magic = SleepToken::MAGIC;
  gSleepToken.check = gSleepToken.checkFor();
}

void restoreAfterSleep(const bool deepSleepWake) {
  if (deepSleepWake && gSleepToken.magic == SleepToken::MAGIC && gSleepToken.check == gSleepToken.checkFor() &&
      gSleepToken.used <= gSleepToken.total) {
    gUsage.total = gSleepToken.total;
    gUsage.used = gSleepToken.used;
    gUsage.changes = library::libraryChangeCount();
    gUsage.known = true;
  }
  gSleepToken.magic = gSleepToken.check = 0;
}

bool isQuickToCount() {
#ifdef SIMULATOR
  // The host storage shim does not expose card capacity.
  return false;
#else
  if (Storage.totalBytes() == 0) return false;
  // SdFat keeps the volume's format to itself, but the two formats record a directory's
  // size differently: FAT always stores 0, exFAT the bytes of the clusters it holds, and
  // an exFAT directory holds at least one. The firmware's state directory exists from
  // the first boot.
  HalFile dir = Storage.open("/.crosspoint");
  const bool exFat = dir && dir.isDirectory() && dir.fileSize64() > 0;
  if (dir) dir.close();
  return exFat;
#endif
}

}  // namespace CardSpace

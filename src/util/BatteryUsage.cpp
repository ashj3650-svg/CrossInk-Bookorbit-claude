#include "BatteryUsage.h"

#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <WallClock.h>

#include <algorithm>
#include <cstdio>

#include "activities/reader/ReadingStatsUtils.h"

namespace BatteryUsage {
namespace {

constexpr char PATH[] = "/.crosspoint/battery_usage.bin";
constexpr uint32_t MAGIC = 0x55425243;  // "CRBU"
constexpr uint16_t VERSION = 1;

struct Header {
  uint32_t magic = MAGIC;
  uint16_t version = VERSION;
  uint16_t size = sizeof(BatteryUsageMath::State);
};
static_assert(sizeof(Header) == 8, "battery_usage.bin starts with 8 bytes");
static_assert(sizeof(BatteryUsageMath::State) == 56, "battery_usage.bin holds a 56-byte state");

// Enough measurement for a figure to mean something with whole-percent readings.
constexpr uint32_t MIN_ASLEEP_SECONDS = 6u * 3600u;
constexpr uint32_t MIN_AWAKE_SECONDS = 30u * 60u;

bool load(BatteryUsageMath::State& out) {
  out = BatteryUsageMath::State{};
  HalFile file;
  if (!Storage.exists(PATH) || !Storage.openFileForRead("BATU", PATH, file)) return false;
  Header header;
  const bool ok = file.read(&header, sizeof(header)) == static_cast<int>(sizeof(header)) && header.magic == MAGIC &&
                  header.version == VERSION && header.size == sizeof(out) &&
                  file.read(&out, sizeof(out)) == static_cast<int>(sizeof(out));
  file.close();
  if (!ok) out = BatteryUsageMath::State{};
  return ok;
}

bool save(const BatteryUsageMath::State& state) {
  HalFile file;
  if (!Storage.openFileForWrite("BATU", PATH, file)) return false;
  const Header header;
  const bool ok = file.write(reinterpret_cast<const uint8_t*>(&header), sizeof(header)) == sizeof(header) &&
                  file.write(reinterpret_cast<const uint8_t*>(&state), sizeof(state)) == sizeof(state);
  file.close();
  if (!ok) LOG_ERR("BATU", "Cannot write the battery-use totals");
  return ok;
}

// "1.2" from 12 tenths.
void formatTenths(const int32_t tenths, char* out, const size_t outSize) {
  snprintf(out, outSize, "%ld.%ld", static_cast<long>(tenths / 10), static_cast<long>(tenths % 10));
}

size_t appendLine(char* out, const size_t outSize, size_t used, const char* line) {
  if (used >= outSize) return used;
  const int written = snprintf(out + used, outSize - used, "%s%s", used > 0 ? "\n" : "", line);
  if (written < 0) return used;
  return std::min(outSize - 1, used + static_cast<size_t>(written));
}

}  // namespace

void record(const Event event, const uint8_t percent, const bool onUsb, const bool count) {
  BatteryUsageMath::State state;
  load(state);
  BatteryUsageMath::Sample sample;
  sample.event = event;
  sample.percent = percent;
  sample.onUsb = onUsb;
  sample.count = count;
  sample.era = WallClock::era();
  WallClock::now(sample.epoch);
  BatteryUsageMath::apply(state, sample);
  LOG_DBG("BATU", "Event %d at %u%%%s: asleep %lus/%lu, awake %lus/%lu, sync %lus/%lu", static_cast<int>(event),
          static_cast<unsigned>(percent), onUsb ? " (USB)" : "", static_cast<unsigned long>(state.asleep.seconds),
          static_cast<unsigned long>(state.asleep.usedTenths), static_cast<unsigned long>(state.awake.seconds),
          static_cast<unsigned long>(state.awake.usedTenths), static_cast<unsigned long>(state.sync.seconds),
          static_cast<unsigned long>(state.sync.usedTenths));
  save(state);
}

void formatReport(char* out, const size_t outSize) {
  if (outSize == 0) return;
  out[0] = '\0';
  BatteryUsageMath::State state;
  load(state);
  size_t used = 0;
  char line[96];
  char rate[12];
  char span[16];

  const int32_t asleep = BatteryUsageMath::rateTenths(state.asleep, 86400u, MIN_ASLEEP_SECONDS);
  const int32_t awake = BatteryUsageMath::rateTenths(state.awake, 3600u, MIN_AWAKE_SECONDS);
  if (asleep < 0 && awake < 0 && state.sync.count == 0) {
    appendLine(out, outSize, used, tr(STR_BATTERY_USE_NOT_YET));
    return;
  }
  if (asleep >= 0) {
    formatTenths(asleep, rate, sizeof(rate));
    formatCompactReadingDuration(state.asleep.seconds, span, sizeof(span));
    snprintf(line, sizeof(line), tr(STR_BATTERY_USE_ASLEEP_FORMAT), rate, span);
    used = appendLine(out, outSize, used, line);
  }
  if (awake >= 0) {
    formatTenths(awake, rate, sizeof(rate));
    formatCompactReadingDuration(state.awake.seconds, span, sizeof(span));
    snprintf(line, sizeof(line), tr(STR_BATTERY_USE_AWAKE_FORMAT), rate, span);
    used = appendLine(out, outSize, used, line);
  }
  if (state.sync.count > 0) {
    formatTenths(static_cast<int32_t>(state.sync.usedTenths / state.sync.count), rate, sizeof(rate));
    snprintf(line, sizeof(line), tr(STR_BATTERY_USE_SYNC_FORMAT), rate, static_cast<unsigned>(state.sync.count));
    used = appendLine(out, outSize, used, line);
  }
  if (state.lastSleepSeconds > 0) {
    formatCompactReadingDuration(state.lastSleepSeconds, span, sizeof(span));
    formatTenths(static_cast<int32_t>(state.lastSleepUsedTenths), rate, sizeof(rate));
    snprintf(line, sizeof(line), tr(STR_BATTERY_USE_LAST_SLEEP_FORMAT), span, rate);
    used = appendLine(out, outSize, used, line);
  }
  appendLine(out, outSize, used, tr(STR_BATTERY_USE_NOTE));
}

}  // namespace BatteryUsage

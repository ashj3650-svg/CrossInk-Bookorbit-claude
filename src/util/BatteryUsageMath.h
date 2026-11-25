#pragma once

#include <cstdint>

// The arithmetic behind Settings > System > Device > Battery Use, apart from its file
// so the native tests can drive it (see BatteryUsage.h for the file and the hooks).
//
// The device can only read its battery when it is awake, so the battery is read at the
// edges: going to sleep, waking, and the start of a sleep sync. Each stretch between two
// readings lands in one bucket by the events at its ends. The gauge reports whole
// percents, but each stretch starts at the reading the previous one ended on, so the
// rounding does not pile up: over many stretches the totals are as good as the first
// and last readings.
namespace BatteryUsageMath {

enum class Event : uint8_t {
  None = 0,  // no usable reading yet (or the clock could not be trusted)
  Wake = 1,
  Sleep = 2,
  SyncStart = 3,  // the BookOrbit sleep sync's boot, behind the sleep screen
};

struct Bucket {
  uint32_t seconds = 0;
  uint32_t usedTenths = 0;  // tenths of a percent, so halving keeps a decimal
  uint32_t count = 0;       // stretches counted
};

struct State {
  uint8_t lastEvent = static_cast<uint8_t>(Event::None);
  uint8_t lastPercent = 0;
  uint16_t reserved = 0;
  uint32_t lastEpoch = 0;
  uint32_t lastEra = 0;  // WallClock era: a different one means the clock was lost in between
  Bucket awake;
  Bucket asleep;
  Bucket sync;
  uint32_t lastSleepSeconds = 0;  // 0: no sleep measured yet
  uint32_t lastSleepUsedTenths = 0;
};

struct Sample {
  Event event = Event::None;
  uint8_t percent = 0;
  bool onUsb = false;  // charging, or at least not running on the battery alone
  uint32_t epoch = 0;
  uint32_t era = 0;
  bool count = true;  // false: a fresh start (cold boot), nothing before it belongs to it
};

constexpr uint32_t MIN_PLAUSIBLE_EPOCH = 1577836800u;  // 2020-01-01
constexpr uint32_t MAX_STRETCH_SECONDS = 30u * 24u * 3600u;
// Past this much measured time every bucket is halved, so the figures follow the last
// couple of weeks rather than the device's whole life.
constexpr uint32_t WINDOW_SECONDS = 14u * 24u * 3600u;

inline Bucket* bucketFor(State& state, const Event from, const Event to) {
  if (from == Event::Wake && (to == Event::Sleep || to == Event::SyncStart)) return &state.awake;
  if (from == Event::Sleep && to == Event::Wake) return &state.asleep;
  if (from == Event::SyncStart && (to == Event::Sleep || to == Event::Wake)) return &state.sync;
  return nullptr;
}

inline void halve(Bucket& bucket) {
  bucket.seconds /= 2;
  bucket.usedTenths /= 2;
  bucket.count /= 2;
}

// Counts the stretch that ends at this sample, when it can be trusted, and makes the
// sample the start of the next one.
inline void apply(State& state, const Sample& sample) {
  const bool clockOk = sample.epoch >= MIN_PLAUSIBLE_EPOCH;
  const auto from = static_cast<Event>(state.lastEvent);
  const bool usable = sample.count && from != Event::None && clockOk && state.lastEpoch >= MIN_PLAUSIBLE_EPOCH &&
                      sample.era == state.lastEra && !sample.onUsb && sample.percent <= state.lastPercent &&
                      sample.epoch > state.lastEpoch && sample.epoch - state.lastEpoch <= MAX_STRETCH_SECONDS;
  if (usable) {
    if (Bucket* bucket = bucketFor(state, from, sample.event)) {
      const uint32_t seconds = sample.epoch - state.lastEpoch;
      const uint32_t usedTenths = static_cast<uint32_t>(state.lastPercent - sample.percent) * 10u;
      bucket->seconds += seconds;
      bucket->usedTenths += usedTenths;
      bucket->count++;
      if (bucket == &state.asleep) {
        state.lastSleepSeconds = seconds;
        state.lastSleepUsedTenths = usedTenths;
      }
      if (static_cast<uint64_t>(state.awake.seconds) + state.asleep.seconds + state.sync.seconds > WINDOW_SECONDS) {
        halve(state.awake);
        halve(state.asleep);
        halve(state.sync);
      }
    }
  }
  state.lastEvent = static_cast<uint8_t>(clockOk ? sample.event : Event::None);
  state.lastPercent = sample.percent;
  state.lastEpoch = sample.epoch;
  state.lastEra = sample.era;
}

// Tenths of a percent used per `perSeconds` (3600 an hour, 86400 a day); -1 until the
// bucket holds at least `minSeconds` of measurement.
inline int32_t rateTenths(const Bucket& bucket, const uint32_t perSeconds, const uint32_t minSeconds) {
  if (bucket.seconds == 0 || bucket.seconds < minSeconds) return -1;
  return static_cast<int32_t>((static_cast<uint64_t>(bucket.usedTenths) * perSeconds + bucket.seconds / 2) /
                              bucket.seconds);
}

}  // namespace BatteryUsageMath

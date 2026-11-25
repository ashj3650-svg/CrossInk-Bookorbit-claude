#include <gtest/gtest.h>

#include "BatteryUsageMath.h"

using BatteryUsageMath::Event;
using BatteryUsageMath::Sample;
using BatteryUsageMath::State;

namespace {
constexpr uint32_t T0 = 1795000000u;  // a plausible epoch
constexpr uint32_t HOUR = 3600u;

Sample at(const Event event, const uint8_t percent, const uint32_t epoch, const bool onUsb = false,
          const uint32_t era = 7, const bool count = true) {
  Sample sample;
  sample.event = event;
  sample.percent = percent;
  sample.epoch = epoch;
  sample.onUsb = onUsb;
  sample.era = era;
  sample.count = count;
  return sample;
}
}  // namespace

TEST(BatteryUsage, CountsAwakeAsleepAndSyncStretches) {
  State state;
  BatteryUsageMath::apply(state, at(Event::Wake, 90, T0, false, 7, false));  // cold boot: start only
  BatteryUsageMath::apply(state, at(Event::SyncStart, 88, T0 + 2 * HOUR));
  BatteryUsageMath::apply(state, at(Event::Sleep, 87, T0 + 2 * HOUR + 60));
  BatteryUsageMath::apply(state, at(Event::Wake, 85, T0 + 10 * HOUR + 60));

  EXPECT_EQ(state.awake.seconds, 2 * HOUR);
  EXPECT_EQ(state.awake.usedTenths, 20u);
  EXPECT_EQ(state.sync.seconds, 60u);
  EXPECT_EQ(state.sync.usedTenths, 10u);
  EXPECT_EQ(state.sync.count, 1u);
  EXPECT_EQ(state.asleep.seconds, 8 * HOUR);
  EXPECT_EQ(state.asleep.usedTenths, 20u);
  EXPECT_EQ(state.lastSleepSeconds, 8 * HOUR);
  EXPECT_EQ(state.lastSleepUsedTenths, 20u);
  // 2% over 8 h asleep is 6.0% a day; 2% over 2 h awake is 1.0% an hour.
  EXPECT_EQ(BatteryUsageMath::rateTenths(state.asleep, 86400u, 6 * HOUR), 60);
  EXPECT_EQ(BatteryUsageMath::rateTenths(state.awake, 3600u, HOUR / 2), 10);
}

TEST(BatteryUsage, SkipsChargingClockLossAndUntrustedTime) {
  State state;
  BatteryUsageMath::apply(state, at(Event::Sleep, 80, T0));
  BatteryUsageMath::apply(state, at(Event::Wake, 95, T0 + HOUR));  // charged while asleep
  EXPECT_EQ(state.asleep.seconds, 0u);
  BatteryUsageMath::apply(state, at(Event::Sleep, 94, T0 + 2 * HOUR, /*onUsb=*/true));
  EXPECT_EQ(state.awake.seconds, 0u);
  BatteryUsageMath::apply(state, at(Event::Wake, 93, T0 + 3 * HOUR, false, /*era=*/8));  // clock lost
  EXPECT_EQ(state.asleep.seconds, 0u);
  BatteryUsageMath::apply(state, at(Event::Sleep, 92, 1000u, false, 8));  // clock unset
  EXPECT_EQ(state.lastEvent, static_cast<uint8_t>(Event::None));
  BatteryUsageMath::apply(state, at(Event::Wake, 91, T0 + 5 * HOUR, false, 8));
  EXPECT_EQ(state.asleep.seconds, 0u);
  EXPECT_EQ(state.awake.seconds, 0u);
}

TEST(BatteryUsage, IgnoresMismatchedEventsAndHalvesPastTheWindow) {
  State state;
  BatteryUsageMath::apply(state, at(Event::Sleep, 90, T0));
  BatteryUsageMath::apply(state, at(Event::Sleep, 89, T0 + HOUR));  // a wake went unrecorded
  EXPECT_EQ(state.asleep.seconds + state.awake.seconds, 0u);

  uint32_t now = T0 + HOUR;
  uint8_t percent = 89;
  for (int day = 0; day < 15; day++) {
    BatteryUsageMath::apply(state, at(Event::Wake, percent, now + 23 * HOUR));
    BatteryUsageMath::apply(state, at(Event::Sleep, percent, now + 24 * HOUR));
    now += 24 * HOUR;
  }
  EXPECT_LE(static_cast<uint64_t>(state.asleep.seconds) + state.awake.seconds, BatteryUsageMath::WINDOW_SECONDS);
  EXPECT_GT(state.asleep.seconds, 0u);
  EXPECT_EQ(BatteryUsageMath::rateTenths(state.asleep, 86400u, 6 * HOUR), 0);
  EXPECT_EQ(BatteryUsageMath::rateTenths(BatteryUsageMath::Bucket{}, 86400u, 0), -1);
}

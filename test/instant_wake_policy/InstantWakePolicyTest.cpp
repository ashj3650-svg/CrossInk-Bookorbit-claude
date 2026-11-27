#include <gtest/gtest.h>

#include "InstantWakePolicy.h"

using InstantWakePolicy::Outcome;
using InstantWakePolicy::Wake;

namespace {
constexpr uint32_t HOUR_MS = 60u * 60u * 1000u;
}  // namespace

TEST(InstantWakePolicy, DurationIsWholeMinutes) {
  EXPECT_EQ(InstantWakePolicy::durationMs(0), 0u);
  EXPECT_EQ(InstantWakePolicy::durationMs(15), 15u * 60u * 1000u);
  EXPECT_EQ(InstantWakePolicy::durationMs(60), HOUR_MS);
  EXPECT_EQ(InstantWakePolicy::durationMs(180), 3u * HOUR_MS);
}

TEST(InstantWakePolicy, RemainingCountsDownAndEndsWithinTheSlack) {
  EXPECT_EQ(InstantWakePolicy::remainingMs(0, HOUR_MS), HOUR_MS);
  EXPECT_EQ(InstantWakePolicy::remainingMs(HOUR_MS / 2, HOUR_MS), HOUR_MS / 2);
  // A timer that fires a little early ends the stretch instead of a moment's more sleep.
  EXPECT_EQ(InstantWakePolicy::remainingMs(HOUR_MS - InstantWakePolicy::TIMER_SLACK_MS, HOUR_MS), 0u);
  EXPECT_EQ(InstantWakePolicy::remainingMs(HOUR_MS - InstantWakePolicy::TIMER_SLACK_MS - 1, HOUR_MS),
            InstantWakePolicy::TIMER_SLACK_MS + 1);
  EXPECT_EQ(InstantWakePolicy::remainingMs(HOUR_MS, HOUR_MS), 0u);
  EXPECT_EQ(InstantWakePolicy::remainingMs(HOUR_MS + 5000, HOUR_MS), 0u);
}

TEST(InstantWakePolicy, RemainingSurvivesMillisWrap) {
  // millis() wraps after ~49.7 days; the caller passes the unsigned difference.
  const uint32_t start = UINT32_MAX - 1000u;
  const uint32_t now = 2000u;  // 3001 ms later, past the wrap
  EXPECT_EQ(InstantWakePolicy::remainingMs(now - start, HOUR_MS), HOUR_MS - 3001u);
}

TEST(InstantWakePolicy, AcceptedPressWakesTheDevice) {
  uint8_t falseWakes = 0;
  EXPECT_EQ(InstantWakePolicy::afterWake(Wake::PowerButton, true, HOUR_MS, falseWakes), Outcome::WakeUp);
  EXPECT_EQ(falseWakes, 0u);
  // Even at the very end of the stretch a real press wins over the hand-off.
  EXPECT_EQ(InstantWakePolicy::afterWake(Wake::PowerButton, true, 0, falseWakes), Outcome::WakeUp);
}

TEST(InstantWakePolicy, TimerEndsTheStretchOrSleepsOn) {
  uint8_t falseWakes = 0;
  EXPECT_EQ(InstantWakePolicy::afterWake(Wake::Timer, false, 0, falseWakes), Outcome::HandOff);
  EXPECT_EQ(InstantWakePolicy::afterWake(Wake::Timer, false, 60000, falseWakes), Outcome::SleepAgain);
  EXPECT_EQ(falseWakes, 0u) << "timer wakes are not false wakes";
}

TEST(InstantWakePolicy, RejectedPressSleepsAgain) {
  uint8_t falseWakes = 0;
  // A tap when only a long press wakes, or noise on the line.
  EXPECT_EQ(InstantWakePolicy::afterWake(Wake::PowerButton, false, HOUR_MS, falseWakes), Outcome::SleepAgain);
  EXPECT_EQ(InstantWakePolicy::afterWake(Wake::Other, false, HOUR_MS, falseWakes), Outcome::SleepAgain);
  EXPECT_EQ(falseWakes, 2u);
  // ...unless the stretch is over by then.
  EXPECT_EQ(InstantWakePolicy::afterWake(Wake::PowerButton, false, 0, falseWakes), Outcome::HandOff);
}

TEST(InstantWakePolicy, FailedSleepHandsOffAtOnce) {
  uint8_t falseWakes = 0;
  EXPECT_EQ(InstantWakePolicy::afterWake(Wake::Failed, false, HOUR_MS, falseWakes), Outcome::HandOff);
}

TEST(InstantWakePolicy, AChipThatKeepsWakingGivesUpOnLightSleep) {
  uint8_t falseWakes = 0;
  for (uint8_t i = 0; i < InstantWakePolicy::MAX_FALSE_WAKES; ++i) {
    ASSERT_EQ(InstantWakePolicy::afterWake(Wake::Other, false, HOUR_MS, falseWakes), Outcome::SleepAgain) << i;
  }
  EXPECT_EQ(InstantWakePolicy::afterWake(Wake::Other, false, HOUR_MS, falseWakes), Outcome::HandOff);
  // The counter saturates rather than wrapping back into the allowed range.
  for (int i = 0; i < 300; ++i) InstantWakePolicy::afterWake(Wake::Other, false, HOUR_MS, falseWakes);
  EXPECT_EQ(InstantWakePolicy::afterWake(Wake::Other, false, HOUR_MS, falseWakes), Outcome::HandOff);
}

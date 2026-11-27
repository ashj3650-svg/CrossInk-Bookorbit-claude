#pragma once

#include <cstdint>

// Instant Wake's decisions, kept free of hardware so the native tests can run them.
//
// The device light-sleeps for the chosen stretch. Each time the chip wakes, it either
// wakes up for real (a Power press that passes the usual wake check), goes back to light
// sleep for what is left, or hands off: the stretch is over, so the device finishes the
// sleep the normal way (BookOrbit sleep sync when it is on, then deep sleep).
namespace InstantWakePolicy {

enum class Wake : uint8_t {
  PowerButton,  // the Power button's level woke the chip (the press still needs judging)
  Timer,        // the requested time ran out
  Other,        // some other source; treat it as no press
  Failed,       // the chip refused to sleep or could not be armed
};

enum class Outcome : uint8_t { WakeUp, SleepAgain, HandOff };

// A timer wake this close to the end of the stretch ends it, rather than sleeping again
// for a moment (the timer can fire a little early).
constexpr uint32_t TIMER_SLACK_MS = 1000;
// Wakes that turn out to be no press (a tap when a long press wakes, or noise) before the
// device stops trying to light-sleep and deep-sleeps instead: a chip that keeps waking
// would otherwise drain the battery at full power for the whole stretch.
constexpr uint8_t MAX_FALSE_WAKES = 50;

constexpr uint32_t durationMs(const uint8_t minutes) { return static_cast<uint32_t>(minutes) * 60u * 1000u; }

// What is left of the stretch, 0 once it is over. `elapsedMs` is a wrap-safe difference of
// millis() values, which keep counting through light sleep.
constexpr uint32_t remainingMs(const uint32_t elapsedMs, const uint32_t durationMs) {
  return elapsedMs >= durationMs || durationMs - elapsedMs <= TIMER_SLACK_MS ? 0 : durationMs - elapsedMs;
}

// `pressAccepted`: the Power button was down after the wake and passed the wake check (only
// asked for a wake that was not the timer's). `falseWakes` counts the wakes that were not.
inline Outcome afterWake(const Wake wake, const bool pressAccepted, const uint32_t remaining, uint8_t& falseWakes) {
  if (wake == Wake::Failed) return Outcome::HandOff;
  if (wake != Wake::Timer) {
    if (pressAccepted) return Outcome::WakeUp;
    if (falseWakes < UINT8_MAX) falseWakes++;
    if (falseWakes > MAX_FALSE_WAKES) return Outcome::HandOff;
  }
  return remaining == 0 ? Outcome::HandOff : Outcome::SleepAgain;
}

}  // namespace InstantWakePolicy

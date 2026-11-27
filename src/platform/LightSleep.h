#pragma once

#include <cstdint>

#include "util/InstantWakePolicy.h"

// Instant Wake (Settings > System > Device): after the device goes to sleep it first only
// light-sleeps, so Power brings it straight back, and runs the usual sleep (BookOrbit sleep
// sync included) once the chosen time is up. The X4 Classic only, plus its simulator.
#if (defined(FREEINK_DEVICE_X4CLASSIC) && FREEINK_DEVICE_X4CLASSIC) || defined(SIMULATOR_DEVICE_X4_CLASSIC)
#define CROSSINK_APP_INSTANT_WAKE 1
#else
#define CROSSINK_APP_INSTANT_WAKE 0
#endif

#if CROSSINK_APP_INSTANT_WAKE

// The chip-level light sleep behind Instant Wake. CPU, RAM and PSRAM keep their contents
// and the program carries on from the call, so nothing has to be loaded again. The panel
// keeps showing the sleep screen and the SD card stays mounted, both idle.
//
// This lives in the app rather than lib/hal because the simulator replaces the whole HAL
// with its own package: the simulator half below waits on the simulated buttons instead.
namespace LightSleep {

using Wake = InstantWakePolicy::Wake;

// Sleeps until the Power button is down or `durationMs` have passed. Wi-Fi must already be
// off. Returns at once with Failed when the button cannot wake the chip.
Wake sleepFor(uint32_t durationMs);

// The Power button as the hardware sees it right now, bypassing debounce.
bool isPowerButtonDown();

// Judges the press that woke the device the way a deep-sleep wake is judged: any press
// when a short press wakes it, otherwise one held for `longHoldMs` (from the wake).
bool verifyWakePress(bool shortPressWakes, uint16_t longHoldMs);

// Blocks until the Power button is up, so a held press cannot wake the chip again at once.
void waitForPowerButtonRelease();

}  // namespace LightSleep

#endif

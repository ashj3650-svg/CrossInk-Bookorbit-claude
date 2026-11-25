#pragma once

#include <cstddef>
#include <cstdint>

#include "BatteryUsageMath.h"

// How fast the battery goes, asleep, awake and in the BookOrbit sleep sync: the figures
// Settings > System > Device > Battery Use shows. main.cpp reads the battery at each
// sleep and wake (BatteryUsageMath has the arithmetic); the running totals live in
// /.crosspoint/battery_usage.bin (see docs/file-formats.md), one small rewrite per event.
namespace BatteryUsage {

using BatteryUsageMath::Event;

// Never fatal: a failure logs and the figures miss that stretch.
void record(Event event, uint8_t percent, bool onUsb, bool count = true);

// The report, one line per figure, separated by '\n'.
void formatReport(char* out, size_t outSize);

}  // namespace BatteryUsage

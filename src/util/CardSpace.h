#pragma once

#include <cstdint>

// Storage.usedBytes() counts the card's free clusters by reading its whole allocation
// table, with the SD card and the display's bus held. exFAT keeps one bit per cluster
// there, a fraction of a second even for a 128 GB card. FAT32 keeps four bytes per
// cluster, which takes seconds on a typical card and minutes on a slow one (#110).
namespace CardSpace {

// True when the card is exFAT, so usedBytes() is quick enough to call where the user
// waits. False on FAT, and when the format cannot be told.
bool isQuickToCount();

// The card's size and used bytes, counted once and kept until books are added, replaced or
// removed (library::libraryChangeCount moves): in RAM while awake, and across deep sleep
// in RTC memory (see saveForSleep). A wake then shows
// the BookOrbit Home's storage bar without reading the allocation table again. Zeros
// when the card is not quick to count (FAT32) or did not say.
void usage(uint64_t& total, uint64_t& used);

// Counts again now when nothing is kept, for the sleep sync to do behind the sleep screen
// what the next Home would otherwise do while you wait.
void countIfUnknown();

// Deep sleep keeps RTC memory: the counted figures ride through it when they are still
// current. restoreAfterSleep() runs on every boot and takes them only on a deep-sleep
// wake; a restart or power loss counts afresh, as before.
void saveForSleep();
void restoreAfterSleep(bool deepSleepWake);

}  // namespace CardSpace

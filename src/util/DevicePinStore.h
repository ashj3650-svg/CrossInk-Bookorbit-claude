#pragma once

#include <cstdint>

// The four-digit PIN that guards the device at startup, kept in
// /.crosspoint/device_pin.bin (see docs/file-formats.md). The file holds a salted hash,
// not the digits, so a glance at the card does not give the PIN away. It is a lock on
// this firmware's screens, not encryption: the SD card can still be read elsewhere.
namespace DevicePinStore {

constexpr uint8_t PIN_LENGTH = 4;

// True when a PIN is set: the startup lock then asks for it.
bool isSet();

bool verify(const uint8_t (&digits)[PIN_LENGTH]);

// Replaces whatever was set. Returns false (and logs) when the card cannot be written,
// so a caller can tell the user the PIN did not take.
bool set(const uint8_t (&digits)[PIN_LENGTH]);

// Takes the PIN off. Succeeds when none was set.
bool clear();

}  // namespace DevicePinStore

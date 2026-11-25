#include "DevicePinStore.h"

#include <Arduino.h>
#include <HalStorage.h>
#include <Logging.h>

namespace DevicePinStore {
namespace {

constexpr char PATH[] = "/.crosspoint/device_pin.bin";
constexpr uint32_t MAGIC = 0x4E495043;  // "CPIN"
constexpr uint16_t VERSION = 1;

struct Record {
  uint32_t magic = MAGIC;
  uint16_t version = VERSION;
  uint16_t reserved = 0;
  uint32_t salt = 0;
  uint32_t hash = 0;
};
static_assert(sizeof(Record) == 16, "device_pin.bin holds a 16-byte record");

// FNV-1a over the salt and the digits. Four digits are ten thousand combinations, so no
// hash can make the file itself secret; the salt only stops two devices with the same
// PIN from showing the same number, and keeps the digits out of a card dump.
uint32_t hashOf(const uint32_t salt, const uint8_t (&digits)[PIN_LENGTH]) {
  uint32_t hash = 2166136261u;
  for (int i = 0; i < 4; i++) {
    hash = (hash ^ static_cast<uint8_t>(salt >> (8 * i))) * 16777619u;
  }
  for (const uint8_t digit : digits) {
    hash = (hash ^ digit) * 16777619u;
  }
  return hash;
}

bool load(Record& out) {
  HalFile file;
  if (!Storage.exists(PATH) || !Storage.openFileForRead("PIN", PATH, file)) return false;
  const bool ok =
      file.read(&out, sizeof(out)) == static_cast<int>(sizeof(out)) && out.magic == MAGIC && out.version == VERSION;
  file.close();
  return ok;
}

}  // namespace

bool isSet() {
  Record record;
  return load(record);
}

bool verify(const uint8_t (&digits)[PIN_LENGTH]) {
  Record record;
  if (!load(record)) return true;  // no PIN set: nothing to fail
  return hashOf(record.salt, digits) == record.hash;
}

bool set(const uint8_t (&digits)[PIN_LENGTH]) {
  Record record;
  // esp_random() on hardware, and the simulator's shim on the host: the salt only needs
  // to differ between devices.
  record.salt = static_cast<uint32_t>(random(0, 0x7FFFFFFF)) ^ (millis() << 1);
  record.hash = hashOf(record.salt, digits);
  HalFile file;
  if (!Storage.openFileForWrite("PIN", PATH, file)) return false;
  const bool ok = file.write(reinterpret_cast<const uint8_t*>(&record), sizeof(record)) == sizeof(record);
  file.close();
  if (!ok) {
    LOG_ERR("PIN", "Cannot write the device PIN");
    Storage.remove(PATH);
  }
  return ok;
}

bool clear() {
  if (!Storage.exists(PATH)) return true;
  if (Storage.remove(PATH)) return true;
  LOG_ERR("PIN", "Cannot remove the device PIN");
  return false;
}

}  // namespace DevicePinStore

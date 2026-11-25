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

// isSet() runs at every boot and on every Settings paint: remember the answer instead of
// opening the file each time. -1 = not read yet. set() and clear() keep it current.
int8_t cachedIsSet = -1;

// FNV-1a over the salt and the digits. Four digits are ten thousand combinations, so no
// hash can make the file itself secret; the salt only stops two devices with the same
// PIN from showing the same number, and keeps the digits out of a card dump.
uint32_t hashOf(const uint32_t salt, const uint8_t (&digits)[PIN_LENGTH]) {
  uint32_t hash = 2166136261u;
  for (int i = 0; i < static_cast<int>(sizeof(salt)); i++) {
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
  if (cachedIsSet < 0) {
    Record record;
    cachedIsSet = load(record) ? 1 : 0;
  }
  return cachedIsSet == 1;
}

bool verify(const uint8_t (&digits)[PIN_LENGTH]) {
  Record record;
  if (!load(record)) return true;  // no PIN set: nothing to fail
  return hashOf(record.salt, digits) == record.hash;
}

bool set(const uint8_t (&digits)[PIN_LENGTH]) {
  Record record;
  // Arduino's random() draws from esp_random() on hardware (std::rand() in the
  // simulator): the salt only needs to differ between devices.
  record.salt = static_cast<uint32_t>(random(0x7FFFFFFF)) ^ (millis() << 1);
  record.hash = hashOf(record.salt, digits);
  HalFile file;
  if (!Storage.openFileForWrite("PIN", PATH, file)) {
    LOG_ERR("PIN", "Cannot open the device PIN file for writing");
    return false;
  }
  const bool ok = file.write(reinterpret_cast<const uint8_t*>(&record), sizeof(record)) == sizeof(record);
  file.close();
  if (!ok) {
    LOG_ERR("PIN", "Cannot write the device PIN");
    Storage.remove(PATH);
  }
  // A failed write leaves no PIN behind: the old one went with the truncated file.
  cachedIsSet = ok ? 1 : 0;
  return ok;
}

bool clear() {
  if (Storage.exists(PATH) && !Storage.remove(PATH)) {
    LOG_ERR("PIN", "Cannot remove the device PIN");
    return false;
  }
  cachedIsSet = 0;
  return true;
}

}  // namespace DevicePinStore

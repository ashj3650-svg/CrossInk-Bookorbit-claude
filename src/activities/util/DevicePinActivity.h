#pragma once

#include <cstdint>
#include <functional>

#include "activities/Activity.h"
#include "components/themes/BaseTheme.h"
#include "util/DevicePinStore.h"

// The four-digit PIN screen: the lock the device starts behind, and the screen that sets
// a new PIN (Settings > System > Device > Device PIN).
//
// Buttons work like a combination lock: Up/Right raise the digit in the highlighted box,
// Down/Left lower it, Confirm moves to the next box (and checks the PIN after the
// fourth), Back steps back a box. On a touch device a number pad is drawn as well. In
// Unlock mode Back on the first box does nothing, so the only ways past the lock are
// the PIN and sleep.
class DevicePinActivity final : public Activity {
 public:
  enum class Mode : uint8_t {
    Unlock,  // startup: runs onUnlocked and never finishes on its own
    SetNew,  // asks twice, saves, then finishes (isCancelled when Back leaves it)
  };

  DevicePinActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, Mode mode,
                    std::function<void()> onUnlocked = {});

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  // Startup lock: a global shortcut must not reach the screen behind it.
  bool blocksGlobalInput() const override { return mode == Mode::Unlock; }
  bool isDeviceLock() const override { return mode == Mode::Unlock; }

 private:
  static constexpr uint8_t LENGTH = DevicePinStore::PIN_LENGTH;
  static constexpr uint8_t ATTEMPTS_BEFORE_WAIT = 5;
  static constexpr unsigned long WAIT_MS = 30000;

  enum class Notice : uint8_t { None, WrongPin, Mismatch, SaveFailed };

  void changeDigit(int delta);
  void typeDigit(uint8_t digit);
  void advance();
  void stepBack();
  void submit();
  void resetEntry();
  bool handleTouch();
  // The number pad's twelve keys: 0-8 are 1-9, 9 is Delete, 10 is 0, 11 is OK.
  Rect keyRect(int key) const;
  int keypadTop() const;

  Mode mode;
  std::function<void()> onUnlocked;
  uint8_t digits[LENGTH] = {};
  uint8_t first[LENGTH] = {};  // SetNew: the first entry, to confirm against
  uint8_t cursor = 0;
  bool confirming = false;  // SetNew: asking for the second entry
  bool unlocking = false;   // the correct PIN is in: stop taking input
  bool firstPaint = true;
  Notice notice = Notice::None;
  uint8_t wrongAttempts = 0;
  unsigned long waitUntil = 0;  // after too many wrong tries, input is ignored until then
};

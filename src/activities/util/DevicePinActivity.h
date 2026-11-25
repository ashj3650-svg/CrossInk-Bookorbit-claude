#pragma once

#include <cstdint>
#include <functional>

#include "activities/Activity.h"
#include "util/DevicePinStore.h"

// The four-digit PIN screen: the lock the device starts behind, and the screen that sets
// a new PIN (Settings > System > Device > Device PIN).
//
// Entry works the same with buttons and with touch: Up/Down pick the digit under the
// cursor, Left/Right (or a tap on a box) move between the four boxes, Confirm accepts.
// On a touch device the ten keys are tappable as well. In Unlock mode Back does nothing,
// so the only ways past it are the PIN and the power button.
class DevicePinActivity final : public Activity {
 public:
  enum class Mode : uint8_t {
    Unlock,  // startup: runs onUnlocked and never finishes on its own
    SetNew,  // asks twice, saves, then finishes with the result
  };

  DevicePinActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, Mode mode,
                    std::function<void()> onUnlocked = {});

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  // Startup lock: a global shortcut must not reach the screen behind it.
  bool blocksGlobalInput() const override { return mode == Mode::Unlock; }

 private:
  static constexpr uint8_t LENGTH = DevicePinStore::PIN_LENGTH;

  void moveCursor(int delta);
  void changeDigit(int delta);
  void submit();
  void resetEntry();
  Rect boxRect(int index) const;
  Rect keyRect(int key) const;
  void drawEntry() const;

  Mode mode;
  std::function<void()> onUnlocked;
  uint8_t digits[LENGTH] = {};
  uint8_t first[LENGTH] = {};  // SetNew: the first entry, to confirm against
  uint8_t cursor = 0;
  bool confirming = false;  // SetNew: asking for the second entry
  bool wrongPin = false;
  bool unlocking = false;  // the correct PIN is in: stop taking input
  uint8_t attempts = 0;
};

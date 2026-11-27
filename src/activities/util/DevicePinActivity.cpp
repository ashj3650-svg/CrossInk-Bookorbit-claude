#include "DevicePinActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <cstdio>
#include <cstring>
#include <utility>

#include "components/UITheme.h"
#include "fontIds.h"

namespace {

constexpr int BOX_SIZE = 64;
constexpr int BOX_GAP = 16;
constexpr int KEY_COLUMNS = 3;
constexpr int KEY_ROWS = 4;
constexpr int KEY_WIDTH = 96;
constexpr int KEY_HEIGHT = 56;
constexpr int KEY_GAP = 10;
constexpr int KEY_DELETE = 9;
constexpr int KEY_ZERO = 10;
constexpr int KEY_OK = 11;

}  // namespace

DevicePinActivity::DevicePinActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const Mode mode,
                                     std::function<void()> onUnlocked)
    : Activity("DevicePin", renderer, mappedInput), mode(mode), onUnlocked(std::move(onUnlocked)) {}

void DevicePinActivity::onEnter() {
  Activity::onEnter();
  resetEntry();
  requestUpdate(true);
}

void DevicePinActivity::resetEntry() {
  memset(digits, 0, sizeof(digits));
  cursor = 0;
}

void DevicePinActivity::changeDigit(const int delta) {
  digits[cursor] = static_cast<uint8_t>((digits[cursor] + 10 + delta) % 10);
  notice = Notice::None;
  requestUpdate();
}

void DevicePinActivity::typeDigit(const uint8_t digit) {
  digits[cursor] = digit;
  notice = Notice::None;
  advance();
}

void DevicePinActivity::advance() {
  if (cursor + 1 < LENGTH) {
    cursor++;
    digits[cursor] = 0;
    requestUpdate();
    return;
  }
  submit();
}

void DevicePinActivity::stepBack() {
  notice = Notice::None;
  if (cursor > 0) {
    digits[cursor] = 0;
    cursor--;
    requestUpdate();
    return;
  }
  if (mode == Mode::Unlock) return;
  if (confirming) {
    // Back from the second entry goes back to choosing the PIN.
    confirming = false;
    resetEntry();
    requestUpdate();
    return;
  }
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}

void DevicePinActivity::submit() {
  if (mode == Mode::Unlock) {
    if (DevicePinStore::verify(digits)) {
      LOG_INF("PIN", "Device unlocked");
      unlocking = true;
      if (onUnlocked) onUnlocked();
      return;
    }
    wrongAttempts++;
    LOG_INF("PIN", "Wrong PIN (%u)", static_cast<unsigned>(wrongAttempts));
    notice = Notice::WrongPin;
    if (wrongAttempts % ATTEMPTS_BEFORE_WAIT == 0) waitUntil = millis() + WAIT_MS;
    resetEntry();
    requestUpdate();
    return;
  }

  if (!confirming) {
    memcpy(first, digits, sizeof(first));
    confirming = true;
    resetEntry();
    requestUpdate();
    return;
  }
  if (memcmp(first, digits, sizeof(first)) != 0) {
    notice = Notice::Mismatch;
    confirming = false;
    resetEntry();
    requestUpdate();
    return;
  }
  if (!DevicePinStore::set(digits)) {
    notice = Notice::SaveFailed;
    confirming = false;
    resetEntry();
    requestUpdate();
    return;
  }
  LOG_INF("PIN", "Device PIN set");
  setResult(ActivityResult{});
  finish();
}

int DevicePinActivity::keypadTop() const {
  const Rect screen = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const int padHeight = KEY_ROWS * KEY_HEIGHT + (KEY_ROWS - 1) * KEY_GAP;
  return screen.y + screen.height - padHeight - UITheme::getInstance().getMetrics().verticalSpacing * 2;
}

Rect DevicePinActivity::keyRect(const int key) const {
  const Rect screen = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const int padWidth = KEY_COLUMNS * KEY_WIDTH + (KEY_COLUMNS - 1) * KEY_GAP;
  const int left = screen.x + (screen.width - padWidth) / 2;
  const int column = key % KEY_COLUMNS;
  const int row = key / KEY_COLUMNS;
  return Rect{left + column * (KEY_WIDTH + KEY_GAP), keypadTop() + row * (KEY_HEIGHT + KEY_GAP), KEY_WIDTH, KEY_HEIGHT};
}

bool DevicePinActivity::handleTouch() {
  if (!mappedInput.hasTouch()) return false;
  int x = 0;
  int y = 0;
  if (!mappedInput.wasScreenTapped(x, y)) return false;
  for (int key = 0; key < KEY_COLUMNS * KEY_ROWS; key++) {
    const Rect rect = keyRect(key);
    if (x < rect.x || x >= rect.x + rect.width || y < rect.y || y >= rect.y + rect.height) continue;
    if (key == KEY_DELETE) {
      stepBack();
    } else if (key == KEY_OK) {
      advance();
    } else {
      typeDigit(key == KEY_ZERO ? 0 : static_cast<uint8_t>(key + 1));
    }
    return true;
  }
  return false;
}

void DevicePinActivity::loop() {
  if (unlocking) return;
  if (waitUntil != 0) {
    // Too many wrong tries: every press is ignored until the wait is over.
    if (static_cast<long>(millis() - waitUntil) < 0) return;
    waitUntil = 0;
    notice = Notice::None;
    requestUpdate();
    return;
  }
  if (handleTouch()) return;

  using Button = MappedInputManager::Button;
  if (mappedInput.wasPressed(Button::Up) || mappedInput.wasPressed(Button::Right)) {
    changeDigit(1);
  } else if (mappedInput.wasPressed(Button::Down) || mappedInput.wasPressed(Button::Left)) {
    changeDigit(-1);
  } else if (mappedInput.wasReleased(Button::Confirm)) {
    advance();
  } else if (mappedInput.wasReleased(Button::Back)) {
    stepBack();
  }
}

void DevicePinActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect screen = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const bool waiting = waitUntil != 0;
  // Wrapped lines keep a margin instead of running edge to edge.
  const Rect textArea{screen.x + BOX_GAP * 2, screen.y, screen.width - BOX_GAP * 4, screen.height};

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight},
                 tr(STR_DEVICE_PIN));

  int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing * 4;
  const char* prompt = mode == Mode::Unlock ? tr(STR_PIN_ENTER) : confirming ? tr(STR_PIN_AGAIN) : tr(STR_PIN_CHOOSE);
  UITheme::drawCenteredText(renderer, screen, UI_12_FONT_ID, y, prompt, true, EpdFontFamily::BOLD);
  y += renderer.getLineHeight(UI_12_FONT_ID) + metrics.verticalSpacing * 3;

  // The four boxes: entered digits are masked, the current one shows its value.
  const int rowWidth = LENGTH * BOX_SIZE + (LENGTH - 1) * BOX_GAP;
  const int rowLeft = screen.x + (screen.width - rowWidth) / 2;
  const int digitHeight = renderer.getLineHeight(UI_12_FONT_ID);
  for (int i = 0; i < LENGTH; i++) {
    const int x = rowLeft + i * (BOX_SIZE + BOX_GAP);
    const bool current = i == cursor && !waiting;
    renderer.drawRect(x, y, BOX_SIZE, BOX_SIZE, current ? 4 : 1, true);
    char text[2] = {};
    if (i < cursor) {
      text[0] = '*';
    } else if (current) {
      text[0] = static_cast<char>('0' + digits[i]);
    }
    if (text[0] == '\0') continue;
    const int textWidth = renderer.getTextWidth(UI_12_FONT_ID, text, EpdFontFamily::BOLD);
    renderer.drawText(UI_12_FONT_ID, x + (BOX_SIZE - textWidth) / 2, y + (BOX_SIZE - digitHeight) / 2, text, true,
                      EpdFontFamily::BOLD);
  }
  y += BOX_SIZE + metrics.verticalSpacing * 2;
  if (!mappedInput.hasTouch() && !waiting) {
    // The side buttons change the digit too; their hints would only show a "+" and a
    // rotated "-", so say it in words.
    y += UITheme::drawCenteredWrappedText(renderer, textArea, SMALL_FONT_ID, y, tr(STR_PIN_HELP), 2);
    y += metrics.verticalSpacing * 3;
  }

  char waitText[96];
  const char* noticeText = nullptr;
  if (waiting) {
    snprintf(waitText, sizeof(waitText), tr(STR_PIN_WAIT_FORMAT), static_cast<int>(WAIT_MS / 1000));
    noticeText = waitText;
  } else if (notice == Notice::WrongPin) {
    noticeText = tr(STR_PIN_WRONG);
  } else if (notice == Notice::Mismatch) {
    noticeText = tr(STR_PIN_MISMATCH);
  } else if (notice == Notice::SaveFailed) {
    noticeText = tr(STR_PIN_SAVE_FAILED);
  }
  if (noticeText) {
    y += UITheme::drawCenteredWrappedText(renderer, textArea, UI_10_FONT_ID, y, noticeText, 2, true,
                                          EpdFontFamily::BOLD);
    y += metrics.verticalSpacing * 2;
  }
  // Someone who keeps getting it wrong may have forgotten it: say how to start over.
  if (mode == Mode::Unlock && wrongAttempts >= ATTEMPTS_BEFORE_WAIT) {
    UITheme::drawCenteredWrappedText(renderer, textArea, SMALL_FONT_ID, y, tr(STR_PIN_FORGOT), 3);
  }

  if (mappedInput.hasTouch()) {
    for (int key = 0; key < KEY_COLUMNS * KEY_ROWS; key++) {
      const Rect rect = keyRect(key);
      renderer.drawRect(rect.x, rect.y, rect.width, rect.height, 1, true);
      const char label[2] = {static_cast<char>(key == KEY_ZERO ? '0' : '1' + key), '\0'};
      const char* text = label;
      if (key == KEY_DELETE) {
        text = tr(STR_DELETE);
      } else if (key == KEY_OK) {
        text = tr(STR_OK);
      }
      UITheme::drawCenteredText(renderer, rect, UI_10_FONT_ID,
                                rect.y + (rect.height - renderer.getLineHeight(UI_10_FONT_ID)) / 2, text);
    }
  }

  const char* backLabel = "";
  if (!waiting && cursor > 0) {
    backLabel = tr(STR_DELETE);
  } else if (!waiting && mode == Mode::SetNew) {
    backLabel = confirming ? tr(STR_BACK) : tr(STR_CANCEL);
  }
  const char* confirmLabel = waiting ? "" : (cursor + 1 < LENGTH ? tr(STR_PIN_NEXT) : tr(STR_OK));
  const auto labels = mappedInput.mapLabels(backLabel, confirmLabel, waiting ? "" : "-", waiting ? "" : "+");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  // The lock's first paint replaces the boot splash or the sleep image and wants a clean
  // transition; the digit changes after that are small.
  renderer.displayBuffer(firstPaint && mode == Mode::Unlock ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
  firstPaint = false;
}

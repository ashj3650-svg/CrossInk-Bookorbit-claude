#include "ReaderRecapActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <array>
#include <cstdio>

#include "MappedInputManager.h"
#include "components/TouchHeaderBackButton.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
using Button = MappedInputManager::Button;
constexpr std::array<Button, 9> DISMISS_BUTTONS = {Button::Back,  Button::Confirm,  Button::Left,
                                                   Button::Right, Button::Up,       Button::Down,
                                                   Button::Power, Button::PageBack, Button::PageForward};
constexpr int QUOTE_MAX_LINES = 8;
}  // namespace

void ReaderRecapActivity::onEnter() {
  Activity::onEnter();
  requestUpdate();
}

void ReaderRecapActivity::loop() {
  for (const Button button : DISMISS_BUTTONS) {
    if (mappedInput.wasPressed(button)) pressSeen = true;
  }
  bool released = false;
  for (const Button button : DISMISS_BUTTONS) {
    if (mappedInput.wasReleased(button)) released = true;
  }
  int tapX = 0;
  int tapY = 0;
  const bool tapped = mappedInput.wasScreenTapped(tapX, tapY);
  if ((released && pressSeen) || tapped) {
    finish();
  }
}

void ReaderRecapActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, !mappedInput.hasTouchHardware(), false);

  Rect header = TouchHeaderBackButton::standardHeaderRect(renderer);
  header.x = safe.x;
  header.width = safe.width;
  GUI.drawHeader(renderer, header, tr(STR_PREVIOUSLY), nullptr, true);

  const int left = safe.x + metrics.contentSidePadding;
  const int width = std::max(1, safe.width - metrics.contentSidePadding * 2);
  int y = header.y + header.height + metrics.verticalSpacing * 2;

  const int titleLineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  for (const auto& line : renderer.wrappedText(UI_12_FONT_ID, bookTitle.c_str(), width, 2, EpdFontFamily::BOLD)) {
    renderer.drawText(UI_12_FONT_ID, left, y, line.c_str(), true, EpdFontFamily::BOLD);
    y += titleLineHeight;
  }

  const int detailLineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  if (!chapterTitle.empty()) {
    for (const auto& line : renderer.wrappedText(UI_10_FONT_ID, chapterTitle.c_str(), width, 2)) {
      renderer.drawText(UI_10_FONT_ID, left, y, line.c_str());
      y += detailLineHeight;
    }
  }

  char away[64];
  if (daysAway == 1) {
    snprintf(away, sizeof(away), "%s", tr(STR_LAST_READ_YESTERDAY));
  } else {
    snprintf(away, sizeof(away), tr(STR_LAST_READ_DAYS_AGO), static_cast<unsigned>(daysAway));
  }
  renderer.drawText(UI_10_FONT_ID, left, y, away);
  y += detailLineHeight + metrics.verticalSpacing * 2;

  if (!lastLines.empty()) {
    renderer.drawLine(left, y, left + width - 1, y);
    y += metrics.verticalSpacing * 2;
    const int quoteLineHeight = renderer.getLineHeight(UI_12_FONT_ID);
    const int bottom = safe.y + safe.height - metrics.buttonHintsHeight - metrics.verticalSpacing;
    const int fitLines = std::max(0, (bottom - y) / std::max(1, quoteLineHeight));
    const int maxLines = std::min(QUOTE_MAX_LINES, fitLines);
    if (maxLines > 0) {
      // The quote is the end of a page: keep its last lines, which lead into the page
      // the book reopens at, and mark the cut at the front.
      auto lines = renderer.wrappedText(UI_12_FONT_ID, lastLines.c_str(), width, 64);
      const size_t first = lines.size() > static_cast<size_t>(maxLines) ? lines.size() - maxLines : 0;
      for (size_t i = first; i < lines.size(); ++i) {
        std::string text = (i == first && first > 0) ? "..." + lines[i] : lines[i];
        renderer.drawText(UI_12_FONT_ID, left, y, text.c_str());
        y += quoteLineHeight;
      }
    }
  }

  if (!mappedInput.hasTouchHardware()) {
    const auto labels = mappedInput.mapLabels("", tr(STR_READ), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4, true);
  }
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

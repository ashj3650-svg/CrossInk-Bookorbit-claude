#include "AlertActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <string>

#include "CrossPointState.h"
#include "components/UITheme.h"
#include "fontIds.h"

void AlertActivity::onEnter() {
  Activity::onEnter();
  title = APP_STATE.pendingAlertTitle;
  body = APP_STATE.pendingAlertBody;
  goHomeOnBack = APP_STATE.pendingAlertGoHomeOnBack.exchange(false, std::memory_order_relaxed);
  if (requestUpdateAndWait() != RequestUpdateResult::Rendered) {
    LOG_ERR("ALERT", "Alert screen could not be rendered synchronously");
    requestUpdate();
  }
}

void AlertActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (goHomeOnBack) {
      onGoHome();
    } else {
      finish();
    }
  }
}

void AlertActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const int x = safe.x + metrics.contentSidePadding;
  const int contentWidth = safe.width - 2 * metrics.contentSidePadding;
  const auto lineHeight = renderer.getLineHeight(UI_10_FONT_ID);

  const Rect header = UITheme::getHeaderRect(renderer, metrics.headerHeight);
  GUI.drawHeader(renderer, header, title.c_str());

  int y = header.y + header.height + metrics.verticalSpacing;

  // '\n' starts a new paragraph, each wrapped on its own, ten lines in all and no more
  // than the safe area holds.
  int linesLeft = std::max(0, std::min(10, (safe.y + safe.height - y) / lineHeight));
  size_t start = 0;
  while (start <= body.size() && linesLeft > 0) {
    const size_t end = std::min(body.find('\n', start), body.size());
    const std::string paragraph = body.substr(start, end - start);
    for (const auto& line : renderer.wrappedText(UI_10_FONT_ID, paragraph.c_str(), contentWidth, linesLeft)) {
      renderer.drawText(UI_10_FONT_ID, x, y, line.c_str());
      y += lineHeight;
      linesLeft--;
    }
    start = end + 1;
  }

  const auto labels = mappedInput.mapLabels(
      goHomeOnBack ? mappedInput.withBackArrow(tr(STR_HOME)) : mappedInput.withBackArrow(tr(STR_BACK)), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

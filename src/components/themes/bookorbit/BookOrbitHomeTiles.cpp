#include "BookOrbitHomeTiles.h"

#include <Bitmap.h>
#include <BoardConfig.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <iterator>

#include "BookOrbitHomeLayout.h"
#include "BookOrbitHomeText.h"
#include "CrossPointSettings.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "components/icons/listIcons.h"
#include "fontIds.h"

namespace {
using BookOrbitHomeLayout::kIconSize;
using BookOrbitHomeLayout::kMargin;
using BookOrbitHomeLayout::kPad;
using BookOrbitHomeLayout::kTileRadius;
using BookOrbitHomeLayout::kTitleRowTop;

// The big numbers. Boards with scalable reader fonts do not carry the built-in Lexend.
#if CROSSINK_SCALABLE_FONTS
constexpr int kBigFontId = UI_12_FONT_ID;
#else
constexpr int kBigFontId = LEXENDDECA_16_FONT_ID;
#endif
constexpr int kBigValueTop = 28;
constexpr int kBigValueBaseline = 34;  // from kBigValueTop

// Covers drawn since the device woke, kept as snapshots of the framebuffer region they
// were drawn into, in PSRAM. Drawing one again (a page flip, Home again after a menu or a
// sync) is then a memory copy instead of opening and decoding a BMP on the SD card. The
// snapshot is the 1-bit packed panel region, about 3 to 8 KB a cover. Boards without
// PSRAM never fill it. Each wake starts empty, and opening a book frees it.
struct CachedCover {
  std::string path;
  int16_t slotX = 0, slotY = 0, slotW = 0, slotH = 0;
  int16_t x = 0, y = 0, w = 0, h = 0;  // the fitted rect the snapshot covers
  uint8_t orientation = 0;
  bool onBlack = false;
  bool inverted = false;
  uint32_t lastUse = 0;
  size_t bytes = 0;
  HeapByteBuffer data;
};
// One idle slot on the ESP32-C3 boards, which have no PSRAM to fill it.
constexpr size_t kCachedCovers = FREEINK_MCU_C3 ? 1 : 16;
CachedCover gCoverCache[kCachedCovers];
uint32_t gCoverUseClock = 0;

bool sameSlot(const CachedCover& cover, const char* path, const Rect& slot, const uint8_t orientation,
              const bool onBlack, const bool inverted) {
  return cover.data && cover.slotX == slot.x && cover.slotY == slot.y && cover.slotW == slot.width &&
         cover.slotH == slot.height && cover.orientation == orientation && cover.onBlack == onBlack &&
         cover.inverted == inverted && cover.path == path;
}
}  // namespace

void BookOrbitHomeTiles::releaseCoverCache() {
  std::fill(std::begin(gCoverCache), std::end(gCoverCache), CachedCover{});
}

std::string BookOrbitHomeTiles::coverPath(const RecentBook& book, const int width, const int height) {
  if (book.coverBmpPath.empty()) return {};
  if (FsHelpers::hasEpubExtension(book.path)) {
    return Epub(book.path, "/.crosspoint").getAdaptiveThumbBmpPath(width, height);
  }
  return UITheme::getCoverThumbPath(book.coverBmpPath, width, height);
}

void BookOrbitHomeTiles::drawTitleRow(const char* title, const int pageCount, const int currentPage,
                                      const bool withSearch) const {
  const int y = kTitleRowTop + UITheme::getTopStatusBarInset(renderer);
  int titleWidth = renderer.getScreenWidth() - 2 * kMargin - (pageCount * 15 + 8);
  if (withSearch) {
    const Rect button = BookOrbitHomeLayout::searchButtonRect(renderer, pageCount);
    titleWidth = button.x - kMargin - kPad / 2;
    drawLucideIcon(renderer, icon_search_24, button.x + (button.width - kIconSize) / 2,
                   button.y + (button.height - kIconSize) / 2);
  }
  drawFitted(UI_12_FONT_ID, kMargin, y, title, titleWidth, true);
  constexpr int dotRadius = 4;
  int centerX = renderer.getScreenWidth() - kMargin - dotRadius;
  const int centerY = y + 17;
  for (int i = pageCount - 1; i >= 0; i--) {
    drawDot(centerX - dotRadius, centerY - dotRadius, dotRadius, i == currentPage);
    centerX -= 15;
  }
}

void BookOrbitHomeTiles::drawFitted(const int fontId, const int x, const int y, const char* text, const int maxWidth,
                                    const bool bold, const bool black) const {
  const auto style = bold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
  renderer.drawText(fontId, x, y, renderer.truncatedText(fontId, text, maxWidth, style).c_str(), black, style);
}

void BookOrbitHomeTiles::drawRightAligned(const int fontId, const int right, const int y, const char* text,
                                          const bool bold) const {
  const auto style = bold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
  renderer.drawText(fontId, right - renderer.getTextWidth(fontId, text, style), y, text, true, style);
}

void BookOrbitHomeTiles::drawTileFrame(const Rect& tile) const {
  renderer.drawRoundedRect(tile.x, tile.y, tile.width, tile.height, 1, kTileRadius, true);
}

void BookOrbitHomeTiles::drawTileHeader(const Rect& tile, const char* title, const freeink::Icon* icon) const {
  drawTileFrame(tile);
  const int reserved = icon != nullptr ? kIconSize + 2 * kPad : kPad;
  drawFitted(UI_10_FONT_ID, tile.x + kPad, tile.y + 7, title, tile.width - kPad - reserved, true);
  if (icon != nullptr) {
    drawLucideIcon(renderer, *icon, tile.x + tile.width - kPad - kIconSize, tile.y + 7);
  }
}

void BookOrbitHomeTiles::drawTileFooter(const Rect& tile, const char* text) const {
  drawFitted(SMALL_FONT_ID, tile.x + kPad, tile.y + tile.height - 26, text, tile.width - 2 * kPad);
}

void BookOrbitHomeTiles::drawDot(const int left, const int top, const int radius, const bool filled) const {
  const int size = 2 * radius + 1;
  if (filled) {
    renderer.fillRoundedRect(left, top, size, size, radius, Color::Black);
  } else {
    renderer.drawRoundedRect(left, top, size, size, 1, radius, true);
  }
}

void BookOrbitHomeTiles::drawBigValue(const Rect& tile, const char* value, const char* unit) const {
  const int y = tile.y + kBigValueTop;
  const int valueY = y + kBigValueBaseline - renderer.getFontAscenderSize(kBigFontId);
  renderer.drawText(kBigFontId, tile.x + kPad, valueY, value, true, EpdFontFamily::BOLD);
  if (unit == nullptr) return;
  const int x = tile.x + kPad + renderer.getTextWidth(kBigFontId, value, EpdFontFamily::BOLD) + 5;
  drawFitted(UI_10_FONT_ID, x, y + 14, unit, tile.x + tile.width - kPad - x);
}

void BookOrbitHomeTiles::drawBar(const int x, const int base, const int width, const int chartHeight,
                                 const uint32_t value, const uint32_t highest, const bool emphasized) const {
  const int height = std::max(value > 0 ? 3 : 1, static_cast<int>(chartHeight * value / highest));
  if (emphasized) {
    renderer.fillRect(x, base - height, width, height);
  } else {
    renderer.fillRectDither(x, base - height, width, height, Color::DarkGray);
    renderer.drawRect(x, base - height, width, height);
  }
}

void BookOrbitHomeTiles::drawProgressBar(const int x, const int y, const int width, const int height,
                                         const float fraction, const bool black) const {
  renderer.drawRoundedRect(x, y, width, height, 1, height / 2, black);
  const float clamped = std::clamp(fraction, 0.0f, 1.0f);
  if (clamped <= 0.0f) return;
  const int fill = std::max(height, static_cast<int>(static_cast<float>(width - 2) * clamped + 0.5f));
  renderer.fillRoundedRect(x + 1, y + 1, std::min(fill, width - 2), height - 2, (height - 2) / 2,
                           black ? Color::Black : Color::White);
}

void BookOrbitHomeTiles::drawColumnChart(const Rect& area, const uint32_t* values, const int count,
                                         const int emphasized, const char* initials) const {
  const int barWidth = std::min(22, (area.width - (count - 1) * 6) / count);
  const int gap = count > 1 ? (area.width - count * barWidth) / (count - 1) : 0;
  const int labelHeight = 21;
  const int base = area.y + area.height - labelHeight;
  const int chartHeight = base - area.y;
  uint32_t highest = 1;
  for (int i = 0; i < count; i++) highest = std::max(highest, values[i]);
  for (int i = 0; i < count; i++) {
    const int x = area.x + i * (barWidth + gap);
    drawBar(x, base, barWidth, chartHeight, values[i], highest, i == emphasized);
    char initial[5];
    BookOrbitHomeText::copyInitial(initials, i, initial, sizeof(initial));
    renderer.drawText(SMALL_FONT_ID, x + (barWidth - renderer.getTextWidth(SMALL_FONT_ID, initial)) / 2, base, initial);
  }
  renderer.drawLine(area.x, base, area.x + area.width - 1, base);
}

void BookOrbitHomeTiles::drawMetrics(const Rect& tile, const char* const* values, const char* const* labels,
                                     const int count) const {
  drawTileFrame(tile);
  drawMetricColumns(tile, tile.y + (tile.height - 50) / 2, tile.y + 24, tile.y + tile.height - 24, values, labels,
                    count);
}

// Values over their labels, in columns split by rules running from ruleTop to ruleBottom.
void BookOrbitHomeTiles::drawMetricColumns(const Rect& tile, const int valueY, const int ruleTop, const int ruleBottom,
                                           const char* const* values, const char* const* labels,
                                           const int count) const {
  const int columnWidth = (tile.width - 2 * kPad) / count;
  for (int i = 0; i < count; i++) {
    const int x = tile.x + kPad + i * columnWidth;
    if (i > 0) renderer.drawLine(x - 9, ruleTop, x - 9, ruleBottom);
    renderer.drawText(UI_12_FONT_ID, x, valueY, values[i], true, EpdFontFamily::BOLD);
    drawFitted(SMALL_FONT_ID, x, valueY + 30, labels[i], columnWidth - 12);
  }
}

void BookOrbitHomeTiles::drawCover(const RecentBook& book, const Rect& slot, const bool onBlack) const {
  drawCoverFile(coverPath(book, slot.width, slot.height).c_str(), slot, onBlack);
}

void BookOrbitHomeTiles::drawCoverFile(const char* path, const Rect& slot, const bool onBlack) const {
  const uint8_t orientation = static_cast<uint8_t>(renderer.getOrientation());
  const bool inverted = SETTINGS.screenInverted != 0;
  if (path[0] != '\0') {
    for (CachedCover& cover : gCoverCache) {
      if (!sameSlot(cover, path, slot, orientation, onBlack, inverted)) continue;
      if (renderer.copyBufferToRegion(cover.x, cover.y, cover.w, cover.h, cover.data.get(), cover.bytes)) {
        cover.lastUse = ++gCoverUseClock;
        return;
      }
      cover = CachedCover{};
      break;
    }
  }
  if (path[0] != '\0' && Storage.exists(path)) {
    HalFile file;
    if (Storage.openFileForRead("HOME", path, file)) {
      Bitmap bitmap(file);
      if (bitmap.parseHeaders() == BmpReaderError::Ok) {
        const Rect fitted = fittedBitmapRect(bitmap, slot);
        renderer.fillRect(fitted.x, fitted.y, fitted.width, fitted.height, false);
        renderer.drawBitmap(bitmap, fitted.x, fitted.y, fitted.width, fitted.height);
        renderer.drawRect(fitted.x, fitted.y, fitted.width, fitted.height, !onBlack);
        file.close();
        rememberCover(path, slot, fitted, orientation, onBlack, inverted);
        return;
      }
      file.close();
    }
  }
  renderer.fillRect(slot.x, slot.y, slot.width, slot.height, false);
  renderer.drawRect(slot.x, slot.y, slot.width, slot.height, !onBlack);
  if (slot.width >= icon_book_open_32.w + 8) {
    drawLucideIcon(renderer, icon_book_open_32, slot.x + (slot.width - icon_book_open_32.w) / 2,
                   slot.y + (slot.height - icon_book_open_32.h) / 2);
  }
}

void BookOrbitHomeTiles::rememberCover(const char* path, const Rect& slot, const Rect& fitted,
                                       const uint8_t orientation, const bool onBlack, const bool inverted) const {
  if (FREEINK_MCU_C3 || !psramHeapAvailable()) return;
  // The least recently drawn entry makes room; the Home and Library pages show 9 at most.
  CachedCover* target = &gCoverCache[0];
  for (CachedCover& cover : gCoverCache) {
    if (!cover.data) {
      target = &cover;
      break;
    }
    if (cover.lastUse < target->lastUse) target = &cover;
  }
  const size_t bytes = renderer.getRegionByteSize(fitted.x, fitted.y, fitted.width, fitted.height);
  if (bytes == 0) return;
  if (!target->data || target->bytes < bytes) {
    target->data = makePsramByteBufferNoThrow(bytes);
    if (!target->data) {
      LOG_DBG("HOME", "No PSRAM for a cover snapshot (%u bytes)", static_cast<unsigned>(bytes));
      *target = CachedCover{};
      return;
    }
  }
  target->bytes = bytes;
  if (!renderer.copyRegionToBuffer(fitted.x, fitted.y, fitted.width, fitted.height, target->data.get(), bytes)) {
    *target = CachedCover{};
    return;
  }
  target->path = path;
  target->slotX = static_cast<int16_t>(slot.x);
  target->slotY = static_cast<int16_t>(slot.y);
  target->slotW = static_cast<int16_t>(slot.width);
  target->slotH = static_cast<int16_t>(slot.height);
  target->x = static_cast<int16_t>(fitted.x);
  target->y = static_cast<int16_t>(fitted.y);
  target->w = static_cast<int16_t>(fitted.width);
  target->h = static_cast<int16_t>(fitted.height);
  target->orientation = orientation;
  target->onBlack = onBlack;
  target->inverted = inverted;
  target->lastUse = ++gCoverUseClock;
}

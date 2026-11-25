#pragma once

#include <string>

#include "activities/Activity.h"

/**
 * The "Previously..." card: shown over a book reopened after some days away, before its
 * page. It names the book and chapter, how long ago the book was last read, and quotes
 * the last lines read (the end of the page before the resume page). Any button returns
 * to the page; the press that opened the book is ignored.
 */
class ReaderRecapActivity final : public Activity {
 public:
  ReaderRecapActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string bookTitle,
                      std::string chapterTitle, uint32_t daysAway, std::string lastLines)
      : Activity("ReaderRecap", renderer, mappedInput),
        bookTitle(std::move(bookTitle)),
        chapterTitle(std::move(chapterTitle)),
        daysAway(daysAway),
        lastLines(std::move(lastLines)) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isReaderActivity() const override { return true; }
  bool allowPowerAsConfirmInReaderMode() const override { return true; }

 private:
  std::string bookTitle;
  std::string chapterTitle;
  uint32_t daysAway;
  std::string lastLines;
  // Set by a press made while the card is up, so the release of the press that opened
  // the book does not dismiss it.
  bool pressSeen = false;
};

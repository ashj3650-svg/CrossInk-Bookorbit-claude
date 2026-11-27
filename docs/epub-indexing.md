---
title: EPUB Indexing Methods
nav_order: 8.5
---

# EPUB Indexing Methods

Most people never need to change this setting. If a book sits on
an **Indexing** message for a very long time before the first page appears,
switch that book to **Incremental**.

## What is indexing?

Before CrossInk can show a chapter, it has to work out how the text fits onto
pages for your font, size, and margins. This is called indexing. CrossInk saves
the result on the SD card (in the book's cache), so a chapter only has to be
indexed once. Changing the font or spacing means it has to be done again.

**Indexing Method** decides when that work happens:

| Method       | What happens | Good for |
| ------------ | ------------ | -------- |
| **Full Section** (default) | The whole chapter is prepared before you see it. | Most books. After a short wait at the start of a chapter, page turns within that chapter need no further preparation. |
| **Incremental** | Only the pages you need right now are prepared. The rest is prepared in small steps while you read. | Books with enormous chapters, where waiting for a whole chapter is too long. |

## Which should I choose?

- Leave it on **Full Section** if books open at a comfortable speed.
- Switch to **Incremental** if a book pauses for a long time on **Indexing**
  before showing its first page. This is most common with omnibus editions and
  EPUBs that were never split into proper chapters.

With **Incremental**, you may occasionally see **Indexing** pop up if you jump
far ahead, follow a link deep into a chapter, or turn pages faster than
CrossInk can prepare them. That is normal. It is just catching up.

## How to change it

For all books: open **Settings > Reader > Indexing Method** and choose
**Incremental** or **Full Section**.

For one book only: open the Reader Menu, choose the **Settings** tab, then
**Indexing Method**. The choice is saved with that book and does not affect
your other books.

Changing the method does not change the screen you are looking at. It applies
the next time CrossInk needs to index a chapter. It also never changes the EPUB
file itself, your reading progress, bookmarks, clippings, or reading stats.

## If a book is still slow or will not open

Indexing Method changes when the work happens. It does not make the work
smaller. If a difficult book is still slow, or CrossInk runs out of memory on
it, try a lighter [EPUB Render Mode](./epub-render-modes.md), or
[optimize](https://inky.crossink.dev) the EPUB before copying it to the device.

## Index Ahead (X4 Classic, X4 Pro and reTerminal Sticky)

On readers with extra memory (PSRAM), both methods also prepare the chapters
after the one you are reading. Once a page has been on screen for a moment,
CrossInk lays out the next chapter a page at a time, then the one after, up to
three chapters ahead, and saves them to the book's cache like any other
chapter. Reaching them later needs no **Indexing** wait.

A button press always comes first: the work stops between two pages for it,
and continues once you settle on a page again. A chapter you reach before it
is finished keeps the pages already laid out and is completed from there the
usual way. Changing the font, its size, the margins or other layout settings
starts over with the new layout, as any layout change does.

The same readers also read the next page from the cache while you read the
current one and prepare its letters, so a forward page turn only draws it.

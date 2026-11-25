---
title: EPUB Indexing Methods
nav_order: 8.5
---

# EPUB Indexing Methods

Before CrossInk can display an EPUB chapter, it lays out the chapter into
pages and saves that layout in the book's cache. **Indexing Method** chooses
whether CrossInk finishes that work before you start reading the chapter, or
does it a little at a time as you read.

The default is **Full Section**. Most books work well with it. Choose
**Incremental** for a book with unusually large chapters, or when waiting for
an entire chapter to open is more disruptive than seeing a short indexing wait
later.

| Method       | What happens                                                                                                | Main benefit                                                    | Main tradeoff                                                                                                |
| ------------ | ----------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------ |
| Full Section | Builds and caches the whole chapter before it is shown.                                                     | Normal page turns within the chapter do not need more indexing. | A large uncached chapter can take a noticeable time to open.                                                 |
| Incremental  | Builds enough pages to show your current position, then continues in small background steps while you read. | You can begin reading a large chapter sooner.                   | If you reach pages that have not been built yet, CrossInk may briefly show **Indexing** while it catches up. |

## Full Section

Full Section finishes the complete chapter cache before CrossInk displays an
uncached chapter. This is the simpler, more predictable experience for normal
EPUBs: after the initial wait, the chapter's page count and all of its pages
are ready.

CrossInk also tries to build the next chapter while you are reading the
penultimate page of the current one. When this succeeds, moving to the next
chapter does not require a visible indexing wait. It is deliberately
best-effort: on the X3/X4's limited memory, CrossInk skips that background work
when there is not enough free or contiguous memory, and indexes the next
chapter when you enter it instead.

Use Full Section when:

- Most chapters open quickly and you want the smoothest page turns.
- You prefer any indexing wait to happen at the chapter boundary.
- You want a complete chapter cache and final page count as soon as it opens.

Consider Incremental when a particular chapter takes a long time to open, or
when a very large chapter is more likely to stress the reader's available
memory.

## Incremental

Incremental builds only as far as CrossInk needs to display the current page.
It keeps a small number of pages ready ahead of your reading position and uses
short background steps to extend that ready area. Think of it like loading the
first screen of a long web page first, then preparing the rest while you read.

This avoids making you wait for every page in a very large chapter before you
can start. CrossInk saves the pages it has already built, so leaving the book
does not discard that readable progress. A giant chapter may therefore remain
partially indexed until you read farther into it; it does not have to finish in
one continuous session.

Use Incremental when:

- A book pauses for a long time at **Indexing** before its first page appears.
- The book has exceptionally large chapters, such as omnibus editions or
  poorly split EPUBs.
- Starting to read sooner matters more than avoiding an occasional wait later.

Expect a visible **Indexing** popup if you jump far ahead, follow a link to an
unbuilt part of the chapter, or turn pages faster than the background work can
stay ahead. That is normal: CrossInk is building just enough additional pages
to make the requested position readable.

KOReader Sync uses the same content location rather than the other device's
page number. If a synced location is beyond this device's saved incremental
prefix, CrossInk indexes forward until that location is available. Switching
between Incremental and Full Section does not change the saved reading location:
Full Section resolves it as soon as the chapter is built, while Incremental only
builds through the requested content.

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

## Changing The Setting

To change the default for future EPUBs, open **Settings → Reader → Indexing
Method** and choose **Incremental** or **Full Section**.

To change it only for the EPUB you are reading, open the reader menu, choose
the **Settings** tab, then choose **Indexing Method**. The per-book choice is
saved with that book and overrides the global default without changing your
other books.

Changing the method does not discard the chapter currently on screen. The new
choice is used the next time CrossInk needs to index a chapter. It also does
not alter the EPUB file itself, your reading progress, bookmarks, clippings,
or reading statistics.

## If A Book Is Still Slow Or Cannot Index

Indexing Method changes _when_ CrossInk performs layout work; it does not
simplify the publisher's CSS, images, or tables. If a difficult EPUB is still
slow or runs out of memory, try a lighter [EPUB Render Mode](./epub-render-modes.md)
or [optimize](https://inky.crossink.dev) the EPUB before copying it to the device.

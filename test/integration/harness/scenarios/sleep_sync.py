"""This fork's additions to the sleep sync: the BookOrbit Home synced in every sleep sync
(also with no book open, and before any download), Download Missing Books, which puts every
library book missing from the card on it, and the other recent books' positions. Those two
only exist on the PSRAM boards (x4-classic-simulator)."""

from __future__ import annotations

import json
import re
import struct

from formats import read_progress_bin, write_progress_bin
from scenarios.home import HOME_SHELF_KIND, home_snapshot_header, home_snapshot_shelves
from server import load_seed
from sim import LIBRARY, PSRAM_SIM_ENVS, UI_THEME_BOOKORBIT, SimFs, needs, run_simulator, sd_card, sleep_sync


def sleep_sync_with_bookorbit_home(fs: SimFs, top_shelf: str | None = None) -> None:
    """Sync on Sleep on, under the BookOrbit theme."""
    fs.enable_sync_on_sleep()
    fs.update_settings(uiTheme=UI_THEME_BOOKORBIT)
    if top_shelf is not None:
        fs.update_bookorbit(homeShelfTop=top_shelf)


@needs(PSRAM_SIM_ENVS)
def scenario_sleep_sync_home_before_downloads() -> None:
    """The sleep sync refreshes the Home before it downloads new books, so a long download
    cannot cost the Home its sync, then lists the downloads on Just downloaded. The top
    shelf starts as Recently added, which these boards turn into Just downloaded."""
    manifest, _ = load_seed()
    with sd_card(manifest) as fs:
        sleep_sync_with_bookorbit_home(fs, top_shelf=f"{HOME_SHELF_KIND['recently_added']}::")
        # Every book of the library is missing from this card.
        fs.update_bookorbit(autoDownloadNew=True)
        fs.open_book(manifest["progress"][2], progress=(4, 0, 1))

        output = sleep_sync(fs, timeout_s=600)
        assert "New books:" in output, "no book downloads"
        home = output.index("Home sync done")
        assert home < output.index("New books:"), "the Home sync came after the downloads"
        top, _ = home_snapshot_shelves(fs)
        assert top["kind"] == HOME_SHELF_KIND["new_on_reader"], f"Recently added did not become Just downloaded: {top}"
        assert len(top["ids"]) > 0, "Just downloaded does not list the books just downloaded"


@needs(PSRAM_SIM_ENVS)
def scenario_sleep_sync_downloads_missing_books() -> None:
    """Download Missing Books: the first sleep downloads every library book missing from
    the card, newest first; the next finds none missing and leaves a book deleted from the
    card deleted; turning the setting on again brings it back."""
    manifest, _ = load_seed()
    with sd_card(manifest) as fs:
        fs.enable_sync_on_sleep()
        fs.update_bookorbit(autoDownloadNew=True)

        def sleep(timeout_s: int) -> str:
            output = sleep_sync(fs, timeout_s=timeout_s)
            assert "Library:" in output, "no missing-book pass"
            return output

        first = sleep(600)
        library = re.search(r"Library: (\d+) books, (\d+) missing from the card, (\d+) deleted", first)
        assert library, "the pass did not report the library"
        total, missing, deleted = (int(n) for n in library.groups())
        assert total > 20 and missing == total and deleted == 0, f"first pass: {library.group(0)}"
        downloads = re.findall(r"Book (\d+) -> (.+?): result 0$", first, re.MULTILINE)
        assert len(downloads) > 20, f"only {len(downloads)} books downloaded"
        ids = [int(book_id) for book_id, _ in downloads]
        assert len(set(ids)) == len(ids), "a book was downloaded twice"
        for _, path in downloads:
            assert fs.path(path).is_file(), f"{path} is not on the card"
        seen = fs.crosspoint / "bookorbit_auto_download.bin"
        assert seen.is_file(), "the books seen on the card were not saved"
        assert struct.unpack_from("<I", seen.read_bytes(), 12)[0] >= len(downloads), "seen list too short"

        # Delete one: the next sleep must not bring it back, nor download anything else.
        fs.path(downloads[-1][1]).unlink()
        second = sleep(240)
        assert f"Library: {total} books, 0 missing from the card, 1 deleted" in second, \
            "the second pass did not find the library on the card with one book deleted"
        assert "result 0" not in second, "the second pass downloaded a book again"

        # Turning the setting on again forgets that: the deleted book comes back.
        fs.update_bookorbit(autoDownloadNew=True)
        seen.unlink()
        third = sleep(240)
        assert f"Book {downloads[-1][0]} -> {downloads[-1][1]}: result 0" in third, "the deleted book did not come back"


@needs(PSRAM_SIM_ENVS)
def scenario_sleep_sync_recent_books_unchanged() -> None:
    """The sleep sync's other recent books: the first sleep takes their server positions,
    the next finds neither side moved and skips opening them, and a book read on here
    since is sent again."""
    manifest, _ = load_seed()
    open_book, *others = manifest["progress"][2:5]
    with sd_card(manifest) as fs:
        fs.enable_sync_on_sleep()
        open_path = fs.open_book(open_book, progress=(4, 0, 1))
        other_paths = []
        for book in others:
            other_paths.append(f"/books/{book['file'].rsplit('/', 1)[-1]}")
            fs.place(LIBRARY / book["file"], other_paths[-1])
        (fs.crosspoint / "recent.json").write_text(json.dumps(
            {"books": [{"path": path} for path in [open_path, *other_paths]]}))

        def sleep() -> str:
            output = sleep_sync(fs)
            assert "Recent books:" in output, "no recent-book sync"
            return output

        first = sleep()
        for path in other_paths:
            assert f"{path}: took the server position" in first, f"{path} did not take the server position"
        second = sleep()
        for path in other_paths:
            assert f"{path}: neither side moved since the last sync" in second, f"{path} was opened again"
            assert f"{path}: unchanged" in second, f"{path} did not stay unchanged"
        # Read on here: the next sleep must send it, not skip it.
        cache = fs.progress_file(other_paths[0])
        saved = read_progress_bin(cache)
        assert saved, "the first sleep left the book no position"
        spine, count = saved["spine"], saved.get("pageCount", 1)
        write_progress_bin(cache, spine + 1, 0, count)
        state_copy = fs.state_dir(others[0]["hash"]) / "progress.bin"
        if state_copy.exists():
            write_progress_bin(state_copy, spine + 1, 0, count)
        third = sleep()
        assert f"{other_paths[0]}: sent this position" in third, "a book read on here was not sent"


def scenario_sleep_sync_without_open_book() -> None:
    """With no EPUB open, the sleep sync still runs the BookOrbit Home's sync."""
    manifest, _ = load_seed()
    with sd_card(manifest) as fs:
        sleep_sync_with_bookorbit_home(fs)
        output = run_simulator(fs, input_script="6000:POWER:120", choice="none", timeout_s=90)
        assert "Restarting into the BookOrbit sleep sync" in output, "sleep did not hand over to the sleep sync"
        assert "the sleep sync does the library only" in output, "the sleep sync did not run without a book"
        assert "Home sync done" in output, "the Home was not synced"
        assert home_snapshot_header(fs) is not None, "no Home snapshot written"

"""Books on the card: the download index (BOD2, and the BOD1 of earlier releases), the
card scan that files books copied there by other means, and moves through the file transfer
portal that take a book's state along. This fork has no "On device" list: the catalog reads
the index for its book rows' on-device marks."""

from __future__ import annotations

import json
import urllib.parse

from formats import read_progress_bin, write_progress_bin
from server import load_seed, sim_device
from sim import (HOME_TILE_SYNC, LIBRARY, TO_CATALOG, UI_THEME_BOOKORBIT, SimFs, file_transfer_portal, portal_request,
                 presses, run_simulator, sd_card, sleep_sync)

# Home menu -> catalog -> All books (the root's 2nd row): each row's on-device mark reads
# the download index.
CATALOG_ALL_BOOKS = TO_CATALOG + presses(11000, "DOWN", 1) + ["11350:CONFIRM"]


def seed_nested_books(fs: SimFs, count: int) -> list[tuple[int, str, int]]:
    """Copies `count` library EPUBs into series folders under the download folder, where
    no scan looks, so only the download index can list them."""
    _, library = load_seed()
    entries = []
    for i in range(count):
        sd_path = f"{fs.download_folder}/Series {i // 10:02d}/Book {i:03d}.epub"
        dest = fs.place(LIBRARY / library[i % len(library)]["file"], sd_path)
        entries.append((1000 + i, sd_path, dest.stat().st_size))
    return entries


def scenario_catalog_download_index_migration() -> None:
    """An index written by an earlier release (BOD1) is converted the first time the
    catalog reads it: the books it lists, filed in series folders no scan visits, are
    all kept, and the card now holds the same entries as BOD2 with no temporary or
    backup file left behind."""
    manifest, _ = load_seed()
    with sd_card(manifest, download_folder="/Downloads") as fs:
        entries = seed_nested_books(fs, 3)
        fs.write_download_index(entries, version=1)

        script = ";".join(CATALOG_ALL_BOOKS + ["15000:QUIT"])
        output = run_simulator(fs, input_script=script, choice="apply", timeout_s=60)
        assert "Migrated 3 download index entries to v2" in output, \
            "the BOD1 index was not converted with all its books"
        assert fs.read_download_index() == entries, \
            "the migrated index does not hold the BOD1 entries"
        leftovers = sorted(p.name for p in fs.crosspoint.glob("bookorbit_downloads.*")
                           if p.suffix != ".bin")
        assert not leftovers, f"the index rewrite left files behind: {leftovers}"


def scenario_catalog_download_index_beyond_ram_cap() -> None:
    """The index no longer stops at the 128 downloads BOD1 kept, nor at the 512 a device
    without PSRAM kept in RAM: with 520 books in series folders, known only through the
    index, the last entry is All books' first book, whose row is marked on the device and
    whose Confirm opens it instead of downloading it. The visit leaves all 520 entries on
    the card. Before, the oldest were dropped when the index was loaded and saved."""
    manifest, _ = load_seed()
    first_id = sim_device(manifest).catalog_page(size=1, format="epub", sort="title")["items"][0]["id"]
    with sd_card(manifest, download_folder="/Downloads") as fs:
        entries = seed_nested_books(fs, 520)
        entries[-1] = (first_id, *entries[-1][1:])
        fs.write_download_index(entries, version=2)

        script = ";".join(CATALOG_ALL_BOOKS + ["16000:CONFIRM", "24000:QUIT"])
        output = run_simulator(fs, input_script=script, choice="apply", timeout_s=90,
                               after_reboot_script="6000:QUIT")
        assert "Downloading file" not in output, "the book the index's last entry lists was downloaded again"
        state = json.loads((fs.crosspoint / "state.json").read_text())
        assert state.get("openEpubPath") == entries[-1][1], \
            f"Confirm did not open the indexed copy {entries[-1][1]}: {state.get('openEpubPath')}"
        assert fs.read_download_index() == entries, "the visit dropped index entries"


def scenario_sleep_sync_device_scan() -> None:
    """Under the BookOrbit theme, the sleep sync recognises books that reached the card by
    other means, by content hash through the server's match check, and files them where a
    catalog download would have. Three sleep syncs on one card, the download folder being
    the SD root. First: two books copied to /Inbox and the open book move to the paths the
    server's naming template gives them (the open book's state follows it), a book in /Read
    stays there, and an EPUB the server does not have is left alone and remembered. Every
    book identified ends up in the download index; the destinations are read back from that
    index rather than recomputed here. Second, with the index and the scan's state lost (as
    when the server URL changes) and a stray copy of one book added: the books already in
    place are not moved again, the stray copy is deleted and the index is rebuilt. Third,
    with nothing changed: no book is hashed, and only the remembered file's hash goes to the
    server, to learn whether its library changed. Checked against the firmware's
    `Device scan:` log lines."""
    manifest, library = load_seed()
    ids = {f["fileHash"]: item["id"] for item in sim_device(manifest).catalog_manifest()
           for f in item["files"] if f["format"].lower() == "epub"}
    book_a, book_c, book_d = (library[i] for i in (40, 41, 42))  # outside every seeded range
    id_a, id_c, id_d = (ids[b["hash"]] for b in (book_a, book_c, book_d))
    seeded = manifest["progress"][2]
    id_open = ids[seeded["hash"]]

    def scan_lines(output: str) -> str:
        return "\n".join(line for line in output.splitlines() if "Device scan" in line)

    with sd_card(manifest) as fs:
        fs.enable_sync_on_sleep()
        fs.update_settings(uiTheme=UI_THEME_BOOKORBIT)
        open_path = fs.open_book(seeded, progress=(4, 0, 1))

        # 1. Misplaced books move to their catalog location.
        misplaced_a = fs.place(LIBRARY / book_a["file"], "/Inbox/a.epub")
        misplaced_c = fs.place(LIBRARY / book_c["file"], "/Inbox/c.epub")
        fs.place(LIBRARY / book_d["file"], "/Read/d.epub")
        unknown = fs.fs / "Inbox" / "unknown.epub"
        unknown.write_bytes(b"not a BookOrbit book " * 200)

        output = sleep_sync(fs, timeout_s=120)
        assert "Device scan: asked=5 matched=4 moved=3 duplicates=0 remembered=1" in output, \
            "first scan differs:\n" + scan_lines(output)
        index = {book_id: path for book_id, path, _ in fs.read_download_index()}
        assert set(index) == {id_a, id_c, id_d, id_open}, f"download index after the first scan: {index}"
        assert index[id_d] == "/Read/d.epub", f"a book in /Read was moved to {index[id_d]}"
        for original, book_id in ((misplaced_a, id_a), (misplaced_c, id_c), (fs.path(open_path), id_open)):
            assert not original.exists(), f"{original.name} was not moved"
            assert fs.path(index[book_id]).exists(), f"book {book_id} is not at {index[book_id]}"
        state = json.loads((fs.crosspoint / "state.json").read_text())
        assert state["openEpubPath"] == index[id_open], f"the open book's state stayed at {state['openEpubPath']}"
        assert fs.epub_cache_dir(index[id_open]).exists(), "the open book's cache did not follow it"
        assert unknown.exists(), "an EPUB the server does not have was touched"

        # 2. With the index and the scan state lost, books in place stay put and a stray copy goes.
        fs.download_index.unlink()
        (fs.crosspoint / "bookorbit_recognition.bin").unlink()
        stray_c = fs.place(LIBRARY / book_c["file"], "/Inbox/c copy.epub")
        output = sleep_sync(fs, timeout_s=120)
        assert "Device scan: asked=6 matched=5 moved=0 duplicates=1 remembered=1" in output, \
            "second scan differs:\n" + scan_lines(output)
        assert not stray_c.exists(), "the stray copy of an in-place book was not deleted"
        rebuilt = {book_id: path for book_id, path, _ in fs.read_download_index()}
        assert rebuilt == index, f"the rebuilt index differs: {rebuilt} vs {index}"
        leftovers = sorted(p.name for p in fs.crosspoint.glob("bookorbit_scan*"))
        assert not leftovers, f"the scan left files behind: {leftovers}"

        # 3. Nothing new: nothing is hashed, and one bare hash asks whether the library changed.
        output = sleep_sync(fs, timeout_s=120)
        assert "new books of" not in output and "Device scan: asked=" not in output, \
            "an unchanged card was scanned again:\n" + scan_lines(output)
        probes = [line for line in output.splitlines() if "Match check response" in line]
        assert len(probes) == 1 and "(1 hashes)" in probes[0], f"unexpected match checks: {probes}"


def scenario_home_refresh_device_scan() -> None:
    """The Home tile's sync runs the card scan too, in its own network boot: a library book
    copied to /Inbox moves to its catalog location, a second copy of it is deleted, and the
    sync ends on a summary of what the scan did before it restarts to Home."""
    manifest, library = load_seed()
    book = library[43]  # outside every seeded range but the collection's
    book_id = next(item["id"] for item in sim_device(manifest).catalog_manifest()
                   for f in item["files"] if f["fileHash"] == book["hash"])

    with sd_card(manifest) as fs:
        fs.write_settings(uiTheme=UI_THEME_BOOKORBIT)
        copies = [fs.place(LIBRARY / book["file"], f"/Inbox/{name}") for name in ("copied.epub", "copied (1).epub")]

        output = run_simulator(fs, input_script=HOME_TILE_SYNC, choice="none", timeout_s=25)
        assert "Device scan: asked=2 matched=2 moved=1 duplicates=1 remembered=0" in output, \
            "the Home refresh's scan differs:\n" + "\n".join(
                line for line in output.splitlines() if "Device scan" in line)
        assert "Home refresh: showing the card scan's summary" in output, "the refresh ended without its summary"
        index = {i: p for i, p, _ in fs.read_download_index()}
        assert book_id in index and fs.path(index[book_id]).exists(), f"the book was not filed: {index}"
        assert not any(copy.exists() for copy in copies), "a copy stayed in /Inbox"


def scenario_web_move_keeps_book_state() -> None:
    """Moving a book from the file transfer page (POST /move) or over WebDAV (MOVE) carries
    its path-keyed state to the new path: the reading position in its render cache and its
    recent entry. Both used to clear the cache and leave that state behind. WebDAV still
    moves a book the state cannot follow, such as a client renaming x.epub to x.epub.bak
    before saving over it. The simulator boots straight into the portal."""
    manifest, books = load_seed()
    position = (3, 7, 20)
    names = ("Portal.epub", "Dav.epub", "Backup.epub")

    with sd_card(manifest) as fs:
        (fs.fs / "Moved").mkdir()
        recent = []
        for index, name in enumerate(names):
            sim_path = f"/books/{name}"
            fs.place(LIBRARY / books[index]["file"], sim_path)
            write_progress_bin(fs.progress_file(sim_path), *position)
            recent.append({"path": sim_path, "title": f"Title of {name}", "author": "Author"})
        (fs.crosspoint / "recent.json").write_text(json.dumps({"books": recent}))

        with file_transfer_portal(fs) as url:
            form = urllib.parse.urlencode({"path": "/books/Portal.epub", "dest": "/Moved"}).encode()
            status, body = portal_request("POST", url + "/move", form,
                                          {"Content-Type": "application/x-www-form-urlencoded"})
            assert status == 200, f"portal move failed: {status} {body}"
            status, body = portal_request("MOVE", url + "/books/Dav.epub",
                                          headers={"Destination": url + "/Moved/Dav.epub", "Overwrite": "F"})
            assert status == 201, f"WebDAV move failed: {status} {body}"
            status, body = portal_request("MOVE", url + "/books/Backup.epub",
                                          headers={"Destination": url + "/books/Backup.epub.bak"})
            assert status == 201, f"WebDAV move to a non-book name failed: {status} {body}"

        recent_paths = [b["path"] for b in json.loads((fs.crosspoint / "recent.json").read_text())["books"]]
        for name in names[:2]:
            old, new = f"/books/{name}", f"/Moved/{name}"
            assert fs.path(new).exists() and not fs.path(old).exists(), f"{name} did not move"
            progress = read_progress_bin(fs.progress_file(new))
            assert progress and (progress["spine"], progress["page"]) == position[:2], \
                f"{name}'s reading position did not follow it: {progress}"
            assert not fs.epub_cache_dir(old).exists(), f"{name}'s old cache was left behind"
            assert new in recent_paths and old not in recent_paths, \
                f"{name}'s recent entry did not follow it: {recent_paths}"
        assert fs.path("/books/Backup.epub.bak").exists() and not fs.path("/books/Backup.epub").exists(), \
            "the WebDAV rename to a non-book name did not happen"

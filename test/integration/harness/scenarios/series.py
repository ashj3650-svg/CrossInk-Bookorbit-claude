"""Next in series: at the end of a book, the End-of-Book screen offers the series' next
book, opening it when the card has it and downloading it from BookOrbit when not."""

from __future__ import annotations

import json
import shutil
import zipfile
from pathlib import Path

from formats import write_progress_bin
from scenarios.book_update import epub_spine_entries
from server import load_seed, sim_device
from sim import LIBRARY, PSRAM_SIM_ENVS, SimFs, needs, run_simulator, sd_card


def numbered_copy(source: Path, dest: Path, series: str, number: int) -> None:
    """A copy of a library book whose OPF numbers it in a series, as Calibre writes it. The
    server's own copies carry no series: seed.py sets theirs through its API."""
    dest.parent.mkdir(parents=True, exist_ok=True)
    meta = (f'<meta name="calibre:series" content="{series}"/>'
            f'<meta name="calibre:series_index" content="{number}"/></metadata>').encode()
    with zipfile.ZipFile(source) as src, zipfile.ZipFile(dest, "w") as out:
        for info in src.infolist():
            data = src.read(info.filename)
            if info.filename.endswith(".opf"):
                data = data.replace(b"</metadata>", meta, 1)
            out.writestr(info, data, compress_type=info.compress_type)


def finish_book(fs: SimFs, sim_path: str, source: Path) -> None:
    """Asleep on the book's End-of-Book screen: its progress one past the last chapter."""
    fs.set_asleep_in_book(sim_path)
    write_progress_bin(fs.progress_file(sim_path), len(epub_spine_entries(source)), 0, 1)


def open_book_after(fs: SimFs) -> str:
    return json.loads((fs.crosspoint / "state.json").read_text()).get("openEpubPath", "")


def scenario_series_next_on_card() -> None:
    """The End-of-Book screen's first row is the series' next book when the card has it,
    found through the Library index wherever it is filed, ahead of the book after this one
    in its folder; Confirm opens it."""
    manifest, library = load_seed()
    series = manifest["series"]["name"]
    with sd_card(manifest) as fs:
        first = fs.path("/books/a-first.epub")
        numbered_copy(LIBRARY / library[64]["file"], first, series, 1)
        numbered_copy(LIBRARY / library[65]["file"], fs.path("/Saga/second.epub"), series, 2)
        # Alphabetically next in the first book's folder, which the series row comes before.
        shutil.copy2(LIBRARY / library[66]["file"], fs.path("/books/b-other.epub"))

        # Home menu -> Library, whose first open builds the index the lookup reads (in any
        # order but Recently Read, which needs no index).
        fs.write_settings(librarySortMethod=1)
        run_simulator(fs, input_script="3000:DOWN:120;3600:CONFIRM:120;12000:QUIT", choice="none", timeout_s=30)
        assert (fs.crosspoint / "library.idx").exists(), "the Library built no index"

        finish_book(fs, "/books/a-first.epub", first)
        run_simulator(fs, input_script="6000:CONFIRM:120;10000:QUIT", choice="none", timeout_s=30,
                      wake_into_reader=True)
        assert open_book_after(fs) == "/Saga/second.epub", f"opened {open_book_after(fs)!r}, not the next in series"


@needs(PSRAM_SIM_ENVS)
def scenario_series_next_download() -> None:
    """With the series' next book not on the card, the End-of-Book screen offers it from
    BookOrbit: Confirm looks the series up, downloads the book numbered next and opens it.
    After the series' last book, the lookup finds nothing and downloads nothing."""
    manifest, library = load_seed()
    series = manifest["series"]["name"]
    next_id = next(item["id"] for item in sim_device(manifest).catalog_manifest()
                   for f in item["files"] if f["fileHash"] == library[65]["hash"])
    with sd_card(manifest) as fs:
        first = fs.path("/books/a-first.epub")
        numbered_copy(LIBRARY / library[64]["file"], first, series, 1)
        finish_book(fs, "/books/a-first.epub", first)

        # The download ends in a restart into the reader, which the second script quits.
        output = run_simulator(fs, input_script="6000:CONFIRM:120", choice="none", timeout_s=90,
                               wake_into_reader=True, after_reboot_script="8000:QUIT")
        index = {i: p for i, p, _ in fs.read_download_index()}
        assert next_id in index, f"book {next_id} was not downloaded: {index}\n" + "\n".join(
            line for line in output.splitlines() if "BookOrbit" in line)[-2000:]
        assert fs.path(index[next_id]).exists(), "the download index points at no file"
        assert open_book_after(fs) == index[next_id], f"opened {open_book_after(fs)!r}, not the download"

    with sd_card(manifest) as fs:
        last = fs.path("/books/c-last.epub")
        numbered_copy(LIBRARY / library[66]["file"], last, series, 3)
        finish_book(fs, "/books/c-last.epub", last)
        output = run_simulator(fs, input_script="6000:CONFIRM:120;12000:QUIT", choice="none", timeout_s=30,
                               wake_into_reader=True)
        assert f'No book after #3 in series "{series}"' in output, "the lookup did not come back empty"
        assert "Downloading file" not in output, "a book was downloaded after the series' last"

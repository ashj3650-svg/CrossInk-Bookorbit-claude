"""Instant Wake (x4-classic-simulator only): a sleep starts as a light sleep with no sync,
Power wakes it in place, and only once its time is up does the device finish the sleep the
usual way (the BookOrbit sleep sync, or deep sleep). CROSSINK_SIM_INSTANT_WAKE_MS shortens
its minutes."""

from __future__ import annotations

import struct
import time

from server import load_seed, peer_device
from sim import INSTANT_WAKE_SIM_ENVS, LIBRARY, SIM_DEVICE_ID, needs, run_simulator, sd_card


def wake_ms(ms: int) -> dict[str, str]:
    return {"CROSSINK_SIM_INSTANT_WAKE_MS": str(ms)}


@needs(INSTANT_WAKE_SIM_ENVS)
def scenario_instant_wake_hands_off_to_sleep_sync() -> None:
    """A sleep first only light-sleeps, with no sync, and when its time is up the device
    finishes the sleep the usual way, here the BookOrbit sleep sync, which uploads the
    further local position."""
    manifest, _ = load_seed()
    seeded = manifest["progress"][2]
    book_hash = seeded["hash"]
    peer = peer_device(manifest)
    peer.put_progress(book_hash, seeded["progress"], seeded["percentage"])

    with sd_card(manifest) as fs:
        fs.enable_sync_on_sleep()
        fs.enable_instant_wake(60)
        fs.open_book(seeded, progress=(4, 0, 1))
        fs.write_sync_marker(book_hash, int(time.time()) + 3600)

        output = run_simulator(fs, input_script="6000:POWER:120", choice="none", timeout_s=90,
                               extra_env=wake_ms(5000))
        light = output.find("Instant Wake: light sleep for up to 5 s, then the BookOrbit sleep sync")
        assert light >= 0, "the sleep did not start as a light sleep with the sync to follow"
        time_up = output.find("Instant Wake: time up after", light)
        assert time_up > light, "the light sleep never handed off"
        restart = output.find("Restarting into the BookOrbit sleep sync")
        assert restart > time_up, "the sleep sync started before the light sleep was over"
        assert "Sleep sync over" in output[restart:], "sleep sync never ran to its end"
        assert "sync scenario finished" in output, "sleep sync never reached its end marker"
        assert "woken by Power" not in output, "nothing pressed Power, yet the device woke"

    remote = peer.get_progress(book_hash)
    assert float(remote.get("percentage", 0)) > 0.5, f"server progress not advanced: {remote}"
    assert remote.get("device_id") == SIM_DEVICE_ID, f"unexpected device id: {remote}"


@needs(INSTANT_WAKE_SIM_ENVS)
def scenario_instant_wake_round_trip() -> None:
    """The whole cycle in one process: asleep in the book, Power wakes it back into the
    reader without a restart and without syncing; asleep again, the time runs out and the
    BookOrbit sleep sync runs, uploading the position."""
    manifest, _ = load_seed()
    seeded = manifest["progress"][2]
    book_hash = seeded["hash"]
    peer = peer_device(manifest)
    peer.put_progress(book_hash, seeded["progress"], seeded["percentage"])

    with sd_card(manifest) as fs:
        fs.enable_sync_on_sleep()
        fs.enable_instant_wake(60)
        sim_path = fs.open_book(seeded, progress=(4, 0, 1))
        fs.set_asleep_in_book(sim_path)
        fs.write_sync_marker(book_hash, int(time.time()) + 3600)

        # 8 s: sleep from the reader. 11 s: Power, 3 s into a 6 s light sleep, wakes it.
        # 17 s: sleep again; this time the 6 s run out and the sleep sync follows.
        output = run_simulator(fs, input_script="8000:POWER:120;11000:POWER:120;17000:POWER:120",
                               choice="none", timeout_s=120, wake_into_reader=True, extra_env=wake_ms(6000))
        first_reader = output.find("Entering activity: EpubReader")
        assert first_reader >= 0, "the run never opened the reader"
        first_sleep = output.find("Instant Wake: light sleep for up to 6 s", first_reader)
        assert first_sleep > first_reader, "the first sleep was not a light sleep"
        woken = output.find("Instant Wake: woken by Power", first_sleep)
        assert woken > first_sleep, "Power did not wake the device from the light sleep"
        assert "Restarting into the BookOrbit sleep sync" not in output[:woken], \
            "the device synced before it was woken: the light sleep must not sync"
        sync_restart = output.find("Restarting into the BookOrbit sleep sync")
        restarts_before_sync = output[:sync_restart].count("Reset diagnostic")
        assert restarts_before_sync == 1, "the device restarted between the light sleep and the sync"
        back_in_book = output.find("Entering activity: EpubReader", woken)
        assert back_in_book > woken, "the wake did not land back in the book"
        second_sleep = output.find("Instant Wake: light sleep for up to 6 s", back_in_book)
        assert second_sleep > back_in_book, "the second sleep was not a light sleep"
        time_up = output.find("Instant Wake: time up after", second_sleep)
        assert time_up > second_sleep, "the second light sleep never handed off"
        restart = output.find("Restarting into the BookOrbit sleep sync", time_up)
        assert restart > time_up, "the hand-off did not start the sleep sync"
        assert "Sleep sync over" in output[restart:], "sleep sync never ran to its end"
        assert "sync scenario finished" in output, "sleep sync never reached its end marker"

    remote = peer.get_progress(book_hash)
    assert float(remote.get("percentage", 0)) > 0.5, f"server progress not advanced: {remote}"
    assert remote.get("device_id") == SIM_DEVICE_ID, f"unexpected device id: {remote}"


@needs(INSTANT_WAKE_SIM_ENVS)
def scenario_instant_wake_without_sync_deep_sleeps() -> None:
    """With Sync on Sleep off, the hand-off goes straight to deep sleep, which in the
    simulator only ends with a press that relaunches the process as a power-button wake,
    the proof that the device really reached deep sleep."""
    manifest, _ = load_seed()
    with sd_card(manifest) as fs:
        fs.enable_sync_on_sleep()
        fs.update_bookorbit(syncOnSleep=False)
        fs.enable_instant_wake(15)

        # 6 s: sleep from Home. The 3 s light sleep ends; 14 s: Power wakes the deep sleep.
        output = run_simulator(fs, input_script="6000:POWER:120;14000:POWER:120", choice="none",
                               timeout_s=24, extra_env=wake_ms(3000))
        light = output.find("Instant Wake: light sleep for up to 3 s")
        assert light >= 0, "the sleep did not start as a light sleep"
        assert "then the BookOrbit sleep sync" not in output, "a sync was planned with Sync on Sleep off"
        time_up = output.find("Instant Wake: time up after", light)
        assert time_up > light, "the light sleep never handed off"
        assert "Restarting into the BookOrbit sleep sync" not in output, "a sleep sync ran with Sync on Sleep off"
        assert "woken by Power" not in output, "the press woke the light sleep instead of the deep sleep"
        assert "Wake route: PowerButton" in output[time_up:], "the device never deep-slept and woke from it"


@needs(INSTANT_WAKE_SIM_ENVS)
def scenario_instant_wake_keeps_pin_lock() -> None:
    """A wake from the light sleep lands on the Device PIN lock when a PIN is set, as a
    deep-sleep wake does: carrying on without a restart must not skip it."""
    manifest, _ = load_seed()
    with sd_card(manifest) as fs:
        fs.enable_sync_on_sleep()
        fs.enable_instant_wake(60)
        # DevicePinStore's record: "CPIN", version 1, salt, hash (no PIN is ever entered).
        (fs.crosspoint / "device_pin.bin").write_bytes(struct.pack("<IHHII", 0x4E495043, 1, 0, 1, 2))

        output = run_simulator(fs, input_script="3000:POWER:120;6000:POWER:120;10000:QUIT", choice="none",
                               timeout_s=20, extra_env=wake_ms(60000))
        woken = output.find("Instant Wake: woken by Power")
        assert woken >= 0, "Power did not wake the device from the light sleep"
        after = output[woken:]
        assert "Entering activity: DevicePin" in after, "the wake skipped the PIN lock"
        assert "Entering activity: Home" not in after, "Home opened behind the PIN lock's back"


@needs(INSTANT_WAKE_SIM_ENVS)
def scenario_instant_wake_after_auto_sleep() -> None:
    """The inactivity timer's sleep goes through Instant Wake too, with Quick Resume after a
    timeout keeping the page: Power brings the reader back, and the wake restarts the
    inactivity timer rather than sleeping again at once."""
    manifest, _ = load_seed()
    with sd_card(manifest) as fs:
        fs.enable_sync_on_sleep()
        fs.enable_instant_wake(60)
        fs.update_settings(sleepTimeoutMinutes=1, quickResumeSleepScreen=1)  # QUICK_RESUME_AFTER_TIMEOUT
        sim_path = f"/books/{manifest['progress'][3]['file'].rsplit('/', 1)[-1]}"
        fs.place(LIBRARY / manifest["progress"][3]["file"], sim_path)
        fs.set_asleep_in_book(sim_path)

        # The timer sleeps the reader at about 60 s; Power at 66 s wakes it; the run ends at
        # 90 s, well inside the next minute of the restarted timer.
        output = run_simulator(fs, input_script="66000:POWER:120;90000:QUIT", choice="none", timeout_s=110,
                               wake_into_reader=True, extra_env=wake_ms(600000))
        requested = output.find("Instant Wake: sleep requested (timeout)")
        assert requested >= 0, "the inactivity timer did not ask for an Instant Wake sleep"
        woken = output.find("Instant Wake: woken by Power", requested)
        assert woken > requested, "Power did not wake the device from the light sleep"
        assert "Entering activity: EpubReader" in output[woken:], "the wake did not reopen the book"
        assert output.count("Instant Wake: sleep requested") == 1, "the device fell asleep again right after the wake"

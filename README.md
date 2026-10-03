# IRcito v1.5 — utilities

IRcito is an IR learner and remote-control firmware for the M5StickS3, designed as an **application image**
for an EasyLauncher app partition. GPIO42 RX, GPIO46 TX, ESP32-S3 RMT at 1 MHz,
8 MB flash / OPI PSRAM, 256-symbol RX buffer, 12 ms frame gap, EXT_5V, speaker
amp handling, IR normalization/deglitch/decoders and waveform timing remain
as in the physically proven v1.4 build. `src/ir_pipeline.h` is byte-for-byte
unchanged. Small RMT and storage buffers use internal RAM; no PSRAM is required.

## Controls

Home: **B** advances through Learn, Remotes, Macros, TV-B-Gone, Settings, About;
**A** opens the selection. After Learn, **short A** tests, **hold A ~800 ms**
opens Save / Remote, **short B** changes the RAW carrier (36/38/40 kHz) or
captures again for recognized protocols; **hold B** captures again for RAW or
returns Home for recognized protocols. The labels are shown on screen.

Save: choose Remote 1–4, then Button 1–4. Existing slots show an explicit
Overwrite? Yes/No prompt, default No. Remotes opens each saved Button with
Test, Rename, Delete and Back. **Short A** on Test sends the same frame as
v1.4; **hold A** continues protocol-aware repetition while pressed. A Sony
hold transmits individual canonical frames at ~45 ms start-to-start; NEC
uses the standard 9 ms / 2.25 ms / 560 µs repeat frame at ~110 ms. RAW holds
only repeat if a valid repeatCount and repeatPeriodUs policy is present;
old v1.4 RAW records (count 1, period 0) do not loop. Releasing A stops
new transmissions. Settings → Erase all learned
requires Yes/No confirmation. TV-B-Gone offers Start, Region (North America
or Europe), Back. B cancels sending between codes.

## Storage and replay

`src/storage.{h,cpp}` uses **only** the NVS namespace `ircito` via Preferences.
No partition table or filesystem is installed or modified. The build's
`default_8MB.csv` declares a shared NVS partition of **0x5000 = 20 KiB**;
EasyLauncher may have a different actual table. At boot, the app checks that
the running system has a partition labelled `nvs`, prints its size and free
NVS entries, and disables saving gracefully if unavailable. If NVS fills,
the old signal remains active and an error is logged.

There are **4 remotes × 4 buttons = 16 slots** and a conservative **4096-byte
total RAW payload budget** across those slots. The unchanged 256-symbol RX
buffer accepts at most 255 symbols per frame (1020 bytes), subject to the budget and
real NVS free space. Sixteen captures of 59 symbols use 3776 RAW bytes; four
near-maximal captures also fit. Recognized signals store small canonical
metadata and include their original RAW as a backup if it fits the budget;
otherwise only the optional backup is omitted. The NVS blob length is variable
and has a magic, format version, length, CRC32, bounded count and validated
protocol/carrier/command/repeat fields. An index selects an A/B blob per slot:
write the inactive blob, read it back and check CRC, commit the index, then
delete the previous blob. A failed overwrite retains the former active slot.
After power loss an unreferenced blob may remain and be replaced by a future
save to that slot. Delete removes only one slot; Erase all clears only
`ircito`, never other NVS namespaces.

`src/main.cpp` retains the IR hardware functions and uses **one** `replay()`
path for a fresh capture or a loaded button. Sony SIRC12 is regenerated at
40 kHz, 33% duty, three frames ~45 ms start-to-start. Standard NEC remains
38 kHz. Unknown RAW uses the unchanged RX→TX level inversion with unchanged
durations and the selected/stored 36, 38 or 40 kHz carrier. The original RX
buffer is never normalized in place.

## TV-B-Gone

`src/tvbgone_data.cpp` is regenerated from the attributed open-source
`third_party/tvbgone/WORLD_IR_CODES.h` with `tools/generate_tvbgone.py`.
It contains 137 entries per region, including shared entries. Source,
license and attribution: `third_party/tvbgone/NOTICE.md` (CC BY-SA 2.5).
The independent encoder decompresses each code's original MSB-first timing
indices (10 µs units) into RMT symbols, splitting durations above the 15-bit
RMT limit without changing their total. It uses the learner's **existing TX
channel and EXT_5V setup**, switches each code's carrier with
`rmt_apply_carrier()`, and disables carrier for the four unmodulated entries.
Codes are sent one at a time with the original 205 ms inter-code gap. B can
cancel between codes (longest individual code in the database: 334 ms).
No TV-B-Gone code is persisted in NVS.

## Names, macros, and USB backup

The plain text editor starts with an empty entry and shows the old name in
the preceding menu. **Short B** cycles A–Z, a–z, 0–9, space, hyphen,
underscore, plus; **short A** appends the selected character; **hold A**
commits a nonempty name, **hold B** cancels without writing. Names are capped
at 19 characters plus NUL. This deliberately simple two-button editor is
slow for long names; USB backup/import can also provide names.

Macros contains four named macros, each with up to eight references to an
existing Remote/Button slot. Edit selects a step, changes remote/button and
delay (0, 250, 500, 1000, 2000 ms), then Save. Delete/Back removes an
existing step. Run loads each button and calls the same `replay()` function;
B cancels between steps. A missing reference aborts and logs the slot.
Macro records use separate `m0a/b/i` … `m3a/b/i` keys in `ircito`, with a
version, CRC and A/B commit. They do not count against the 4096-byte RAW
payload budget.

Remote names reuse the original `r0`…`r3` v1.4 record layout. Signal names
reuse `name[20]` in the original `s00a/b` v1.4 Header, with the original
magic/version/CRC and A/B index. Neither saved signals nor the EasyLauncher
partition table are migrated or erased when upgrading. `src/ir_pipeline.h`
remains byte-for-byte unchanged from v1.4.

Settings → Export Backup writes `[EXPORT] BEGIN`, then a complete versioned
backup, then `[EXPORT] END` to USB Serial at 115200. Copy only the lines from
`IRCITO-BACKUP|1` through `END` into a text file. Settings → Import Backup
accepts those lines pasted over USB Serial. Each complete and valid record
commits to its own existing NVS slot; a truncated stream leaves unvisited
slots unchanged. Import merges/overwrites listed slots and never calls
`eraseAll()`. Details: [docs/BACKUP.md](docs/BACKUP.md).

## Build and install

Run `pio run -e m5stack-sticks3` (Arduino-ESP32 3.3.6). Select only
`.pio/build/m5stack-sticks3/firmware.bin` as an **application binary** in
EasyLauncher. Do not flash it at offset `0x0` or install the build's
bootloader/partition table; the running EasyLauncher layout is retained.
USB Serial: **115200 baud**.

Host checks (no ESP32 needed):

```sh
g++ -std=c++17 -Wall -Wextra -Werror test/pipeline_host.cpp -o /tmp/pipeline && /tmp/pipeline
g++ -std=c++17 -Wall -Wextra -Werror -Itest/stubs test/storage_host.cpp src/storage.cpp -o /tmp/storage && /tmp/storage
g++ -std=c++17 -Wall -Wextra -Werror -Itest/stubs test/tvbgone_host.cpp src/tvbgone.cpp src/tvbgone_data.cpp -o /tmp/tvbgone && /tmp/tvbgone
g++ -std=c++17 -Wall -Wextra -Werror -Itest/stubs test/macro_host.cpp src/macros.cpp src/storage.cpp -o /tmp/macro && /tmp/macro
g++ -std=c++17 -Wall -Wextra -Werror -Itest/stubs test/backup_host.cpp src/backup.cpp src/macros.cpp src/storage.cpp -o /tmp/backup && /tmp/backup
g++ -std=c++17 -Wall -Wextra -Werror -Itest/stubs test/hold_host.cpp -o /tmp/hold && /tmp/hold
```

The host checks cover Sony/NEC decoding, transactional storage failures,
corruption and namespace isolation, and all 274 regional TV-B-Gone entries.

IRcito v1.5 has also been physically validated on M5StickS3 hardware,
including Learn/replay, persistent saved remotes, Sony SIRC12,
NEC/NEC-like operation, RAW fallback, hold/repeat, macros, rename,
USB backup/import and TV-B-Gone.

This does not imply compatibility with every IR device or protocol.


## Development

IRcito is an AI-assisted firmware project.

Project direction, requirements, hardware testing, debugging, integration and
physical validation were carried out by **Nara Sofía García Ramírez**.

Code implementation and review were assisted by **OpenAI ChatGPT Work**.

The project was developed iteratively using real IR captures, serial logs,
physical hardware testing and regression tests.

## License

Original IRcito source code is released under the MIT License. See `LICENSE`.

Third-party components and datasets retain their respective licenses and
attribution requirements. In particular, the TV-B-Gone dataset is not covered
by IRcito's MIT license. See `third_party/tvbgone/NOTICE.md`.

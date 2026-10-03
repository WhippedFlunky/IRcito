# IRcito technical notes

Implementation details for IRcito v1.5. For installation and everyday use, see the [README](../README.md).

## Contents

- [Design principles](#design-principles)
- [Storage](#storage)
- [IR replay](#ir-replay)
- [TV-B-Gone](#tv-b-gone)
- [Macros](#macros)
- [v1.4 compatibility](#v14-compatibility)
- [Backup and import](#backup-and-import)
- [Tests](#tests)

## Design principles

- IR RX on GPIO42 and IR TX on GPIO46, using the ESP32-S3 RMT peripheral at 1 MHz.
- 256-symbol RX buffer, 12 ms frame gap, EXT_5V handling, speaker amplifier handling, IR normalization/deglitch pipeline, protocol decoders and waveform timing are unchanged from the physically proven v1.4 build.
- Small RMT and storage buffers use internal RAM, so critical IR functionality does not depend on PSRAM.
- The application image never installs, replaces or modifies a partition table or filesystem.

## Storage

`src/storage.{h,cpp}` uses only the NVS namespace `ircito`, through ESP32 Preferences.

### NVS partition

The build configuration declares a shared NVS partition of `0x5000` (20 KiB), but the actual partition layout, NVS size and application offset depend on the firmware or launcher already installed on the device.

At boot, IRcito checks that the running system contains a partition labelled `nvs` and reports its size and available entries over Serial. If suitable NVS storage is unavailable, saving is disabled gracefully. If NVS becomes full, the previous stored signal stays active and an error is logged.

### RAW payload budget

IRcito uses a conservative budget of **4096 bytes of total RAW payload** across all saved slots.

The unchanged 256-symbol RX buffer accepts up to 255 symbols per frame, roughly 1020 bytes for a near-maximal capture. For comparison, 16 captures of 59 symbols each come to about 3776 RAW bytes.

Recognized signals store compact canonical metadata and also keep their original RAW capture as a backup when space permits. If the budget would be exceeded, the optional RAW backup is omitted and the canonical decoded signal is preserved.

### Record format

Each saved signal record includes:

- magic
- format version
- length
- CRC32
- protocol
- carrier
- command and address information
- repeat metadata
- bounded RAW count

### Transactional writes

Each signal slot uses two alternating blobs plus an index. A save proceeds as:

```
write inactive blob
        ↓
read back
        ↓
validate CRC and data
        ↓
commit new index
        ↓
delete old blob
```

A failed overwrite leaves the previous active signal intact. After an unexpected power loss, an unreferenced blob may remain, but a later save to the same slot can replace it.

Delete removes only the selected slot. **Erase all learned** clears only IRcito's own NVS namespace and never touches unrelated ESP32 NVS data.

## IR replay

`src/main.cpp` uses a single `replay()` path for both freshly captured signals and loaded saved buttons.

### Sony SIRC12

Regenerated canonically with a 40 kHz carrier, 33% duty and three transmissions at roughly 45 ms start-to-start.

### NEC

Standard NEC replay at 38 kHz. Hold/repeat uses the standard repeat frame (9 ms mark, 2.25 ms space, 560 µs mark) at roughly 110 ms intervals.

### RAW

Unknown signals use the preserved original RAW durations. The built-in demodulating receiver is active-low while the transmitter uses active-high carrier gating, so RAW replay applies the established RX → TX level inversion. Available carriers are 36, 38 and 40 kHz. The original RX capture is never normalized destructively in place.

### Hold / repeat

- **Sony SIRC12:** repeated canonical frames at roughly 45 ms start-to-start.
- **NEC:** the standard repeat frame described above.
- **RAW:** repeats only when the record contains a valid repeat policy. Old v1.4 RAW records with `repeatCount = 1` and `repeatPeriodUs = 0` do not loop automatically.

Releasing A stops further transmissions.

## TV-B-Gone

The TV-B-Gone feature is based on an attributed open-source dataset:

| File | Role |
|---|---|
| `third_party/tvbgone/WORLD_IR_CODES.h` | Source dataset |
| `src/tvbgone_data.cpp` | Generated firmware representation |
| `tools/generate_tvbgone.py` | Regenerates the file above |
| `third_party/tvbgone/NOTICE.md` | Licensing and attribution |

The dataset is licensed under **CC BY-SA 2.5** and is not covered by IRcito's MIT license.

The database holds 137 North America entries and 137 Europe entries, including shared ones. The encoder decompresses the original MSB-first timing indices (10 µs units) into RMT symbols. Durations beyond the RMT 15-bit limit are split into several symbols without changing total timing.

TV-B-Gone reuses IRcito's RMT TX channel, GPIO46 and EXT_5V handling, and changes the carrier dynamically with `rmt_apply_carrier()`. Four unmodulated entries are sent with the carrier disabled. Codes go out one at a time with the original ~205 ms inter-code gap, and **B** cancels between codes. The longest code in the database lasts about 334 ms.

TV-B-Gone data lives in firmware flash and is never written to NVS.

## Macros

Each macro supports up to 8 steps. A step references an existing remote and button instead of duplicating the stored signal. Delays between steps: 0, 250, 500, 1000 or 2000 ms.

Execution loads each referenced signal and calls the same `replay()` function used everywhere else. **B** cancels between steps. If a referenced button no longer exists, execution stops and logs `[MACRO] missing`.

Macros are stored in the IRcito NVS namespace under keys such as `m0a`, `m0b`, `m0i` through `m3a`, `m3b`, `m3i`. Each record has a format version, a CRC and an A/B transactional commit. Macro data does not count against the 4096-byte RAW budget.

## v1.4 compatibility

v1.5 keeps the v1.4 signal and remote storage format:

- remote names under keys `r0` to `r3`
- signal records in the original layout (`s00a`, `s00b`, ...) with the existing A/B index mechanism

Existing v1.4 Sony, NEC and RAW signals load in v1.5 without migration. Neither saved signals nor the device's partition table are migrated or erased on upgrade.

`src/ir_pipeline.h` is byte-for-byte identical to v1.4:

```
SHA-256: 65a97ce85edb568bd26d30c7f1588d2e6fe680b02e11b6cb3991d0addae47770
```

## Backup and import

Settings → **Export Backup** writes a complete versioned backup to USB Serial at 115200 baud, delimited by:

```
[EXPORT] BEGIN
...
[EXPORT] END
```

The backup data itself starts with `IRCITO-BACKUP|1` and ends with `END`.

Settings → **Import Backup** accepts the same format pasted over USB Serial. Import is a safe merge/overwrite: it does **not** call `eraseAll()` first, each complete and valid record commits independently to its NVS slot, invalid records are rejected, and records never reached by a truncated stream stay unchanged. Remote, signal and macro names can also be edited through this workflow.

Full format: [BACKUP.md](BACKUP.md).

## Tests

Host-side checks compile without an ESP32.

```bash
# IR pipeline
g++ -std=c++17 -Wall -Wextra -Werror \
  test/pipeline_host.cpp \
  -o /tmp/pipeline && /tmp/pipeline

# Storage
g++ -std=c++17 -Wall -Wextra -Werror \
  -Itest/stubs \
  test/storage_host.cpp src/storage.cpp \
  -o /tmp/storage && /tmp/storage

# TV-B-Gone
g++ -std=c++17 -Wall -Wextra -Werror \
  -Itest/stubs \
  test/tvbgone_host.cpp src/tvbgone.cpp src/tvbgone_data.cpp \
  -o /tmp/tvbgone && /tmp/tvbgone

# Macros
g++ -std=c++17 -Wall -Wextra -Werror \
  -Itest/stubs \
  test/macro_host.cpp src/macros.cpp src/storage.cpp \
  -o /tmp/macro && /tmp/macro

# Backup
g++ -std=c++17 -Wall -Wextra -Werror \
  -Itest/stubs \
  test/backup_host.cpp src/backup.cpp src/macros.cpp src/storage.cpp \
  -o /tmp/backup && /tmp/backup

# Hold / repeat
g++ -std=c++17 -Wall -Wextra -Werror \
  -Itest/stubs \
  test/hold_host.cpp \
  -o /tmp/hold && /tmp/hold
```

Coverage: Sony and NEC decoding, IR pipeline regression, transactional storage, simulated storage failures, corruption handling, namespace isolation, TV-B-Gone database decoding, macros, backup/import, hold/repeat and v1.4 → v1.5 NVS compatibility.

Firmware build check:

```bash
pio run -e m5stack-sticks3
```

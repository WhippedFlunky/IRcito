# IRcito v1.5 — Utilities

IRcito is an IR learner and remote-control firmware for the **M5StickS3**, distributed as an **ESP32-S3 application image**.

The provided application binary can be installed using EasyLauncher or any other compatible ESP32-S3 launcher/flashing workflow capable of writing an application image to the correct application partition or offset.

IRcito uses GPIO42 for IR RX, GPIO46 for IR TX and the ESP32-S3 RMT peripheral at 1 MHz. The current build targets the M5StickS3 with 8 MB flash / OPI PSRAM.

The 256-symbol RX buffer, 12 ms frame gap, EXT_5V handling, speaker amplifier handling, IR normalization/deglitch pipeline, protocol decoders and waveform timing remain unchanged from the physically proven v1.4 build.

`src/ir_pipeline.h` is byte-for-byte unchanged from v1.4.

Small RMT and storage buffers use internal RAM; critical IR functionality does not depend on PSRAM.

## Features

IRcito v1.5 includes:

- IR learning using the M5StickS3 built-in receiver
- Sony SIRC12 decoding and canonical replay
- NEC / NEC-like decoding and replay
- RAW fallback for unknown signals
- Manual RAW carrier selection:
  - 36 kHz
  - 38 kHz
  - 40 kHz
- Persistent saved remotes and buttons
- Save, load, overwrite and delete
- Protocol-aware hold/repeat
- Rename remotes, buttons and macros
- Macros / multi-step IR sequences
- USB Serial backup and import
- TV-B-Gone
- North America and Europe TV-B-Gone databases
- Transactional NVS storage
- Cross-version compatibility with v1.4 saved signals
- No filesystem required

## Controls

Home:

- **B** advances through:
  - Learn
  - Remotes
  - Macros
  - TV-B-Gone
  - Settings
  - About
- **A** opens the selected item

### Learn

After learning a signal:

- **Short A** → Test
- **Hold A ~800 ms** → Save / Remote
- **Short B**
  - RAW signal → change carrier between 36 / 38 / 40 kHz
  - recognized protocol → capture again
- **Hold B**
  - RAW signal → capture again
  - recognized protocol → return Home

The available controls are also shown on screen.

### Saved remotes

IRcito currently supports:

- **4 remotes**
- **4 buttons per remote**
- **16 saved signal slots total**

When saving:

1. Choose Remote 1–4.
2. Choose Button 1–4.
3. If the slot already contains a signal, IRcito displays:

```text
Overwrite?
Yes / No
```

The default is **No**.

Saved buttons provide:

- Test
- Rename
- Delete
- Back

### Hold / repeat

On a saved button:

- **Short A** sends the normal command.
- **Hold A** uses protocol-aware repetition while the button remains pressed.

Sony SIRC12 uses repeated canonical frames at approximately **45 ms start-to-start**.

NEC uses the standard repeat frame:

- 9 ms mark
- 2.25 ms space
- 560 µs mark

at approximately **110 ms intervals**.

RAW signals repeat only when they contain a valid repeat policy.

Old v1.4 RAW records with:

```text
repeatCount = 1
repeatPeriodUs = 0
```

do not loop automatically.

Releasing A stops further transmissions.

## Storage and replay

`src/storage.{h,cpp}` uses only the NVS namespace:

```text
ircito
```

through ESP32 Preferences.

The application image itself does **not** install, replace or modify a partition table or filesystem.

The build configuration declares a shared NVS partition of:

```text
0x5000 = 20 KiB
```

but the actual partition layout, NVS size and application offset depend on the firmware/launcher configuration already installed on the device.

At boot, IRcito checks that the running system contains a partition labelled:

```text
nvs
```

and reports its size and available NVS entries over Serial.

If suitable NVS storage is unavailable, saving is disabled gracefully.

If NVS becomes full, the previous stored signal remains active and an error is logged.

### RAW storage

IRcito currently uses a conservative:

```text
4096-byte total RAW payload budget
```

across all saved slots.

The unchanged 256-symbol RX buffer accepts up to 255 symbols per frame, approximately:

```text
1020 bytes
```

of RAW symbol data for a near-maximal capture.

For comparison:

```text
16 captures × 59 symbols ≈ 3776 RAW bytes
```

Recognized signals store compact canonical metadata and also retain their original RAW capture as a backup when space permits.

If the RAW payload budget would be exceeded, the optional RAW backup of a recognized signal may be omitted while preserving the canonical decoded signal.

### Transactional storage

Saved signal records include:

- magic
- format version
- length
- CRC32
- protocol
- carrier
- command/address information
- repeat metadata
- bounded RAW count

Each signal slot uses two alternating blobs plus an index.

The write process is:

```text
write inactive blob
        ↓
read back
        ↓
validate CRC/data
        ↓
commit new index
        ↓
delete old blob
```

A failed overwrite therefore leaves the previous active signal intact.

After an unexpected power loss, an unreferenced blob may remain, but it can be replaced by a future save to the same slot.

Delete removes only the selected slot.

**Erase all learned** clears only IRcito's own NVS namespace and does not erase unrelated ESP32 NVS data.

## IR replay

`src/main.cpp` uses one common `replay()` path for both:

- freshly captured signals
- loaded saved buttons

### Sony SIRC12

Sony SIRC12 is regenerated canonically using:

- 40 kHz carrier
- 33% duty
- three transmissions
- approximately 45 ms start-to-start

### NEC

Standard NEC replay uses:

```text
38 kHz
```

with protocol-aware hold/repeat support.

### RAW

Unknown signals use the preserved original RAW durations.

Because the built-in demodulating IR receiver is active-low while the transmitter uses active-high carrier gating, RAW replay uses the established RX → TX level inversion.

Available carriers:

```text
36 kHz
38 kHz
40 kHz
```

The original RX capture is never destructively normalized in place.

## TV-B-Gone

IRcito includes TV-B-Gone functionality based on the attributed open-source dataset stored at:

```text
third_party/tvbgone/WORLD_IR_CODES.h
```

The generated firmware representation is:

```text
src/tvbgone_data.cpp
```

and can be regenerated using:

```text
tools/generate_tvbgone.py
```

Licensing and attribution information is available at:

```text
third_party/tvbgone/NOTICE.md
```

The TV-B-Gone dataset is licensed under **CC BY-SA 2.5** and is not covered by IRcito's MIT license.

The current database contains:

- 137 North America entries
- 137 Europe entries

including shared entries.

The encoder decompresses the original MSB-first timing indices in 10 µs units into RMT symbols.

Durations exceeding the RMT 15-bit duration limit are split into multiple symbols without changing their total timing.

TV-B-Gone reuses IRcito's existing:

- RMT TX channel
- GPIO46
- EXT_5V handling

and dynamically changes the carrier using:

```cpp
rmt_apply_carrier()
```

Four unmodulated database entries are transmitted with carrier disabled.

Codes are sent one at a time with the original approximately **205 ms inter-code gap**.

**B** cancels the sequence between codes.

The longest individual code in the included database lasts approximately **334 ms**.

TV-B-Gone data is stored in firmware flash and is never written to NVS.

## Names

IRcito v1.5 includes a simple two-button text editor.

Available characters:

```text
A-Z
a-z
0-9
space
-
_
+
```

Controls:

- **Short B** → cycle character
- **Short A** → append selected character
- **Hold A** → save
- **Hold B** → cancel

Names are limited to:

```text
19 characters + NUL
```

Remote, signal and macro names can also be modified through the backup/import workflow.

## Macros

IRcito supports:

```text
4 macros
```

with up to:

```text
8 steps per macro
```

Each macro step references an existing:

```text
Remote / Button
```

rather than duplicating the stored IR signal.

Available delays between steps:

```text
0 ms
250 ms
500 ms
1000 ms
2000 ms
```

Macro execution loads each referenced signal and calls the same `replay()` function used elsewhere in IRcito.

**B** can cancel a macro between steps.

If a referenced button no longer exists, execution stops and logs:

```text
[MACRO] missing
```

Macros are stored separately using keys such as:

```text
m0a
m0b
m0i
...
m3a
m3b
m3i
```

inside the same IRcito-owned NVS namespace.

Macro records include:

- format version
- CRC
- A/B transactional commit

Macro data does not count against the 4096-byte RAW payload budget.

## v1.4 compatibility

IRcito v1.5 maintains compatibility with the v1.4 signal and remote storage format.

Remote names continue using:

```text
r0
r1
r2
r3
```

Signal records continue using the original v1.4 layout such as:

```text
s00a
s00b
```

and the existing A/B index mechanism.

Existing v1.4 Sony, NEC and RAW signals can be loaded by v1.5 without migration.

Neither existing saved signals nor the device's existing partition table are migrated or erased when upgrading.

`src/ir_pipeline.h` remains byte-for-byte identical to v1.4.

SHA-256:

```text
65a97ce85edb568bd26d30c7f1588d2e6fe680b02e11b6cb3991d0addae47770
```

## USB backup and import

Settings → **Export Backup** writes a complete versioned backup to USB Serial at:

```text
115200 baud
```

The export is delimited by:

```text
[EXPORT] BEGIN
...
[EXPORT] END
```

The actual backup data begins with:

```text
IRCITO-BACKUP|1
```

and ends with:

```text
END
```

Copy those lines into a text file to keep a backup.

Settings → **Import Backup** accepts the same format pasted over USB Serial.

Import is designed as a safe merge/overwrite operation.

It does **not** call:

```cpp
eraseAll()
```

before restoring data.

Each complete and valid record commits independently to its target NVS slot.

If an import stream is truncated, records that were never reached remain unchanged.

Invalid records are rejected rather than blindly written.

Full documentation:

[docs/BACKUP.md](docs/BACKUP.md)

## Build

IRcito uses PlatformIO and Arduino-ESP32 3.3.6.

Build with:

```sh
pio run -e m5stack-sticks3
```

or:

```sh
python -m platformio run -e m5stack-sticks3
```

The application binary is generated at:

```text
.pio/build/m5stack-sticks3/firmware.bin
```

## Installation

The generated/downloadable `.bin` is an **ESP32-S3 application image**.

It can be installed using EasyLauncher or any other compatible ESP32-S3 launcher/flashing workflow that supports writing an application image to the correct application partition or offset.

Examples include launcher-based installation and correctly configured low-level flashing workflows.

> **Important:** IRcito's release `.bin` is **not a merged full-flash image**.

It does not contain the complete:

- bootloader
- partition table
- full device flash layout

Therefore, do **not** blindly write the application binary to:

```text
0x0
```

When using a low-level flashing tool such as `esptool`, determine the correct application partition offset from the partition table already installed on the device before writing IRcito.

The application binary itself does not replace the existing partition table.

USB Serial:

```text
115200 baud
```

## Tests

Host-side checks can be compiled without an ESP32.

### IR pipeline

```sh
g++ -std=c++17 -Wall -Wextra -Werror \
  test/pipeline_host.cpp \
  -o /tmp/pipeline && /tmp/pipeline
```

### Storage

```sh
g++ -std=c++17 -Wall -Wextra -Werror \
  -Itest/stubs \
  test/storage_host.cpp src/storage.cpp \
  -o /tmp/storage && /tmp/storage
```

### TV-B-Gone

```sh
g++ -std=c++17 -Wall -Wextra -Werror \
  -Itest/stubs \
  test/tvbgone_host.cpp src/tvbgone.cpp src/tvbgone_data.cpp \
  -o /tmp/tvbgone && /tmp/tvbgone
```

### Macros

```sh
g++ -std=c++17 -Wall -Wextra -Werror \
  -Itest/stubs \
  test/macro_host.cpp src/macros.cpp src/storage.cpp \
  -o /tmp/macro && /tmp/macro
```

### Backup

```sh
g++ -std=c++17 -Wall -Wextra -Werror \
  -Itest/stubs \
  test/backup_host.cpp src/backup.cpp src/macros.cpp src/storage.cpp \
  -o /tmp/backup && /tmp/backup
```

### Hold/repeat

```sh
g++ -std=c++17 -Wall -Wextra -Werror \
  -Itest/stubs \
  test/hold_host.cpp \
  -o /tmp/hold && /tmp/hold
```

The host checks cover:

- Sony and NEC decoding
- IR pipeline regression
- transactional storage
- simulated storage failures
- corruption handling
- namespace isolation
- TV-B-Gone database decoding
- macros
- backup/import
- hold/repeat
- v1.4 → v1.5 NVS compatibility

PlatformIO build:

```text
pio run -e m5stack-sticks3
PASS
```

## Physical testing

IRcito v1.5 has been physically validated on real M5StickS3 hardware.

Tested functionality includes:

- IR reception
- IR transmission
- Learn / replay
- persistence across reboot
- saved remotes
- Sony SIRC12
- NEC / NEC-like signals
- RAW fallback
- RAW carrier switching
- overwrite / delete
- hold/repeat
- macros
- rename
- USB backup/import
- TV-B-Gone

Compatibility with every infrared device or protocol is **not guaranteed**.

## Binary release

IRcito v1.5.0:

```text
IRcito-v1.5.bin
```

Size:

```text
666608 bytes
```

SHA-256:

```text
6e0f4ff58be59fcff8e3a822eeeb45ed9a243f4415db0b96665adcc5cffb74ae
```

The provided file is a valid ESP32-S3 **application image**, not a merged full-flash image.

## Development

IRcito is an **AI-assisted firmware project**.

Project direction, requirements, hardware testing, debugging, integration and physical validation were carried out by **Nara Sofía García Ramírez**.

Code implementation and review were assisted by **OpenAI ChatGPT Work**.

The project was developed iteratively using:

- real IR captures
- serial logs
- physical hardware testing
- regression testing
- protocol analysis

## License

Original IRcito source code is released under the **MIT License**.

See:

```text
LICENSE
```

Third-party components and datasets retain their respective licenses and attribution requirements.

In particular, the TV-B-Gone dataset is **not** covered by IRcito's MIT license.

See:

```text
third_party/tvbgone/NOTICE.md
```

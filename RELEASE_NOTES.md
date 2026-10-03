# IRcito v1.5.0 — Utilities

IRcito v1.5 adds practical day-to-day utilities on top of the v1.4 saved-remotes build while keeping the proven IR pipeline unchanged.

## What's new

### Protocol-aware hold / repeat

Saved buttons can now be held for repeated transmission.

- **Sony SIRC12**
  - repeated canonical frames
  - approximately 45 ms start-to-start
- **NEC**
  - standard NEC repeat frame
  - approximately 110 ms repeat interval
- **RAW**
  - does not repeat unless a valid repeat policy is present

Releasing the button stops further transmissions.

### Macros

IRcito now supports:

- up to **4 macros**
- up to **8 steps per macro**
- references to existing Remote/Button slots
- configurable delays between steps:
  - 0 ms
  - 250 ms
  - 500 ms
  - 1000 ms
  - 2000 ms
- cancellation during execution

Macros use the same `replay()` path as normal saved buttons.

If a referenced signal no longer exists, the macro aborts safely and reports the missing slot.

### Rename

Remotes, buttons and macros can now be renamed using the built-in two-button text editor.

Supported characters:

```text
A-Z
a-z
0-9
space
-
_
+
```

Names are limited to 19 characters plus NUL.

### USB Serial backup / import

IRcito can now export and import:

- remote names
- saved signals
- protocol metadata
- RAW data
- carrier settings
- repeat metadata
- macros

Backup format:

```text
IRCITO-BACKUP|1
```

Export and import use USB Serial at:

```text
115200 baud
```

Import is performed as a safe merge/overwrite operation and does not erase all existing data before restoration.

A truncated import leaves records that were not reached unchanged.

See:

```text
docs/BACKUP.md
```

for full details.

## Compatibility with v1.4

IRcito v1.5 preserves the existing v1.4 signal and remote storage format.

A cross-version compatibility test wrote real v1.4 Sony and RAW NVS records using the v1.4 storage module and loaded the same records with v1.5 without migration.

Existing v1.4 saved remotes are therefore retained when upgrading.

The IR processing pipeline remains unchanged:

- `src/ir_pipeline.h` is byte-for-byte identical to v1.4
- partition configuration is unchanged
- existing signal records are not migrated
- macro records use new independent NVS keys

`src/ir_pipeline.h` SHA-256:

```text
65a97ce85edb568bd26d30c7f1588d2e6fe680b02e11b6cb3991d0addae47770
```

`platformio.ini` SHA-256:

```text
e519ccd1935a3ea31c1342f3a5a4f1eb2e80c0043b8e93045305990c9ad42b73
```

## Storage

Macros add new records:

```text
m0a / m0b / m0i
...
m3a / m3b / m3i
```

inside IRcito's existing application-owned NVS namespace.

Macro records use:

- magic/version validation
- CRC
- A/B transactional commit

Macro data does not consume the RAW payload budget used by saved IR captures.

## Installation

Download:

```text
IRcito-v1.5.bin
```

The provided binary is a valid **ESP32-S3 application image**.

It can be installed using EasyLauncher or another compatible ESP32-S3 launcher/flashing workflow capable of writing an application image to the correct application partition or offset.

> **Important:** this is not a merged full-flash image.

Do not blindly flash the release binary at:

```text
0x0
```

The application image does not contain a complete bootloader or partition table.

When using a low-level flashing tool such as `esptool`, determine the correct application partition offset from the partition table already installed on the device.

### Binary information

File:

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

`esptool image-info` validation:

- ESP32-S3 application image
- six segments
- 8 MB flash header
- valid checksum
- valid validation hash

## Verification

Host-side tests:

- `pipeline_host` — **PASS**
- `storage_host` — **PASS**
- `tvbgone_host` — **PASS**
- `macro_host` — **PASS**
- `backup_host` — **PASS**
- `hold_host` — **PASS**
- v1.4 → v1.5 NVS compatibility — **PASS**

PlatformIO:

```text
pio run -e m5stack-sticks3
```

Result:

```text
SUCCESS
```

## Physical testing

IRcito v1.5 has been physically tested on real M5StickS3 hardware.

Validated functionality includes:

- Learn / replay
- persistent saved remotes
- persistence across reboot
- Sony SIRC12
- NEC / NEC-like operation
- RAW fallback
- carrier switching
- hold / repeat
- macros
- rename
- USB backup / import
- save / overwrite / delete
- TV-B-Gone
- TV-B-Gone cancellation
- v1.4 regression behavior

Compatibility with every infrared device or protocol is not guaranteed.

## Notes

IRcito v1.5 remains focused on functionality rather than a final polished UI.

The main goals of this release were:

- preserve the known-good IR core
- improve everyday usability
- add automation through macros
- add safe backup/import
- keep compatibility with v1.4 saved data

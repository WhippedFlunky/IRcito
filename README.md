# IRcito

**Universal IR learner and remote-control firmware for the M5StickS3.**

[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
![Platform: ESP32-S3](https://img.shields.io/badge/platform-ESP32--S3-informational)
![Version: 1.5.0](https://img.shields.io/badge/version-1.5.0-green)

Point a remote at your M5StickS3, press a button, and IRcito learns it. Save the signal, replay it later, chain several signals into a macro, or fire off TV-B-Gone. Everything is stored on the device itself, with no filesystem and no companion app.

<!-- Add a photo or screenshot of the device here, e.g. ![IRcito on the M5StickS3](docs/images/ircito.jpg) -->

## Features

- **Learn** signals with the built-in IR receiver
- **Sony SIRC12** and **NEC / NEC-like** decoding with canonical replay
- **RAW fallback** for unknown protocols, with selectable 36 / 38 / 40 kHz carrier
- **Saved remotes:** 4 remotes × 4 buttons, persistent across reboots
- **Hold to repeat**, protocol-aware (Sony, NEC and RAW with a repeat policy)
- **Macros:** up to 4 macros of 8 steps each, with configurable delays
- **Rename** remotes, buttons and macros on-device
- **TV-B-Gone** with North America and Europe databases
- **USB Serial backup and import**
- **Crash-safe storage:** a failed save never destroys the previous signal
- Compatible with signals saved by v1.4

## Hardware

| Item | Value |
|---|---|
| Device | M5StickS3 (ESP32-S3, 8 MB flash, OPI PSRAM) |
| IR receiver | GPIO42 |
| IR transmitter | GPIO46 |
| Framework | Arduino-ESP32 3.3.6 via PlatformIO |

## Installation

The release file `IRcito-v1.5.bin` is an **ESP32-S3 application image**. Install it with [EasyLauncher](https://github.com/) or any other launcher or flashing workflow that can write an application image to the correct application partition.

> [!WARNING]
> The `.bin` is **not** a merged full-flash image. It contains no bootloader and no partition table. **Do not write it to `0x0`.** If you use a low-level tool such as `esptool`, first read the application partition offset from the partition table already on your device.

IRcito does not install or modify the partition table. It uses a partition labelled `nvs` that must already exist on the device. If none is available, saving is disabled and everything else keeps working.

### Build from source

```bash
pio run -e m5stack-sticks3
```

The image is written to `.pio/build/m5stack-sticks3/firmware.bin`.

## Usage

Press **B** on the home screen to move through the menu and **A** to open the selected item.

**Home menu:** Learn → Remotes → Macros → TV-B-Gone → Settings → About

### Learning a signal

| After capture | Action |
|---|---|
| Short **A** | Test the signal |
| Hold **A** (~800 ms) | Save to a remote and button |
| Short **B** | RAW: change carrier (36 / 38 / 40 kHz). Recognized: capture again |
| Hold **B** | RAW: capture again. Recognized: back to Home |

On-screen hints show the available actions.

### Saved remotes and buttons

Choose a remote (1–4) and a button (1–4) when saving. If the slot is taken, IRcito asks for confirmation and defaults to **No**.

On a saved button:

- **Short A** sends the command once.
- **Hold A** repeats it while the button stays pressed.
- The menu also offers **Rename** and **Delete**.

### Macros

A macro is a sequence of up to 8 steps. Each step points to an existing remote button and adds a delay of 0, 250, 500, 1000 or 2000 ms before the next step. Press **B** to cancel between steps. If a referenced button has been deleted, the macro stops.

### TV-B-Gone

Sends power-off codes one at a time from the North America or Europe database. Press **B** to cancel between codes.

### Naming

A two-button editor: short **B** cycles the character, short **A** appends it, hold **A** saves, hold **B** cancels. Names are up to 19 characters from `A-Z a-z 0-9 space - _ +`.

### Backup and import

Settings → **Export Backup** prints a versioned backup over USB Serial (115200 baud). Copy the output into a text file to keep it. Settings → **Import Backup** accepts the same text pasted back in. Import merges into existing data and never wipes it first. See [docs/BACKUP.md](docs/BACKUP.md) for the format.

## Limits

| Resource | Limit |
|---|---|
| Remotes | 4 |
| Buttons per remote | 4 (16 signal slots total) |
| Macros | 4, with up to 8 steps each |
| Name length | 19 characters |
| RAW payload, all slots combined | 4096 bytes |
| RAW symbols per captured frame | 255 |

If the RAW budget would be exceeded, the optional RAW backup of a recognized signal is dropped and the decoded signal is kept.

## Tests

Host-side tests compile on a regular machine with no ESP32:

```bash
g++ -std=c++17 -Wall -Wextra -Werror test/pipeline_host.cpp -o /tmp/pipeline && /tmp/pipeline
```

Test targets cover the IR pipeline, storage, macros, backup, hold/repeat and TV-B-Gone. Commands for all of them are in [docs/TECHNICAL.md](docs/TECHNICAL.md#tests).

IRcito v1.5 has also been validated on real M5StickS3 hardware: reception, transmission, learn and replay, persistence across reboots, overwrite and delete, hold/repeat, macros, rename, backup/import and TV-B-Gone. Compatibility with every infrared device or protocol is **not guaranteed**.

## Documentation

- [docs/TECHNICAL.md](docs/TECHNICAL.md): storage design, replay details, TV-B-Gone, macros, v1.4 compatibility, tests
- [docs/BACKUP.md](docs/BACKUP.md): backup and import format
- [RELEASE_NOTES.md](RELEASE_NOTES.md): version history

## Binary release

| | |
|---|---|
| File | `IRcito-v1.5.bin` |
| Size | 666608 bytes |
| SHA-256 | `6e0f4ff58be59fcff8e3a822eeeb45ed9a243f4415db0b96665adcc5cffb74ae` |

## Development

IRcito is an **AI-assisted firmware project**. Project direction, requirements, hardware testing, debugging, integration and physical validation were carried out by **Nara Sofía García Ramírez**. Code implementation and review were assisted by **OpenAI ChatGPT Work**.

It was built iteratively from real IR captures, serial logs, physical hardware testing, regression tests and protocol analysis.

## License

Original IRcito source code is released under the [MIT License](LICENSE).

Third-party components and datasets keep their own licenses. In particular, the TV-B-Gone dataset is licensed under **CC BY-SA 2.5** and is **not** covered by IRcito's MIT license. See [third_party/tvbgone/NOTICE.md](third_party/tvbgone/NOTICE.md).

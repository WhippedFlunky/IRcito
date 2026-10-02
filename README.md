# IRcito

Universal IR learner and remote-control firmware for the M5StickS3.

IRcito turns the M5StickS3 into a small standalone infrared learning remote with protocol decoding, RAW fallback, persistent saved remotes and TV-B-Gone functionality.

## Current release

**v1.4.0**

This is the first public release of IRcito.

## Features

- Learn IR signals using the M5StickS3 built-in IR receiver
- Replay captured signals
- Sony SIRC12 decoding and canonical replay
- NEC / NEC-like decoding through the current universal pipeline
- RAW fallback for unknown signals
- Manual RAW carrier selection:
  - 36 kHz
  - 38 kHz
  - 40 kHz
- Persistent saved remotes and buttons
- Save, load, overwrite and delete learned signals
- Data validation and corruption handling
- TV-B-Gone
- North America and Europe TV-B-Gone databases
- Cancelable TV-B-Gone transmission
- No filesystem required
- No PSRAM dependency for critical IR buffers
- Designed for use as an application image under EasyLauncher

## Hardware

Target device:

- M5StickS3
- ESP32-S3-PICO-1-N8R8
- 8 MB flash
- 8 MB PSRAM

Integrated IR hardware:

- IR RX: GPIO42
- IR TX: GPIO46

The firmware uses RMT at 1 MHz resolution.

## IR pipeline

The learner follows a non-destructive pipeline:

```text
RMT RX
  ↓
Original RAW capture
  ↓
Temporary run representation
  ↓
Conservative deglitch / normalization
  ↓
Protocol decoders
  ├── recognized → canonical replay
  └── unknown    → RAW replay

The original RAW capture is preserved rather than destructively rewritten during decoding.
For RAW replay, the received demodulated levels are inverted for transmission because the integrated receiver output is active-low while the IR transmitter uses active-high carrier gating.
Saved remotes
IRcito v1.4 supports persistent remotes and buttons stored in NVS.
Stored signals may contain:
- protocol
- carrier frequency
- address
- command
- bit count
- repeat metadata
- original RAW capture when applicable
Storage records use versioning and validation.
The storage implementation is designed so corrupted records do not crash the firmware or invalidate unrelated saved signals.
TV-B-Gone
IRcito includes TV-B-Gone functionality using an attributed third-party infrared code database.
The upstream dataset and its licensing information are kept under:
third_party/tvbgone/

See:
third_party/tvbgone/NOTICE.md

for attribution and licensing details.
The generated firmware representation is located in:
src/tvbgone_data.cpp

and can be regenerated using:
tools/generate_tvbgone.py

Physical testing
IRcito v1.4 has been tested on real M5StickS3 hardware.
The following functionality has been physically validated during development:
- IR reception
- IR transmission
- Sony SIRC12 learning and replay
- NEC / NEC-like learning and replay
- RAW fallback
- carrier switching
- saved remotes
- persistence across reboot
- delete / overwrite
- TV-B-Gone
This does not imply compatibility with every IR device or protocol.
Build
The project uses PlatformIO.
From the project directory:
python -m platformio run

The generated application image is normally available at:
.pio/build/m5stack-sticks3/firmware.bin

IRcito is intended to be installed as an application image.
Do not treat the application .bin as a merged full-flash image.
Tests
Host-side tests are included for:
- IR pipeline
- storage
- TV-B-Gone
See:
test/

Project structure
src/
  ir_pipeline.h
  main.cpp
  storage.cpp
  storage.h
  tvbgone.cpp
  tvbgone.h
  tvbgone_data.cpp

test/
third_party/
tools/

Development
IRcito is an AI-assisted firmware project.
Project direction, requirements, hardware testing, debugging, integration and physical validation were carried out by Nara Sofía García Ramírez.
Code implementation and review were assisted by OpenAI ChatGPT Work.
The project was developed iteratively using real IR captures, serial logs, physical device testing and regression testing.
Third-party components
Third-party source code and datasets retain their respective licenses and attribution requirements.
In particular, the TV-B-Gone database is not covered by the MIT license of IRcito itself. See:
third_party/tvbgone/NOTICE.md

License
Original IRcito source code is released under the MIT License.
See:
LICENSE

Third-party components remain under their respective licenses.

# v1.5 utilities — release verification

Application image: `dist/IR-Learner-StickS3-v1.5-utilities-app.bin`

- Size: **666608 bytes**
- SHA-256: `6e0f4ff58be59fcff8e3a822eeeb45ed9a243f4415db0b96665adcc5cffb74ae`
- esptool image-info: **ESP32-S3**, six segments, 8 MB flash header, valid
  checksum and validation hash; this is an application image, not a merged
  flash image for address 0x0.
- `src/ir_pipeline.h` SHA-256 in both v1.4 and v1.5:
  `65a97ce85edb568bd26d30c7f1588d2e6fe680b02e11b6cb3991d0addae47770`.
- `platformio.ini` SHA-256 in both versions:
  `e519ccd1935a3ea31c1342f3a5a4f1eb2e80c0043b8e93045305990c9ad42b73`.

The v1.4 signal and remote wire formats remain at version 1. A host
compatibility run wrote actual v1.4 Sony and RAW NVS records using the v1.4
storage module, then loaded those same bytes using v1.5 without migration.
Macro records add new keys `m0a/b/i` through `m3a/b/i` in the same app-owned
namespace, with magic/version/CRC and A/B commit. The partition table and
IR normalization/deglitch/decoder source were not changed.

Host tests: `pipeline_host`, `storage_host`, `tvbgone_host`, `macro_host`,
`backup_host`, `hold_host`, and the cross-version NVS compatibility check:
**PASS**. PlatformIO `pio run -e m5stack-sticks3`: **SUCCESS**. Physical
operation of the new hold, macro, rename, and USB import flows has not been
tested without the actual StickS3; test them before relying on a backup.

Recommended physical checks:

1. Verify v1.4 saved Sony, NEC and RAW buttons still load and Test works.
2. Hold and release A on saved Sony Vol+ and an NEC button. Check `[HOLD]`
   start/repeat/stop and that no further frames are sent after release.
3. Build a three-step macro with delays; run it, then cancel another run with B.
   Delete a referenced button and confirm the macro aborts with `[MACRO] missing`.
4. Rename a remote/button/macro; reboot and confirm names persist.
5. Export to a Serial log, import after changing a record, and verify names,
   canonical and RAW buttons, carrier, and macros; check `[IMPORT]` errors.
6. Recheck Learn/Save/overwrite/delete and TV-B-Gone cancellation.

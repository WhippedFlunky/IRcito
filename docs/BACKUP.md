# IRcito USB Serial backup format, version 1

This is a line-oriented text stream so the StickS3 never loads an entire
backup in memory. USB Serial is 115200 baud, newline `LF` (`CRLF` accepted).
Only uppercase hex digits are accepted. Up to four remotes, four buttons per
remote, four macros, eight steps per macro; indices on wire are **zero-based**.

```
IRCITO-BACKUP|1
R|0|536F6E79205456|XXXXXXXX
S|0|0|566F6C756D65202B|1|40000|1|18|12|3|45000|0||XXXXXXXX
M|0|4576656E696E67|2|0,0,1000;1,2,500;|XXXXXXXX
END
```

`XXXXXXXX` is eight uppercase hex digits of CRC32 (IEEE polynomial
`0xEDB88320`, init and final XOR `0xFFFFFFFF`) over all preceding bytes on
the **same line**, excluding the final `|` and checksum. Names are ASCII
bytes encoded in hex, 1–19 characters from `A-Z a-z 0-9 space - _ +`.
Remote records (`R`) carry slot and name. Signal records (`S`) carry remote,
button, name, protocol (0 RAW, 1 Sony SIRC12, 2 NEC), carrier Hz, address,
command, bit count, repeat count, repeat period µs, RAW symbol count and
RAW symbols. Every RMT symbol is exactly eight hex digits of the native
32-bit `rmt_symbol_word_t.val`; the count determines the field length. A
recognized signal may have zero RAW symbols. RAW records need at least one.
The RMT capture is copied without normalization. Macro records (`M`) carry
macro slot, name, step count, and a final field with `remote,button,delay;`
per step. Allowed delays are `0,250,500,1000,2000` ms.

The importer validates the marker/version, CRC, field counts, numeric bounds,
names and protocol metadata through the v1.4 storage validator, and the
existing 4096-byte RAW budget. Every accepted signal uses the v1.4 A/B
transactional write. Macro records use a separate A/B transaction. Invalid
records are rejected without clearing their prior slot. Records already
committed before a truncated transfer may remain updated; all other slots
stay as they were. The `END` line marks a complete transfer. There is no
global erase, implicit deletion, JSON parser, filesystem, or PSRAM dependency.

For import, select **Settings → Import Backup**, then paste exactly the
header, record lines, and `END` from a saved export. Watch `[IMPORT]`
messages for rejected records and commit count. Press B to cancel. Serial
terminal programs must send plain text with newlines; the export wrapper
lines `[EXPORT] BEGIN` and `[EXPORT] END` are **not** input lines.

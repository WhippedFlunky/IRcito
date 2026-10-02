# TV-B-Gone data and attribution

`WORLD_IR_CODES.h` is an unmodified copy of the public TV-B-Gone for Arduino
database from https://github.com/shirriff/Arduino-TV-B-Gone (retrieved
2026-10-01). `src/tvbgone_data.cpp` is generated from it by
`tools/generate_tvbgone.py` and preserves the original region order, carrier
frequencies, compressed code indices, and timing values. The ESP32-S3 RMT
encoder in `src/tvbgone.cpp` is newly written for this project.

Original TV-B-Gone firmware copyright (c) Mitch Altman and Limor Fried 2009;
Arduino port by Ken Shirriff; later edits and documentation by Gabriel Staples.
The upstream project identifies its license as **Creative Commons
Attribution-ShareAlike 2.5**:
https://creativecommons.org/licenses/by-sa/2.5/

Credit the authors and the source URL when redistributing the adapted database
or firmware. Share adaptations under CC BY-SA 2.5 or a compatible license and
indicate changes. This project makes no claim of exclusive rights in the
original database. See the original source file and upstream README for the
original author notices. Other third-party dependencies retain their own
licenses.

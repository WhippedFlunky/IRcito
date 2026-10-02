#!/usr/bin/env python3
"""Reproducibly convert the attributed TV-B-Gone v1.3 database into flash C++.

Input: third_party/tvbgone/WORLD_IR_CODES.h (unmodified upstream file).
No button or manufacturer mapping is added. The MSB-first compressed indices,
10-us timing units, frequency, and region order are preserved.
"""
import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parents[1]
source = (ROOT / "third_party/tvbgone/WORLD_IR_CODES.h").read_text()
source = re.sub(r"/\*.*?\*/|//[^\n]*", "", source, flags=re.S)
out = ROOT / "src/tvbgone_data.cpp"

def array(kind):
    found = {}
    for name, body in re.findall(rf"const uint(?:8|16)_t (code_\w+{kind})\[\] PROGMEM\s*=\s*\{{(.*?)\}};", source, re.S):
        body = re.sub(r"//[^\n]*|/\*.*?\*/", "", body, flags=re.S)
        found[name] = [int(x, 0) for x in re.findall(r"0x[0-9a-fA-F]+|\d+", body)]
    return found

times, indices = array("Times"), array("Codes")
codes = []
for m in re.finditer(
    r"const struct IrCode (code_(?:na|eu)\d+Code) PROGMEM\s*=\s*\{\s*"
    r"(?:freq_to_timerval\((\d+)\)|(0)),\s*(\d+),\s*(\d+),"
    r"\s*(code_(?:na|eu)\d+Times),\s*(code_(?:na|eu)\d+Codes)", source):
    name, hz, no_carrier, pairs, bits, t, p = m.groups()
    hz, pairs, bits = int(hz or no_carrier), int(pairs), int(bits)
    assert (hz == 0 or 25000 <= hz <= 85000) and 1 <= pairs <= 255 and bits in (2, 3)
    assert len(times[t]) % 2 == 0 and len(times[t]) // 2 <= 1 << bits
    assert len(indices[p]) * 8 >= pairs * bits
    for k in range(pairs):
        value = 0
        for j in range(bits):
            offset = k * bits + j
            value = (value << 1) | ((indices[p][offset // 8] >> (7 - offset % 8)) & 1)
        assert value < len(times[t]) // 2, (name, k, value)
    codes.append((name, hz, pairs, bits, times[t], indices[p]))

lookup = {c[0]: i for i, c in enumerate(codes)}
regions = {}
for region in ("NA", "EU"):
    raw = re.search(rf"(?:NApowerCodes|EUpowerCodes)\[\] PROGMEM\s*=\s*\{{(.*?)\}};",
                    source, re.S).group(1)
    raw = re.sub(r"//[^\n]*|/\*.*?\*/", "", raw, flags=re.S)
    regions[region] = [lookup[x] for x in re.findall(r"&?(code_(?:na|eu)\d+Code)", raw)]
    assert len(regions[region]) > 100

lines = [
    '// Adapted DATA from shirriff/Arduino-TV-B-Gone WORLD_IR_CODES.h,',
    '// (c) Mitch Altman + Limor Fried 2009, port Ken Shirriff; see',
    '// third_party/tvbgone/NOTICE.md (CC BY-SA 2.5 attribution/share-alike).',
    '#include "tvbgone.h"',
    'namespace tvbgone {',
]
for i, (_, _, _, _, t, p) in enumerate(codes):
    lines += [f'static const uint16_t t{i}[] = {{{", ".join(map(str, t))}}};',
              f'static const uint8_t p{i}[] = {{{", ".join(map(str, p))}}};']
lines.append('const Code kCodes[] = {')
for i, (_, hz, pairs, bits, t, _) in enumerate(codes):
    lines.append(f'  {{{hz}, {pairs}, {bits}, {len(t)//2}, t{i}, p{i}}},')
lines.append('};')
for region in ('NA', 'EU'):
    lines.append(f'const uint16_t k{region}[] = {{{", ".join(map(str, regions[region]))}}};')
    lines.append(f'const size_t k{region}Count = {len(regions[region])};')
lines.append('} // namespace tvbgone')
out.write_text('\n'.join(lines) + '\n')
print(f'{len(codes)} distinct codes; NA={len(regions["NA"])} EU={len(regions["EU"])}; wrote {out}')

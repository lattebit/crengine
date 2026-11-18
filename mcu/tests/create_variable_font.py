#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# 中文：从测试字库生成无轮廓增量的双轴字体，仅供故障和轴识别测试。
# English: Build a two-axis font without outline deltas for failure and axis-identification tests.
from pathlib import Path
import struct
import sys

source = Path(sys.argv[1]).read_bytes()
count = struct.unpack_from('>H', source, 4)[0]
tables = {}
for i in range(count):
    tag, _, offset, size = struct.unpack_from('>4sIII', source, 12 + i * 16)
    tables[tag] = source[offset:offset + size]
if b'glyf' not in tables:
    raise SystemExit('Variable-font tests require a TrueType outline test font')
glyphs = struct.unpack_from('>H', tables[b'maxp'], 4)[0]
# 中文：光学尺寸刻意排第一，确保调用方不是读取第一个轴当字重。
# English: Put optical size first so callers cannot mistake the first axis for weight.
tables[b'fvar'] = struct.pack('>IHHHHHH', 0x10000, 16, 2, 2, 20, 0, 12)
for tag, low, default, high, name in [(b'opsz', 7, 12, 72, 256), (b'wght', 100, 400, 900, 257)]:
    tables[b'fvar'] += struct.pack('>4siiiHH', tag, low << 16, default << 16, high << 16, 0, name)
end = 20 + (glyphs + 1) * 2
tables[b'gvar'] = struct.pack('>IHHIHHI', 0x10000, 2, 0, end, glyphs, 0, end) + bytes((glyphs + 1) * 2)
head = bytearray(tables[b'head']); head[8:12] = bytes(4); tables[b'head'] = head
count = len(tables); power = 1 << (count.bit_length() - 1)
data = bytearray(struct.pack('>IHHHH', 0x10000, count, power * 16, power.bit_length() - 1, count * 16 - power * 16))
data += bytes(count * 16)
def checksum(block):
    block = block + bytes((-len(block)) % 4)
    return sum(struct.unpack(f'>{len(block) // 4}I', block)) & 0xffffffff
head_at = 0
for i, (tag, block) in enumerate(sorted(tables.items())):
    offset = len(data)
    struct.pack_into('>4sIII', data, 12 + i * 16, tag, checksum(block), offset, len(block))
    data += block; data += bytes((-len(data)) % 4)
    if tag == b'head': head_at = offset
struct.pack_into('>I', data, head_at + 8, (0xb1b0afba - checksum(data)) & 0xffffffff)
Path(sys.argv[2]).write_bytes(data)

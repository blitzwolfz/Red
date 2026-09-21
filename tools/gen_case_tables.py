#!/usr/bin/env python3
"""Generates src/unicode_case.h from the Unicode data Python ships with.

    python3 tools/gen_case_tables.py > src/unicode_case.h

Only simple case mapping is generated: one code point in, one code point
out. The mappings that change length are handled separately in
src/unicode_case.cpp, because there are few enough of them to list.

The output is runs of (start, end, stride, delta), because the mappings
come in two shapes. Whole alphabets move by a constant: a-z is one run
with stride 1. Latin Extended-A and much of Cyrillic and Greek alternate
upper, lower, upper, lower: those are runs with stride 2.
"""
import sys
import unicodedata

MAX = 0x110000


def simple_mappings(direction):
    """code point -> its simple upper or lower case, where they differ."""
    out = {}
    for cp in range(MAX):
        ch = chr(cp)
        mapped = ch.upper() if direction == "upper" else ch.lower()
        # Anything that changes length is not a simple mapping.
        if len(mapped) != 1:
            continue
        target = ord(mapped)
        if target != cp:
            out[cp] = target - cp
    return out


def runs(mapping):
    """Packs the mapping into (start, end, stride, delta) runs.

    A run may only use code points no earlier run has taken, which is why
    the scan looks at `left` rather than at the mapping: otherwise a
    stride 2 run would happily reach across points already spoken for.
    """
    left = set(mapping)
    packed = []
    for start in sorted(mapping):
        if start not in left:
            continue
        delta = mapping[start]

        # Try stride 1 and stride 2, and keep whichever reaches further.
        best_stride, best_count = 1, 1
        for stride in (1, 2):
            count = 1
            cp = start + stride
            while cp in left and mapping[cp] == delta:
                count += 1
                cp += stride
            if count > best_count:
                best_stride, best_count = stride, count

        end = start + (best_count - 1) * best_stride
        packed.append((start, end, best_stride, delta))
        for k in range(best_count):
            left.discard(start + k * best_stride)
    return packed


def emit(name, packed, out):
    out.write(f"constexpr CaseRun k{name}[] = {{\n")
    for start, end, stride, delta in packed:
        out.write(f"    {{0x{start:05x}, 0x{end:05x}, {stride}, {delta}}},\n")
    out.write("};\n\n")


def main():
    out = sys.stdout
    version = unicodedata.unidata_version
    out.write(f"""// Simple case mappings, generated from Unicode {version}.
//
//     python3 tools/gen_case_tables.py > src/unicode_case.h
//
// Do not edit by hand. Each run says: for every code point from `start`
// to `end` stepping by `stride`, add `delta` to reach the other case.
#pragma once

#include <cstdint>

namespace red {{

struct CaseRun {{
  uint32_t start;
  uint32_t end;
  uint8_t stride;
  int32_t delta;
}};

""")
    upper = runs(simple_mappings("upper"))
    lower = runs(simple_mappings("lower"))
    emit("ToUpper", upper, out)
    emit("ToLower", lower, out)
    out.write("}  // namespace red\n")
    sys.stderr.write(f"upper: {len(upper)} runs, lower: {len(lower)} runs\n")


if __name__ == "__main__":
    main()

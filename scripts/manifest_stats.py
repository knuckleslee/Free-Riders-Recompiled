#!/usr/bin/env python3
"""Counts what a pipeline manifest holds, and what a later one added.

    py scripts\\manifest_stats.py pipeline-cache\\pipelines-d3d12.bin
    py scripts\\manifest_stats.py before.bin after.bin      # what after added

Used while recording the manifest that ships with a release
(docs/roadmap.md): copy the file after each recorded run and compare it with
the one before. Runs that add next to nothing mean the recording has covered
that part of the game.
"""
import struct
import sys

MAGIC = b'SFRPLM01'


def load(path):
    data = open(path, 'rb').read()
    if data[:8] != MAGIC:
        raise SystemExit(f'{path}: not a pipeline manifest')
    records, at = [], 8
    while at + 4 <= len(data):
        (length,) = struct.unpack_from('<I', data, at)
        at += 4
        if not length or at + length > len(data):
            break  # a cut tail: the complete records before it count
        records.append(data[at:at + length])
        at += length
    return records


def shaders(record):
    # version u32, vertex id u64, pixel id u64 (native_pipeline_manifest.cpp)
    return struct.unpack_from('<QQ', record, 4)


def describe(records):
    vertex = {shaders(r)[0] for r in records}
    pixel = {shaders(r)[1] for r in records}
    return vertex, pixel


def main(argv):
    if len(argv) not in (2, 3):
        raise SystemExit(__doc__)
    first = load(argv[1])
    vertex, pixel = describe(first)
    print(f'{argv[1]}: {len(first)} pipelines, {len(vertex)} vertex shaders, {len(pixel)} pixel shaders')
    if len(argv) == 3:
        second = load(argv[2])
        v2, p2 = describe(second)
        known = set(first)
        added = [r for r in second if r not in known]
        print(f'{argv[2]}: {len(second)} pipelines, {len(v2)} vertex shaders, {len(p2)} pixel shaders')
        share = 100.0 * len(added) / max(1, len(second))
        print(f'added: {len(added)} pipelines ({share:.1f}% of the later file), '
              f'{len(v2 - vertex)} new vertex shaders, {len(p2 - pixel)} new pixel shaders')
        dropped = len(known - set(second))
        if dropped:
            print(f'note: {dropped} pipelines of the first file are not in the second (a different recording)')


if __name__ == '__main__':
    main(sys.argv)

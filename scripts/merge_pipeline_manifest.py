#!/usr/bin/env python3
"""Merges pipeline manifests into one, without duplicates.

    py scripts\\merge_pipeline_manifest.py shipped.bin new-session.bin -o merged.bin

Records keep the order of their first appearance, so the first file stays a
prefix of the result. A manifest is append-only and the game skips records it
already has, which makes playing on top of an existing one (copy it into
pipeline-cache\\ first) the same as merging afterwards; this is for combining
files from separate sessions or machines. See docs/roadmap.md.
"""
import argparse
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


def merge(paths):
    seen, merged = set(), []
    for path in paths:
        for record in load(path):
            if record not in seen:
                seen.add(record)
                merged.append(record)
    return merged


def write(path, records):
    with open(path, 'wb') as out:
        out.write(MAGIC)
        for record in records:
            out.write(struct.pack('<I', len(record)) + record)


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('manifests', nargs='+')
    parser.add_argument('-o', '--output', required=True)
    args = parser.parse_args(argv[1:])
    merged = merge(args.manifests)
    write(args.output, merged)
    counts = [len(load(p)) for p in args.manifests]
    print(f'{len(merged)} pipelines from {sum(counts)} records in {len(counts)} files -> {args.output}')


if __name__ == '__main__':
    main(sys.argv)

#!/usr/bin/env python3
"""Keep the generated code's label checkpoints only where a loop can close.

generate_diagnostic.py puts sfr::guest_checkpoint(); after every loc_ label,
so the permit is offered at least once per pass of every loop. Most labels
are targets of forward branches only (if/else joins, early exits): code that
reaches one has not gone round anything since the last checkpoint. Every
function entry checkpoints too (sfr::enter_function), so any code running
for a long time still passes a checkpoint as long as each loop keeps one:
those are the labels something later in the same function refers to (a goto
or a jump table case, which is how every backward branch is written).

The checkpoint costs little each time (a thread_local countdown), but it is
also a call the compiler must assume can change anything, which ends what it
may keep in registers. This writes a copy of checked generated sources with
the checkpoint removed after each label nothing later in its function names,
and loops_report.json beside it:

    python scripts/loop_checkpoints.py out/recomp/diagnostic-fast out/recomp/diagnostic-loops

The permit counts checkpoints, not time (SFR_CHECKPOINT_INTERVAL, 256 by
default): with fewer of them each turn lasts longer. scripts/build_ab.ps1
-Loops builds this as sfr_cpu_diagnostic_d.exe.
"""
import argparse
import json
import re
import shutil
import sys
from pathlib import Path

FUNCTION = re.compile(r'^PPC_FUNC_IMPL\(', re.MULTILINE)
CHECKPOINTED = re.compile(r'^(loc_[0-9A-Fa-f]+):(\r?\n)\tsfr::guest_checkpoint\(\);\r?\n', re.MULTILINE)
MARKER = '// loop checkpoints only'


def thin_function(body):
    """One function's text without the checkpoints no later code can loop back
    to, and (kept, removed)."""
    kept = removed = 0
    pieces, cursor = [], 0
    for match in CHECKPOINTED.finditer(body):
        name = match[1]
        # Anything later naming the label, a goto, a jump table entry or even a
        # comment, keeps it: only a label nothing after it mentions is dropped.
        if re.search(r'\b' + name + r'\b', body[match.end():]):
            kept += 1
            continue
        pieces.append(body[cursor:match.start()])
        pieces.append(match[1] + ':' + match[2])
        cursor = match.end()
        removed += 1
    pieces.append(body[cursor:])
    return ''.join(pieces), kept, removed


def transform(source):
    """The source with only loop checkpoints, and (kept, removed)."""
    if MARKER in source:
        raise ValueError('already transformed')
    starts = [match.start() for match in FUNCTION.finditer(source)]
    if not starts:
        return source, 0, 0
    pieces = [source[:starts[0]]]
    kept = removed = 0
    for begin, end in zip(starts, starts[1:] + [len(source)]):
        body, k, r = thin_function(source[begin:end])
        pieces.append(body)
        kept += k
        removed += r
    if not removed:
        return source, kept, 0
    newline = '\r\n' if '\r\n' in source else '\n'
    return MARKER + newline + ''.join(pieces), kept, removed


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('source', type=Path)
    parser.add_argument('destination', type=Path)
    args = parser.parse_args()
    if not (args.source / 'report.json').is_file():
        sys.exit(f'{args.source} is not checked diagnostic output (no report.json)')
    if args.destination.exists():
        sys.exit(f'{args.destination} exists: remove it first')
    shutil.copytree(args.source, args.destination)
    kept = removed = files = 0
    for path in sorted(args.destination.glob('ppc_recomp.*.cpp')):
        with open(path, encoding='utf-8', newline='') as source:
            text = source.read()
        changed, k, r = transform(text)
        kept += k
        removed += r
        if r:
            path.write_text(changed, encoding='utf-8', newline='')
            files += 1
    if not kept + removed:
        sys.exit('no label checkpoints found: is this generated code?')
    (args.destination / 'loops_report.json').write_text(
        json.dumps({'source': str(args.source), 'kept': kept, 'removed': removed, 'files': files}, indent=2) + '\n',
        encoding='utf-8')
    print(f'label checkpoints: {kept} kept at loops, {removed} removed, in {files} files: {args.destination}')


if __name__ == '__main__':
    main()

#!/usr/bin/env python3
"""Make generated functions take GuestMemory's fast path once, at entry.

Every guest load and store in the generated code goes through
GuestMemory::load/store on sfr::active_memory. The build has no strict
aliasing, so after each guest store the compiler must load active_memory,
the page table pointer and the base from memory again before the next
access. SFR_FAST_PATH() (src/diagnostic_hooks.h) copies them into locals
once; the PPC_LOAD/PPC_STORE macros then use those locals and the function's
own base argument, and decide exactly as GuestMemory::load/store do.

This writes a copy of the checked diagnostic sources with SFR_FAST_PATH();
after each function's PPC_FUNC_PROLOGUE(); (nothing else changes), and
fast_report.json beside it:

    python scripts/fast_guest_access.py out/recomp/diagnostic out/recomp/diagnostic-fast

Build it with scripts/build_tools.ps1 -Diagnostic -DiagnosticDirectory
out/recomp/diagnostic-fast, or both at once with scripts/build_ab.ps1.
"""
import argparse
import json
import re
import shutil
import sys
from pathlib import Path

PROLOGUE = re.compile(r'^(\t+)PPC_FUNC_PROLOGUE\(\);(\r?\n)', re.MULTILINE)
MARKER = 'SFR_FAST_PATH();'


def transform(source):
    """The source with SFR_FAST_PATH(); after every prologue, and how many."""
    if MARKER in source:
        raise ValueError('already transformed')
    return PROLOGUE.subn(lambda m: f'{m[1]}PPC_FUNC_PROLOGUE();{m[2]}{m[1]}{MARKER}{m[2]}', source)


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
    functions = files = 0
    for path in sorted(args.destination.glob('ppc_recomp.*.cpp')):
        with open(path, encoding='utf-8', newline='') as source:
            text = source.read()
        changed, count = transform(text)
        if count:
            path.write_text(changed, encoding='utf-8', newline='')
            functions += count
            files += 1
    if not functions:
        sys.exit('no PPC_FUNC_PROLOGUE(); found: is this generated code?')
    (args.destination / 'fast_report.json').write_text(
        json.dumps({'source': str(args.source), 'functions': functions, 'files': files}, indent=2) + '\n',
        encoding='utf-8')
    print(f'SFR_FAST_PATH in {functions} functions of {files} files: {args.destination}')


if __name__ == '__main__':
    main()

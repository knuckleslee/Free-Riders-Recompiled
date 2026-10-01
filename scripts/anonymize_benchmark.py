#!/usr/bin/env python3
"""Makes a copy of a benchmark folder that is safe to send to someone else.

    py scripts\\anonymize_benchmark.py out\\bench\\20260930-151014
    py scripts\\anonymize_benchmark.py out\\bench\\20260930-151014 --also MYPC --keep-screenshots

Writes <folder>-shareable beside it (and a .zip of it) and leaves the original
alone. What the logs hold that says who ran them, and what it becomes:

- the Windows user name in every path (C:\\Users\\<name>\\...): C:\\Users\\USER\\...
  (the same for /home/<name> and /Users/<name>);
- the folder the generated code was found in (info.txt "generated="): removed;
- the language and country the game was told (NATIVE_USER_LANGUAGE/COUNTRY):
  the values are replaced;
- the save-profile id folder the game wrote to (E000 and twelve hex digits):
  E000XXXXXXXXXXXX;
- anything you name with --also (a computer name, your own name): XXXX;
- screenshots (*.bmp): left out unless --keep-screenshots, as they show whatever
  was on the screen.

Kept, because a comparison needs it: the commit, the CPU and GPU names, the
Windows version and every timing. Read the result before sending it: this
removes what the tool knows to look for, not everything that could identify you.
"""
import argparse
import re
import shutil
import sys
from pathlib import Path

TEXT = {'.log', '.txt', '.md', '.json', '.out', '.csv', '.map'}


def patterns(also):
    rules = [
        # A Windows user name may contain spaces: it ends at the next slash.
        (rb'(?i)([A-Z]:[\\/]Users[\\/])[^\\/"\'\r\n]+', rb'\1USER'),
        (rb'(/home/|/Users/)[^/\s"\']+', rb'\1USER'),
        (rb'(?m)^generated=.*$', rb'generated=<removed>'),
        (rb'windows_langid=0x[0-9A-Fa-f]+ xbox_language=\d+', rb'windows_langid=0x0 xbox_language=0'),
        (rb'(NATIVE_USER_COUNTRY source=\S+ )iso=\S+ xbox_country=\d+', rb'\1iso=XX xbox_country=0'),
        (rb'E000[0-9A-Fa-f]{12}', rb'E000XXXXXXXXXXXX'),
    ]
    for word in also:
        rules.append((re.escape(word.encode()), b'XXXX'))
    return [(re.compile(pattern), replacement) for pattern, replacement in rules]


def scrub(data, rules):
    changed = 0
    for pattern, replacement in rules:
        data, count = pattern.subn(replacement, data)
        changed += count
    return data, changed


def anonymize(source, destination, also=(), keep_screenshots=False):
    """Copies source to destination scrubbed; returns (files copied, replacements, files left out)."""
    rules = patterns(also)
    copied = replaced = left_out = 0
    for path in sorted(Path(source).rglob('*')):
        if not path.is_file():
            continue
        target = Path(destination) / path.relative_to(source)
        suffix = path.suffix.lower()
        if suffix == '.bmp' and not keep_screenshots:
            left_out += 1
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        if suffix in TEXT or path.name == 'info.txt':
            data, count = scrub(path.read_bytes(), rules)
            target.write_bytes(data)
            replaced += count
        else:
            shutil.copyfile(path, target)
        copied += 1
    return copied, replaced, left_out


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('folder')
    parser.add_argument('--also', action='append', default=[], metavar='WORD',
                        help='another word to blank out (a computer name, your name); may be repeated')
    parser.add_argument('--keep-screenshots', action='store_true')
    args = parser.parse_args(argv[1:])
    source = Path(args.folder)
    if not source.is_dir():
        raise SystemExit(f'{source}: not a folder')
    destination = source.with_name(source.name + '-shareable')
    if destination.exists():
        shutil.rmtree(destination)
    copied, replaced, left_out = anonymize(source, destination, args.also, args.keep_screenshots)
    archive = shutil.make_archive(str(destination), 'zip', destination)
    print(f'{copied} files copied, {replaced} replacements, {left_out} screenshots left out')
    print(f'Folder: {destination}')
    print(f'Zip:    {archive}')
    print('Read info.txt and one log before sending: only what this tool looks for is removed.')


if __name__ == '__main__':
    main(sys.argv)

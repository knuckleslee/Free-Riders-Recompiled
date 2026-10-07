#!/usr/bin/env python3
"""Where the main thread's time goes, from SFR_MAIN_PROFILE samples.

The host profiler (diagnostic_main.cpp) samples the main guest thread's
instruction pointer every millisecond and prints, at the end of the run,
one 'HOST_PROFILE rva=0x... count' line per address, relative to the
executable's base. The linker's map (sfr_cpu_diagnostic.map, beside the
executable) names the function and object file holding each address.

    python scripts/profile_summary.py out/bench/<run> [--map path] [--top 40]

reads every *.log in the directory that has HOST_PROFILE lines, prints a
table per category of code (the recompiled game, guest memory access, the
renderer, ...) and the hottest functions, and writes profile.md.

With more than one profiled setting (profile and profile-skip-draws, named by
the log: <setting>-<run>.log) each gets its own table, and a comparison in ms a
frame says where the time goes that a setting saves: a function's share of the
samples times the mean frame time of the sampled presents (from SFR_PROFILE_AFTER,
12200 in the benchmark: the race). The frame that drawing nothing saves is
larger than the drawing timers (docs/benchmark.md); the functions that shrink
without drawing are where the rest is.
"""
import argparse
import bisect
import re
from pathlib import Path

SAMPLE = re.compile(r'^HOST_PROFILE rva=0x([0-9a-f]+) (\d+)')
PRESENT = re.compile(r'^NATIVE_PRESENT .* frame=(\d+) .* seconds=([0-9.]+)')
PROFILE_AFTER = 12200  # scripts/benchmark.ps1, the profile settings' SFR_PROFILE_AFTER
# MSVC link.exe: " 0001:00000000       ?name@@...    0000000140001000 f   object.obj"
MSVC_SYMBOL = re.compile(r'^\s*[0-9a-f]{4}:[0-9a-f]{8}\s+(\S+)\s+([0-9a-f]{16})\s+(?:f\s+)?(?:i\s+)?(\S+)\s*$', re.I)
MSVC_BASE = re.compile(r'Preferred load address is ([0-9a-f]+)', re.I)
# What the object files are, in the order they are tried.
CATEGORIES = (
    ('遊戲生成碼', re.compile(r'ppc_recomp|ppc_func_mapping', re.I)),
    ('客體記憶體存取', re.compile(r'guest_memory|vector_memory|memory_update|load_halfword|store_halfword|store_float', re.I)),
    ('執行許可與排程', re.compile(r'guest_execution|guest_threads|native_sync|critical_section', re.I)),
    ('畫圖', re.compile(r'guest_graphics|native_renderer|native_presentation|native_graphics|native_shaders|native_render|'
                       r'native_raster|native_formats|texture|plume|d3d12|vulkan|vma', re.I)),
    ('HLE 與診斷', re.compile(r'diagnostic_main|nui_|xex_module|virtual_memory|physical_memory|native_', re.I)),
    ('C/C++ 執行階段', re.compile(r'libcmt|libvcruntime|libucrt|msvcprt|msvcrt|vcruntime|ucrt|libcpmt', re.I)),
)
OUTSIDE = '執行檔以外（系統、驅動程式、等待中）'


def read_map(path):
    """Sorted [(rva, name, object)] from the linker map (link.exe's format,
    which lld-link /MAP writes too), public and static symbols alike."""
    text = Path(path).read_text(encoding='utf-8', errors='replace').splitlines()
    symbols, base = [], None
    for line in text:
        match = MSVC_BASE.search(line)
        if match:
            base = int(match.group(1), 16)
            continue
        match = MSVC_SYMBOL.match(line)
        if match and base is not None:
            name, address, obj = match.groups()
            address = int(address, 16)
            if address >= base:
                symbols.append((address - base, name, obj))
    symbols.sort()
    return symbols


def demangle(name):
    """A readable name for an MSVC-decorated one: ?draw@NativeRenderer@sfr@@... -> sfr::NativeRenderer::draw."""
    if not name.startswith('?') or name.startswith('?$') or name.startswith('??'):
        return name
    scope = name[1:].split('@@')[0].split('@')
    return '::'.join(reversed([part for part in scope if part]))


def category(obj):
    for title, pattern in CATEGORIES:
        if pattern.search(obj):
            return title
    return '其他'


def setting_of(log):
    """profile-skip-draws-2.log -> profile-skip-draws."""
    stem = Path(log).stem
    head, _, run = stem.rpartition('-')
    return head if head and run.isdigit() else stem


def read_samples(directory, setting=None):
    samples = {}
    for log in sorted(Path(directory).glob('*.log')):
        if setting is not None and setting_of(log) != setting:
            continue
        with open(log, encoding='utf-8', errors='replace') as lines:
            for line in lines:
                match = SAMPLE.match(line)
                if match:
                    rva = int(match.group(1), 16)
                    samples[rva] = samples.get(rva, 0) + int(match.group(2))
    return samples


def profiled_settings(directory):
    """The settings whose logs hold samples, in name order."""
    found = set()
    for log in Path(directory).glob('*.log'):
        with open(log, encoding='utf-8', errors='replace') as lines:
            if any(line.startswith('HOST_PROFILE rva=') for line in lines):
                found.add(setting_of(log))
    return sorted(found)


def frame_ms(directory, setting, after=PROFILE_AFTER):
    """Mean ms a present over the sampled presents of a setting's runs, or None."""
    seconds = frames = 0.0
    for log in sorted(Path(directory).glob('*.log')):
        if setting_of(log) != setting:
            continue
        first = last = None
        with open(log, encoding='utf-8', errors='replace') as lines:
            for line in lines:
                match = PRESENT.match(line)
                if match and int(match.group(1)) >= after:
                    point = (int(match.group(1)), float(match.group(2)))
                    first = first or point
                    last = point
        if first and last and last[0] > first[0]:
            frames += last[0] - first[0]
            seconds += last[1] - first[1]
    return 1000 * seconds / frames if frames else None


def attribute(samples, symbols):
    """Samples by (function, object file) and by category of code."""
    addresses = [rva for rva, _, _ in symbols]
    last = symbols[-1][0] + 0x100000 if symbols else 0
    by_function, by_category = {}, {}
    for rva, count in samples.items():
        index = bisect.bisect_right(addresses, rva) - 1
        # Past the image (a system DLL, the driver) or before its first symbol.
        if index < 0 or rva > last or rva >= 1 << 32:
            key, where = (OUTSIDE, ''), OUTSIDE
        else:
            _, name, obj = symbols[index]
            key, where = (demangle(name), obj), category(obj)
        by_function[key] = by_function.get(key, 0) + count
        by_category[where] = by_category.get(where, 0) + count
    return by_function, by_category


def summarise(samples, symbols, top=40):
    by_function, by_category = attribute(samples, symbols)
    total = sum(samples.values()) or 1
    lines = [f'主執行緒取樣 {total} 次（每 1 ms 一次）', '', '| 類別 | 比例 |', '| --- | ---: |']
    for where, count in sorted(by_category.items(), key=lambda item: -item[1]):
        lines.append(f'| {where} | {100 * count / total:.1f}% |')
    lines += ['', '| 函式 | 目的檔 | 比例 |', '| --- | --- | ---: |']
    for (name, obj), count in sorted(by_function.items(), key=lambda item: -item[1])[:top]:
        lines.append(f'| `{name}` | {obj} | {100 * count / total:.2f}% |')
    return '\n'.join(lines)


def compare(directory, settings, symbols, top=40):
    """ms a frame of each category and function in each setting, the first
    setting against each other one, largest saving first."""
    columns = []
    for setting in settings:
        samples = read_samples(directory, setting)
        ms = frame_ms(directory, setting)
        if not samples or not ms:
            continue
        total = sum(samples.values())
        by_function, by_category = attribute(samples, symbols)
        columns.append((setting, ms, {k: ms * v / total for k, v in by_function.items()},
                        {k: ms * v / total for k, v in by_category.items()}))
    if len(columns) < 2:
        return ''
    base = columns[0]
    names = [f'{name} ms' for name, _, _, _ in columns] + [f'{base[0]} − {name}' for name, _, _, _ in columns[1:]]
    rule = '| --- |' + ' ---: |' * len(names)
    lines = ['## 每格 ms 比較', '',
             f'每個函式的取樣比例乘以該設定取樣期間（第 {PROFILE_AFTER} 次 present 起）的平均每格時間。'
             f'最後幾欄是 {base[0]} 比另一個設定多花的 ms：省下的時間在哪裡。', '',
             '| 類別 | ' + ' | '.join(names) + ' |', rule]
    lines.append('| **整格** | ' + ' | '.join(f'{ms:.2f}' for _, ms, _, _ in columns) + ' | ' +
                 ' | '.join(f'{base[1] - ms:+.2f}' for _, ms, _, _ in columns[1:]) + ' |')
    def rows(index, label, limit):
        keys = set().union(*(column[index] for column in columns))
        ranked = sorted(keys, key=lambda k: -(base[index].get(k, 0) - columns[1][index].get(k, 0)))
        out = []
        for key in ranked[:limit]:
            values = [column[index].get(key, 0) for column in columns]
            out.append(f'| {label(key)} | ' + ' | '.join(f'{v:.2f}' for v in values) + ' | ' +
                       ' | '.join(f'{values[0] - v:+.2f}' for v in values[1:]) + ' |')
        return out
    lines += rows(3, lambda k: k, len(CATEGORIES) + 2)
    lines += ['', '| 函式 | ' + ' | '.join(names) + ' |', rule]
    lines += rows(2, lambda k: f'`{k[0]}` ({k[1]})' if k[1] else k[0], top)
    return '\n'.join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('directory')
    parser.add_argument('--map', help='the linker map (default: sfr_cpu_diagnostic.map in the directory)')
    parser.add_argument('--top', type=int, default=40)
    args = parser.parse_args()
    samples = read_samples(args.directory)
    if not samples:
        print('No HOST_PROFILE samples (run the benchmark with -Configs profile).')
        return
    map_path = Path(args.map) if args.map else Path(args.directory) / 'sfr_cpu_diagnostic.map'
    symbols = read_map(map_path) if map_path.exists() else []
    if not symbols:
        print(f'No symbols read from {map_path}; every sample counts as outside the executable.')
    settings = profiled_settings(args.directory)
    if len(settings) > 1:
        # 'profile' first: the comparison is against it
        settings.sort(key=lambda name: (name != 'profile', name))
        parts = [compare(args.directory, settings, symbols, args.top)]
        for setting in settings:
            parts.append(f'## {setting}\n\n' + summarise(read_samples(args.directory, setting), symbols, args.top))
        table = '\n\n'.join(part for part in parts if part)
    else:
        table = summarise(samples, symbols, args.top)
    print(table)
    (Path(args.directory) / 'profile.md').write_text(table + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()

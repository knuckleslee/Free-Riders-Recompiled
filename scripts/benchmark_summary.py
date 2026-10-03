#!/usr/bin/env python3
"""Sums up the race frames of benchmark runs (scripts/benchmark.ps1).

Each run is a game log, stderr of sfr_cpu_diagnostic, with one NATIVE_PRESENT
line a frame. Only the frames of the race count (racing=1), less the first
--skip of them (the countdown, and shaders and pipelines made on first use).
A frame's time is the gap between its present and the one before.

    python scripts/benchmark_summary.py out/bench/<run> [--skip 600]

prints a table per configuration and writes it to <run>/summary.md.

The same number of frames is a shorter stretch of the race for a faster
setting, and the start of a race (the countdown, the first straight) is
lighter than what follows, so the frame-count table flatters whichever is
faster (+43% where the same seconds of the race gave +18%). A second
comparison counts the frames of one stretch of race time, the same for every
run: from --window-start seconds after the race began to the end of the
shortest run's race, at most --window-end.
"""
import argparse
import re
import statistics
from pathlib import Path

PRESENT = re.compile(r'^NATIVE_PRESENT .*\bframe=(\d+)\b')
FIELD = re.compile(r'\b([a-z_0-9]+)=([^\s]+)')
# The per-frame costs worth comparing, averaged over the measured frames.
AVERAGED = ('draw_ms', 'present_ms', 'gpu_wait_ms', 'main_queued_ms', 'main_blocked_ms', 'main_ready_ms')


def holder_ms(text, guest):
    """What 'holders=1:12.30,7:3.10' says the guest thread held, in ms."""
    for item in text.split(','):
        who, _, ms = item.partition(':')
        if who == str(guest):
            return float(ms)
    return 0.0


def read_run(path, skip):
    """The measured race frames of one log, and how the run ended."""
    frames, racing_seen, stop = [], 0, None
    previous = None
    race_start = None
    timeline = []  # (seconds since the race began, frame ms) of every race frame
    with open(path, encoding='utf-8', errors='replace') as log:
        for line in log:
            if line.startswith('STOP '):
                stop = line.strip()
                continue
            if line.startswith('HANG_REPORT') and not stop:
                stop = line.strip()
                continue
            if not PRESENT.match(line):
                continue
            fields = dict(FIELD.findall(line))
            seconds = float(fields.get('seconds', 'nan'))
            gap_ms = (seconds - previous) * 1000 if previous is not None else None
            previous = seconds
            if fields.get('racing') != '1':
                continue
            racing_seen += 1
            if race_start is None:
                race_start = seconds
            if gap_ms is not None:
                timeline.append((seconds - race_start, gap_ms))
            if racing_seen <= skip or gap_ms is None:
                continue
            frame = {'ms': gap_ms, 'draws': int(fields.get('draws', 0)),
                     'pipelines': int(fields.get('pipelines', 0)), 'pipeline_ms': float(fields.get('pipeline_ms', 0))}
            for name in AVERAGED:
                frame[name] = float(fields.get(name, 0))
            frame['main_held_ms'] = holder_ms(fields.get('holders', ''), 1)
            frames.append(frame)
    ended = 'present-limit' if stop and 'present-limit' in stop else (stop or 'no STOP line (killed or crashed)')
    read_run.timeline = timeline
    return frames, racing_seen, ended


def window_fps(timeline, start, end):
    """Frames per second over [start, end) seconds of the race."""
    times = [ms for t, ms in timeline if start <= t < end]
    return 1000 * len(times) / sum(times) if times else None


REASON = re.compile(r'^PARALLEL_REASON reason=(0x[0-9a-f]+) kind=(\w+)(?: name=(\S+))?')
KINDS = {1: 'import', 2: 'hook', 3: 'memory'}


def read_reasons(path, skip=0):
    """Why the main thread queued during the race (SFR_PARALLEL_HELD=1):
    {(guest, reason): ms} summed over the measured race frames, the names of
    the reasons, and how many frames were summed."""
    queued, names, racing_seen, frames = {}, {}, 0, 0
    with open(path, encoding='utf-8', errors='replace') as log:
        for line in log:
            match = REASON.match(line)
            if match:
                names[int(match.group(1), 16)] = (match.group(2), match.group(3) or '')
                continue
            if not line.startswith('NATIVE_PRESENT') or ' racing=1 ' not in line:
                continue
            racing_seen += 1
            if racing_seen <= skip:
                continue
            frames += 1
            fields = dict(FIELD.findall(line))
            for item in filter(None, fields.get('main_blockers_by_reason', '').split(',')):
                guest, reason, ms = item.split(':')
                key = (int(guest), int(reason, 16))
                queued[key] = queued.get(key, 0.0) + float(ms)
    return queued, names, frames


def held_table(directory, skip=0, top=15):
    """The reasons the main thread queued behind longest, per race frame,
    over every run that reported them."""
    total, names, frames = {}, {}, 0
    for log in sorted(Path(directory).glob('*.log')):
        queued, run_names, run_frames = read_reasons(log, skip)
        if not queued:
            continue
        names.update(run_names)
        frames += run_frames
        for key, ms in queued.items():
            total[key] = total.get(key, 0.0) + ms
    if not total or not frames:
        return ''
    lines = ['主執行緒排隊時，擋住它的執行緒在做什麼（比賽中，每格平均）：', '',
             '| 客體 | 種類 | 位址 | 名稱 | 每格 ms |', '| ---: | --- | --- | --- | ---: |']
    for (guest, reason), ms in sorted(total.items(), key=lambda item: -item[1])[:top]:
        if reason == 0:
            kind, address, name = '自己的程式', '', ''
        else:
            kind, name = names.get(reason, (KINDS.get(reason >> 32, '?'), ''))
            address = f'0x{reason & 0xFFFFFFFF:08x}'
        lines.append(f'| {guest} | {kind} | {address} | {name} | {ms / frames:.3f} |')
    return '\n'.join(lines)


def percentile(values, fraction):
    ordered = sorted(values)
    return ordered[min(len(ordered) - 1, int(fraction * len(ordered)))]


def describe(frames):
    times = [f['ms'] for f in frames]
    result = {
        'frames': len(frames),
        'median_ms': statistics.median(times),
        'p95_ms': percentile(times, 0.95),
        'p99_ms': percentile(times, 0.99),
        'fps': 1000 * len(times) / sum(times),
        'draws': statistics.median(f['draws'] for f in frames),
    }
    for name in AVERAGED + ('main_held_ms',):
        result[name] = statistics.fmean(f[name] for f in frames)
    # What a player feels is the long frames, not the mean: how many there are,
    # and how many of them are a pipeline made on the spot (docs/roadmap.md: 0 to 2 a race).
    result['slow50'] = sum(1 for t in times if t > 50)
    result['slow80'] = sum(1 for t in times if t > 80)
    result['hitch30'] = sum(1 for f in frames if f['pipeline_ms'] >= 5 and f['ms'] > 30)
    result['pipelines'] = sum(f['pipelines'] for f in frames)
    return result


COLUMNS = (('fps', '平均 fps', '{:.1f}'), ('median_ms', '中位數 ms', '{:.1f}'), ('p95_ms', 'P95 ms', '{:.1f}'),
           ('p99_ms', 'P99 ms', '{:.1f}'), ('main_held_ms', '主執行緒持有 ms', '{:.1f}'),
           ('main_queued_ms', '主執行緒排隊 ms', '{:.1f}'), ('draw_ms', '繪製 ms', '{:.1f}'),
           ('present_ms', 'present ms', '{:.1f}'), ('slow50', '>50 ms 格', '{}'), ('hitch30', '編譯卡頓格', '{}'),
           ('pipelines', '現場建 pipeline', '{}'), ('frames', '量到的格數', '{}'))


MINIMUM_WINDOW = 15.0  # seconds; a shorter common stretch says too little


def race_window(rows, start, end):
    """[start, end) seconds of race time that every comparable run covers."""
    good, _ = valid_runs(rows)
    if not good:
        return None
    end = min([end] + [stats['race_seconds'] for _, _, stats in good])
    return (start, end) if end - start >= MINIMUM_WINDOW else None


def summarise(directory, skip, window_start=20.0, window_end=75.0):
    """(markdown table, rows) for every <config>-<n>.log in directory."""
    runs = {}
    for log in sorted(Path(directory).glob('*.log')):
        config, _, repeat = log.stem.rpartition('-')
        if not config or not repeat.isdigit():
            continue
        runs.setdefault(config, []).append((int(repeat), log))
    lines = ['| 設定 | 趟 | ' + ' | '.join(title for _, title, _ in COLUMNS) + ' | 結束 |',
             '| --- | --- |' + ' ---: |' * len(COLUMNS) + ' --- |']
    rows = []
    # The warm-up first, then the settings in the order they ran.
    for config, logs in sorted(runs.items(), key=lambda item: (item[0] != 'warmup', min(p.stat().st_mtime for _, p in item[1]))):
        for repeat, log in sorted(logs):
            frames, seen, ended = read_run(log, skip)
            if not frames:
                note = f'沒有比賽畫面（racing 格數 {seen}）；{ended}'
                lines.append(f'| {config} | {repeat} | ' + ' | '.join('-' for _ in COLUMNS) + f' | {note} |')
                rows.append((config, repeat, None, ended))
                continue
            stats = describe(frames)
            stats['timeline'] = read_run.timeline
            stats['race_seconds'] = read_run.timeline[-1][0] if read_run.timeline else 0
            rows.append((config, repeat, stats, ended))
            lines.append(f'| {config} | {repeat} | ' +
                         ' | '.join(fmt.format(stats[key]) for key, _, fmt in COLUMNS) + f' | {ended} |')
    lines += ['', compare(rows)]
    window = race_window(rows, window_start, window_end)
    if window:
        start, end = window
        for _, _, stats, _ in rows:
            if stats:
                stats['window_fps'] = window_fps(stats['timeline'], start, end)
        lines += ['', f'### 同一段比賽時間（開賽後 {start:.0f}–{end:.0f} 秒）', '',
                  '每趟都只算這段時間裡的畫面，所以較快的設定不會因為量到比較多比賽前段的輕畫面而佔便宜。',
                  '判定以這張表為準。這張表只有平均 fps 與逐輪比值是這段時間的，中位數、P95 與 >50 ms 格沿用整趟。', '',
                  compare([r for r in rows if not r[2] or r[2].get('window_fps')], metric='window_fps')]
    else:
        lines += ['', f'（比賽太短，湊不出 {MINIMUM_WINDOW:.0f} 秒以上的共同時段，沒有同時段的比較。）']
    held = held_table(directory, skip)
    if held:
        lines += ['', held]
    return '\n'.join(lines), rows


MINIMUM_ROUNDS = 4  # fewer pairs than this prove nothing, whatever the ratios
EXPECTED_FRAMES_TOLERANCE = 0.02  # a run with fewer race frames than the others did not play the same race


def valid_runs(rows):
    """(config, repeat, stats) of the runs that can be compared, and why the others cannot."""
    counts = [stats['frames'] for config, _, stats, ended in rows
              if config != 'warmup' and stats and 'present-limit' in ended]
    expected = statistics.median(counts) if counts else 0
    good, problems = [], []
    for config, repeat, stats, ended in rows:
        if config == 'warmup':
            continue
        if not stats:
            problems.append(f'{config}-{repeat}: 沒有走進比賽，沒有可量的比賽畫面（{ended}）')
        elif 'present-limit' not in ended:
            problems.append(f'{config}-{repeat}: 沒有跑到 present-limit（{ended}）')
        elif abs(stats['frames'] - expected) > EXPECTED_FRAMES_TOLERANCE * expected:
            problems.append(f"{config}-{repeat}: 比賽格數 {stats['frames']}，其他趟約 {expected:.0f}")
        else:
            good.append((config, repeat, stats))
    return good, problems


def compare(rows, reference_name=None, metric='fps'):
    """Each setting against baseline (or --reference, or exe-a when there is no baseline): the median run, then round by round.

    The settings take turns (benchmark.ps1), so run n of each belongs to the same
    stretch of the PC's day. The paired ratios are what says whether a setting
    differs: only a change that is the same way in most rounds and larger than
    the baseline's own spread is called one.
    """
    good, problems = valid_runs(rows)
    by_config = {}
    for config, repeat, stats in good:
        by_config.setdefault(config, {})[repeat] = stats
    if not by_config:
        return '沒有任何一趟可以比較。\n' + '\n'.join(f'- {p}' for p in problems)
    lines = []
    if problems:
        lines += ['**以下幾趟不納入比較：**'] + [f'- {p}' for p in problems] + ['']
    median = {config: {key: statistics.median(s[key] for s in runs.values()) for key in (metric, 'median_ms', 'p95_ms', 'slow50')}
              for config, runs in by_config.items()}
    # The reference is baseline; an exe-a / exe-b comparison has none, so exe-a stands in.
    ref = reference_name or ('baseline' if 'baseline' in by_config else 'exe-a' if 'exe-a' in by_config else sorted(by_config)[0])
    if ref not in by_config:
        return '\n'.join(lines + [f'沒有 {ref} 這個設定可當對照。'])
    reference = median.get(ref)
    lines += [f'| 設定 | 趟數 | 平均 fps | 中位數 ms | P95 ms | >50 ms 格 | 相對 {ref} |', '| --- | ---: | ---: | ---: | ---: | ---: | ---: |']
    for config, value in median.items():
        change = f"{(value[metric] / reference[metric] - 1) * 100:+.0f}%" if reference else '-'
        lines.append(f"| {config} | {len(by_config[config])} | {value[metric]:.1f} | {value['median_ms']:.1f} | "
                     f"{value['p95_ms']:.1f} | {value['slow50']:.0f} | {change} |")
    baseline = by_config.get(ref, {})
    if len(baseline) >= 2:
        fps = [s[metric] for s in baseline.values()]
        spread = (max(fps) - min(fps)) / statistics.median(fps)
        lines += ['', f'{ref} 自己各趟之間的差距（最大減最小，除以中位數）：**{spread * 100:.0f}%**'
                      f'（{min(fps):.1f}–{max(fps):.1f} fps）。其他設定的差距必須明顯大於這個數字才算數。']
        lines += ['', f'逐輪成對比較（同一輪的該設定 fps 除以 {ref} 的 fps）：', '',
                  '| 設定 | 每輪比值 | 中位數 | 較快的輪數 | 判定 |', '| --- | --- | ---: | ---: | --- |']
        for config, runs in by_config.items():
            if config == ref:
                continue
            rounds = sorted(set(runs) & set(baseline))
            if len(rounds) < 2:
                continue
            ratios = [runs[r][metric] / baseline[r][metric] for r in rounds]
            middle = statistics.median(ratios)
            faster = sum(1 for r in ratios if r > 1)
            consistent = max(faster, len(ratios) - faster) / len(ratios) >= 0.8
            threshold = max(0.05, spread / 2)
            if len(ratios) < MINIMUM_ROUNDS:
                verdict = f'輪數不足（至少 {MINIMUM_ROUNDS} 輪才下判定）'
            elif abs(middle - 1) > threshold and consistent:
                verdict = '較快' if middle > 1 else '較慢'
            else:
                verdict = '不能判定（在雜訊內）'
            lines.append(f"| {config} | {' '.join(f'{r:.2f}' for r in ratios)} | {middle:.2f} | {faster}/{len(ratios)} | {verdict} |")
    else:
        lines += ['', f'{ref} 不到兩趟，無法估計雜訊，也無法逐輪比較。']
    return '\n'.join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('directory')
    parser.add_argument('--skip', type=int, default=600, help='race frames left out at the start (default 600)')
    parser.add_argument('--window-start', type=float, default=20.0, help='race seconds where the common stretch begins')
    parser.add_argument('--window-end', type=float, default=75.0, help='race seconds where it ends at the latest')
    args = parser.parse_args()
    table, _ = summarise(args.directory, args.skip, args.window_start, args.window_end)
    print(table)
    (Path(args.directory) / 'summary.md').write_text(table + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()

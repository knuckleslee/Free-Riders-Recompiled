#!/usr/bin/env python3
"""Whether every build and setting of a smoke run works, before any of them is timed.

scripts/smoke.ps1 runs each setting once, a short race each. This reads the
logs of such a run and says for each one whether it

- ended at its present limit (not a crash or the time limit), with no hang report,
- reached the race and presented enough race frames,
- drew something in them (draws per race frame above zero),
- shows the marks of the feature it is about: the main core reserved, guest
  threads in parallel, the outside-the-executable profile, the index cache.

    python scripts/smoke_check.py out/bench/<run>

prints the table and writes smoke.md beside the logs. Nothing in it comes from
the game's code or names the PC: setting names, counts, how a run ended (its
category only) and a frame rate that is not a comparison (one short run each).
"""
import argparse
import re
import sys
from pathlib import Path

PRESENT = re.compile(r'^NATIVE_PRESENT ')
FIELD = re.compile(r'\b([a-z_0-9]+)=([^\s]+)')
STOP = re.compile(r'^STOP (\S+)')
MIN_RACE_FRAMES = 300

# Lines a setting's feature leaves in the log when it is in effect: (what, pattern, minimum count).
MARKS = {
    'main-core': [('主核心保留', re.compile(r'^MAIN_CORE_RESERVED '), 1)],
    'all-main-core': [('主核心保留', re.compile(r'^MAIN_CORE_RESERVED '), 1),
                      ('並行客體執行緒', re.compile(r'^PARALLEL_WORKER '), 3)],
    'all': [('並行客體執行緒', re.compile(r'^PARALLEL_WORKER '), 3)],
    'all-stress': [('並行客體執行緒', re.compile(r'^PARALLEL_WORKER '), 3)],
    'profile': [('剖析樣本', re.compile(r'^HOST_PROFILE rva='), 1),
                ('執行檔以外的 DLL', re.compile(r'^HOST_PROFILE_OUTSIDE '), 1)],
}


def check_log(path, config):
    """{'ended', 'race_frames', 'draws', 'cached', 'fps', 'marks', 'problems'} for one run."""
    ended, race_frames, draws, cached, has_cached, hangs = None, 0, 0, 0, False, 0
    seconds, counts = [], {what: 0 for what, _, _ in MARKS.get(config, [])}
    with open(path, encoding='utf-8', errors='replace') as log:
        for line in log:
            stop = STOP.match(line)
            if stop:
                ended = stop.group(1)
                continue
            if line.startswith('HANG_REPORT'):
                hangs += 1
                if ended is None:
                    ended = 'hang-report'
                continue
            for what, pattern, _ in MARKS.get(config, []):
                if pattern.match(line):
                    counts[what] += 1
            if not PRESENT.match(line):
                continue
            fields = dict(FIELD.findall(line))
            if fields.get('racing') != '1':
                continue
            race_frames += 1
            draws += int(fields.get('draws', 0) or 0)
            if 'cached_index_draws' in fields:
                has_cached = True
                cached += int(fields['cached_index_draws'] or 0)
            try:
                seconds.append(float(fields['seconds']))
            except (KeyError, ValueError):
                pass
    problems = []
    if ended != 'present-limit':
        problems.append(f'結束方式：{ended or "沒有 STOP（被強制結束或當掉）"}')
    if hangs:
        problems.append(f'卡住報告 {hangs} 次（畫面停住超過 SFR_HANG_SECONDS）')
    if race_frames < MIN_RACE_FRAMES:
        problems.append(f'比賽畫面只有 {race_frames} 格')
    if race_frames and draws == 0:
        problems.append('比賽中沒有畫任何東西')
    if has_cached and race_frames and cached == 0:
        problems.append('索引快取沒有命中')
    for what, _, minimum in MARKS.get(config, []):
        if counts[what] < minimum:
            problems.append(f'沒看到「{what}」的記錄')
    fps = (len(seconds) - 1) / (seconds[-1] - seconds[0]) if len(seconds) > 1 and seconds[-1] > seconds[0] else None
    return {'ended': ended, 'race_frames': race_frames, 'draws': draws / race_frames if race_frames else 0,
            'cached': cached if has_cached else None, 'fps': fps, 'marks': counts, 'problems': problems}


def runs(directory):
    """(config, log) of every run but the warmup, in name order."""
    for log in sorted(Path(directory).glob('*.log')):
        match = re.match(r'^(.+)-(\d+)$', log.stem)
        if match and match.group(1) != 'warmup':
            yield match.group(1), log


def report(directory):
    rows = [(config, log.name, check_log(log, config)) for config, log in runs(directory)]
    lines = ['# 功能檢查（每個設定一趟短比賽）', '',
             '幀率只是確認有在跑，一趟短比賽不能拿來比較快慢。', '',
             '| 設定 | 結果 | 比賽格數 | 每格繪製 | 索引快取命中 | 幀率 | 功能記錄 | 問題 |',
             '| --- | --- | ---: | ---: | ---: | ---: | --- | --- |']
    for config, _, result in rows:
        marks = '、'.join(f'{what} {count}' for what, count in result['marks'].items()) or '—'
        lines.append('| {} | {} | {} | {:.0f} | {} | {} | {} | {} |'.format(
            config, '正常' if not result['problems'] else '**有問題**', result['race_frames'], result['draws'],
            '—' if result['cached'] is None else result['cached'],
            '—' if result['fps'] is None else f"{result['fps']:.1f}", marks, '；'.join(result['problems']) or '—'))
    failed = [config for config, _, result in rows if result['problems']]
    lines += ['', f'{len(rows) - len(failed)}/{len(rows)} 個設定正常。' if rows else '沒有找到記錄檔。']
    if failed:
        lines.append('有問題的：' + '、'.join(failed) + '。它們的完整記錄留在這台電腦，先不要拿去比較性能。')
    return '\n'.join(lines), bool(rows) and not failed


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('directory')
    args = parser.parse_args()
    text, ok = report(args.directory)
    print(text)
    (Path(args.directory) / 'smoke.md').write_text(text + '\n', encoding='utf-8')
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()

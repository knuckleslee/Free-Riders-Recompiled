from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import benchmark_summary as bench


def present(frame, seconds, racing, **fields):
    extra = ' '.join(f'{key}={value}' for key, value in fields.items())
    return (f'NATIVE_PRESENT source=0x824e65a0 device=0x71600000 lr=0x8249bc70 frame={frame} draws=800 {extra} '
            f'holders=1:20.00,7:3.00 racing={racing} presented=1 seconds={seconds:.3f}\n')


def write_log(directory, name, frame_ms, menu=3, race=6, stop='STOP present-limit @0x0: SFR_PRESENT_LIMIT reached'):
    lines, seconds = [], 0.0
    for frame in range(menu + race):
        racing = int(frame >= menu)
        seconds += (frame_ms if racing else 16) / 1000
        lines.append(present(frame, seconds, racing, draw_ms=5, main_queued_ms=2))
    if stop:
        lines.append(stop + '\n')
    (Path(directory) / name).write_text(''.join(lines), encoding='utf-8')


class BenchmarkSummaryTest(unittest.TestCase):
    def test_counts_only_race_frames_after_the_skipped_ones(self):
        with tempfile.TemporaryDirectory() as directory:
            write_log(directory, 'baseline-1.log', 40)
            frames, seen, ended = bench.read_run(Path(directory) / 'baseline-1.log', skip=2)
            self.assertEqual(seen, 6)
            self.assertEqual(len(frames), 4)
            self.assertTrue(all(abs(f['ms'] - 40) < 1e-6 for f in frames))
            self.assertEqual(frames[0]['main_held_ms'], 20.0)
            self.assertEqual(ended, 'present-limit')

    def test_the_first_race_frame_uses_the_gap_from_the_menu(self):
        with tempfile.TemporaryDirectory() as directory:
            write_log(directory, 'baseline-1.log', 40, menu=1, race=2)
            frames, _, _ = bench.read_run(Path(directory) / 'baseline-1.log', skip=0)
            self.assertEqual(len(frames), 2)

    def test_a_run_that_never_raced_or_stopped_is_reported(self):
        with tempfile.TemporaryDirectory() as directory:
            write_log(directory, 'serial-1.log', 40, race=0, stop='')
            table, rows = bench.summarise(directory, skip=0)
            self.assertIsNone(rows[0][2])
            self.assertIn('no STOP line', table)

    def test_compares_each_setting_with_baseline(self):
        with tempfile.TemporaryDirectory() as directory:
            write_log(directory, 'warmup-1.log', 50)
            write_log(directory, 'baseline-1.log', 40)
            write_log(directory, 'baseline-2.log', 40)
            write_log(directory, 'skip-draws-1.log', 20)
            table, rows = bench.summarise(directory, skip=0)
            self.assertEqual(sorted({row[0] for row in rows}), ['baseline', 'skip-draws', 'warmup'])
            self.assertIn('| baseline | 2 | 25.0 | 40.0 | 40.0 | +0% |', table)
            self.assertIn('| skip-draws | 1 | 50.0 | 20.0 | 20.0 | +100% |', table)
            # The warm-up is listed with the runs but not compared.
            runs, comparison = table.split('\n\n')
            self.assertIn('| warmup | 1 | 20.0', runs)
            self.assertNotIn('warmup', comparison)

    def test_sums_what_held_the_permit_during_the_race_only(self):
        with tempfile.TemporaryDirectory() as directory:
            held = ('PARALLEL_HELD guest=37 hook=0x824a3398 count=10 ms=4.5\n'
                    'PARALLEL_HELD guest=16 import=0x82acc5ec name=XNotifyGetNext count=20 ms=1.5\n')
            lines = [present(0, 0.016, 0), held, present(1, 0.032, 0)]  # the menu: not counted
            lines += [present(2 + i, 0.05 + i * 0.04, 1) for i in range(4)] + [held]
            (Path(directory) / 'held-1.log').write_text(''.join(lines), encoding='utf-8')
            table = bench.held_table(directory)
            self.assertIn('| 37 | hook | 0x824a3398 |  | 2.50 | 1.125 |', table)
            self.assertIn('| 16 | import | 0x82acc5ec | XNotifyGetNext | 5.00 | 0.375 |', table)
            self.assertLess(table.index('| 37 |'), table.index('| 16 |'))


if __name__ == '__main__':
    unittest.main()

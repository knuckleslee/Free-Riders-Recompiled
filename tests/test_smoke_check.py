import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import smoke_check  # noqa: E402


def race(frames, draws=120, cached=5, start=10.0, step=0.03):
    # 1600 frames of 0.03 s: 48 s of race, 18 of them past the intro.
    return ''.join(f'NATIVE_PRESENT frame={i} seconds={start + i * step:.3f} racing=1 draws={draws} '
                   f'cached_index_draws={cached}\n' for i in range(frames))


class SmokeCheck(unittest.TestCase):
    def write(self, directory, name, text):
        (Path(directory) / name).write_text(text, encoding='utf-8')

    def test_good_and_bad_runs(self):
        with tempfile.TemporaryDirectory() as directory:
            self.write(directory, 'warmup-1.log', 'STOP crash\n')  # not judged
            self.write(directory, 'baseline-1.log', race(1600) + 'STOP present-limit @0x0: reached\n')
            self.write(directory, 'main-core-1.log', 'MAIN_CORE_RESERVED mask=0x1\n' + race(1600) + 'STOP present-limit\n')
            self.write(directory, 'all-1.log', 'PARALLEL_WORKER guest_id=2\n' + race(1600) + 'STOP present-limit\n')
            self.write(directory, 'exe-e-1.log', race(100) + 'STOP guest-fault @0x82000000: x\n')
            self.write(directory, 'exe-d-1.log', race(1600, draws=0, cached=0))
            self.write(directory, 'profile-1.log', race(1600) + 'STOP present-limit\nHOST_PROFILE rva=0x10 5\n')
            text, ok = smoke_check.report(directory)
            self.assertFalse(ok)
            self.assertNotIn('| warmup', text)
            self.assertIn('| baseline | 正常 | 48 | 1600 | 120 | 8000 | 33.3 | — | — |', text)
            self.assertIn('| main-core | 正常 |', text)
            self.assertIn('沒看到「並行客體執行緒」的記錄', text)  # all: one worker, three wanted
            self.assertIn('結束方式：guest-fault；比賽只跑了 3 秒，沒有進入正式比賽（開場與倒數約 25 秒）；比賽畫面只有 100 格', text)
            self.assertNotIn('0x82000000', text)  # the category only, no game address
            self.assertIn('沒有 STOP（被強制結束或當掉）；比賽中沒有畫任何東西；索引快取沒有命中', text)
            self.assertIn('沒看到「執行檔以外的 DLL」的記錄', text)
            self.assertIn('2/6 個設定正常。', text)

    def test_all_good(self):
        with tempfile.TemporaryDirectory() as directory:
            self.write(directory, 'baseline-1.log', race(1600) + 'STOP present-limit\n')
            self.write(directory, 'other-1.log', race(1600) + 'STOP present-limit\n')
            text, ok = smoke_check.report(directory)
            self.assertTrue(ok)
            self.assertIn('2/2 個設定正常。', text)

    def test_a_hang_that_recovered_is_still_a_problem(self):
        with tempfile.TemporaryDirectory() as directory:
            self.write(directory, 'all-stress-1.log', race(1600) + 'HANG_REPORT presents=5\nSTOP present-limit\n')
            text, ok = smoke_check.report(directory)
            self.assertFalse(ok)
            self.assertIn('卡住報告 1 次', text)

    def test_a_run_that_ends_in_the_intro_is_a_problem(self):
        with tempfile.TemporaryDirectory() as directory:
            # the first smoke run: 768 frames, 11 s, all start line and countdown
            self.write(directory, 'baseline-1.log', race(768, step=0.015) + 'STOP present-limit\n')
            text, ok = smoke_check.report(directory)
            self.assertFalse(ok)
            self.assertIn('| baseline | **有問題** | 12 | 768 | — | 3840 | — | — | 比賽只跑了 12 秒', text)


if __name__ == '__main__':
    unittest.main()

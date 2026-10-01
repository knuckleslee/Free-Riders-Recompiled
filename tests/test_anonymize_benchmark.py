from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import anonymize_benchmark as anon

LOG = (
    'NATIVE_USER_LANGUAGE source=GetUserDefaultUILanguage windows_langid=0x404 xbox_language=8\n'
    'NATIVE_USER_COUNTRY source=GetUserDefaultGeoName iso=TW xbox_country=101\n'
    'RESULT NtCreateFile path=sav:\\SfrAllData.sav host=C:\\Users\\Alice Smith\\Documents\\game\\out\\bench\\x'
    '\\save-warmup-1\\E000C56DDFFD8EA0\\00000001\\SfrA\n'
    'RESULT host=c:/users/bob/AppData/x /home/carol/game MYPC-01 started\n'
    'NATIVE_PRESENT frame=0 draws=800 racing=1 presented=1 seconds=1.000\n'
)
INFO = ('commit=3872cb4\ngenerated=C:/Users/Alice/Documents/game/out/recomp/diagnostic-local\n'
        'cpu=Intel(R) Core(TM) i7-6850K CPU @ 3.60GHz\ngpu=NVIDIA GeForce RTX 3080 Ti\n')


class AnonymizeTest(unittest.TestCase):
    def run_it(self, **options):
        root = Path(tempfile.mkdtemp())
        source = root / 'run'
        source.mkdir()
        (source / 'a.log').write_text(LOG, encoding='utf-8')
        (source / 'info.txt').write_text(INFO, encoding='utf-8')
        (source / 'shot.bmp').write_bytes(b'BM')
        (source / 'tool.exe').write_bytes(b'Alice stays in a binary')
        destination = root / 'out'
        result = anon.anonymize(source, destination, **options)
        return destination, result

    def test_names_ids_and_locale_are_removed_and_the_timings_stay(self):
        destination, (copied, replaced, left_out) = self.run_it(also=['MYPC-01'])
        log = (destination / 'a.log').read_text(encoding='utf-8')
        info = (destination / 'info.txt').read_text(encoding='utf-8')
        for secret in ('Alice', 'bob', 'carol', 'MYPC-01', 'E000C56DDFFD8EA0', 'iso=TW', '0x404'):
            self.assertNotIn(secret, log + info)
        self.assertIn('C:\\Users\\USER\\Documents\\game', log)
        self.assertIn('/home/USER/game', log)
        self.assertIn('seconds=1.000', log)
        self.assertIn('presented=1', log)
        self.assertIn('generated=<removed>', info)
        self.assertIn('i7-6850K', info)
        self.assertIn('RTX 3080 Ti', info)
        self.assertIn('commit=3872cb4', info)
        self.assertEqual((copied, left_out), (3, 1))
        self.assertGreater(replaced, 6)

    def test_screenshots_stay_only_when_asked_for(self):
        destination, (_, _, left_out) = self.run_it(keep_screenshots=True)
        self.assertTrue((destination / 'shot.bmp').exists())
        self.assertEqual(left_out, 0)
        destination, _ = self.run_it()
        self.assertFalse((destination / 'shot.bmp').exists())

    def test_other_files_are_copied_untouched(self):
        destination, _ = self.run_it()
        self.assertEqual((destination / 'tool.exe').read_bytes(), b'Alice stays in a binary')


if __name__ == '__main__':
    unittest.main()

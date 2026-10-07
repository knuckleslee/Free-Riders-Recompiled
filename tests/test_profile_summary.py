from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import profile_summary as profile

MAP = """ sfr_cpu_diagnostic

 Preferred load address is 0000000140000000

  Address         Publics by Value              Rva+Base               Lib:Object

 0001:00000000       sub_82439530               0000000140001000 f   ppc_recomp.12.obj
 0001:00001000       ?draw@NativeRenderer@sfr@@QEAAXAEBUNativeDraw@2@@Z 0000000140002000 f   native_renderer.cpp.obj
 0001:00002000       ?read_scalar@GuestMemory@sfr@@QEBA_K_K0@Z 0000000140003000 f i guest_memory.cpp.obj
 0001:00003000       memcpy                     0000000140004000 f   libvcruntime:memcpy.obj

 Static symbols

 0001:00004000       ?helper@@YAXXZ             0000000140005000 f   diagnostic_main.cpp.obj
"""


class ProfileSummaryTest(unittest.TestCase):
    def test_names_samples_by_function_and_category(self):
        with tempfile.TemporaryDirectory() as directory:
            (Path(directory) / 'sfr_cpu_diagnostic.map').write_text(MAP, encoding='utf-8')
            (Path(directory) / 'profile-1.log').write_text(
                'NATIVE_PRESENT frame=1\n'
                'HOST_PROFILE rva=0x1010 50\n'          # the game's own code
                'HOST_PROFILE rva=0x2400 20\n'          # the renderer
                'HOST_PROFILE rva=0x3008 10\n'          # a checked memory access
                'HOST_PROFILE rva=0x5010 5\n'           # a static host function
                'HOST_PROFILE rva=0x7ffe12340000 15\n'  # a system DLL
                , encoding='utf-8')
            symbols = profile.read_map(Path(directory) / 'sfr_cpu_diagnostic.map')
            self.assertEqual([rva for rva, _, _ in symbols], [0x1000, 0x2000, 0x3000, 0x4000, 0x5000])
            table = profile.summarise(profile.read_samples(directory), symbols)
            self.assertIn('| 遊戲生成碼 | 50.0% |', table)
            self.assertIn('| 畫圖 | 20.0% |', table)
            self.assertIn('| 客體記憶體存取 | 10.0% |', table)
            self.assertIn('| 執行檔以外（系統、驅動程式、等待中） | 15.0% |', table)
            self.assertIn('| `sfr::NativeRenderer::draw` | native_renderer.cpp.obj | 20.00% |', table)
            self.assertIn('| `helper` | diagnostic_main.cpp.obj | 5.00% |', table)

    def test_compares_settings_in_ms_a_frame(self):
        with tempfile.TemporaryDirectory() as directory:
            (Path(directory) / 'sfr_cpu_diagnostic.map').write_text(MAP, encoding='utf-8')
            def presents(ms):
                # Two presents before the sampled range, then 100 at ms apart.
                return ''.join(f'NATIVE_PRESENT source=0x0 frame={12198 + i} racing=1 seconds={(i * ms / 1000):.4f}\n'
                               for i in range(103))
            for run in (1, 2):
                # 20 ms a frame: half the game's code, half the renderer
                (Path(directory) / f'profile-{run}.log').write_text(
                    presents(20) + 'HOST_PROFILE rva=0x1010 50\nHOST_PROFILE rva=0x2400 50\n', encoding='utf-8')
                # 12 ms without drawing: the renderer is gone, the game's code alone
                (Path(directory) / f'profile-skip-draws-{run}.log').write_text(
                    presents(12) + 'HOST_PROFILE rva=0x1010 60\n', encoding='utf-8')
            (Path(directory) / 'baseline-1.log').write_text(presents(20), encoding='utf-8')
            self.assertEqual(profile.profiled_settings(directory), ['profile', 'profile-skip-draws'])
            self.assertAlmostEqual(profile.frame_ms(directory, 'profile'), 20.0, places=3)
            symbols = profile.read_map(Path(directory) / 'sfr_cpu_diagnostic.map')
            table = profile.compare(directory, ['profile', 'profile-skip-draws'], symbols)
            self.assertIn('| 類別 | profile ms | profile-skip-draws ms | profile − profile-skip-draws |', table)
            self.assertIn('| **整格** | 20.00 | 12.00 | +8.00 |', table)
            self.assertIn('| 畫圖 | 10.00 | 0.00 | +10.00 |', table)
            self.assertIn('| 遊戲生成碼 | 10.00 | 12.00 | -2.00 |', table)
            self.assertIn('| `sfr::NativeRenderer::draw` (native_renderer.cpp.obj) | 10.00 | 0.00 | +10.00 |', table)
            # Largest saving first
            self.assertLess(table.index('| 畫圖 |'), table.index('| 遊戲生成碼 |'))


if __name__ == '__main__':
    unittest.main()

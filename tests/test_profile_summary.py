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

    def test_names_the_dlls_outside_and_their_callers(self):
        with tempfile.TemporaryDirectory() as directory:
            (Path(directory) / 'sfr_cpu_diagnostic.map').write_text(MAP, encoding='utf-8')
            (Path(directory) / 'profile-1.log').write_text(
                'HOST_PROFILE rva=0x1010 60\n'
                'HOST_PROFILE rva=0x7ffe12340000 40\n'
                'HOST_PROFILE_OUTSIDE module=ntdll.dll caller=0x5010 caller2=0x1010 25\n'
                'HOST_PROFILE_OUTSIDE module=amdxc64.dll caller=0x2400 caller2=0x0 10\n'
                'HOST_PROFILE_OUTSIDE module=ntdll.dll caller=0x0 caller2=0x0 5\n'
                , encoding='utf-8')
            (Path(directory) / 'profile-2.log').write_text(
                'HOST_PROFILE_OUTSIDE module=ntdll.dll caller=0x5010 caller2=0x1010 0\n', encoding='utf-8')
            symbols = profile.read_map(Path(directory) / 'sfr_cpu_diagnostic.map')
            outside = profile.read_outside(directory)
            self.assertEqual(sum(outside.values()), 40)
            table = profile.summarise_outside(outside, symbols, sum(profile.read_samples(directory).values()))
            self.assertIn('| ntdll.dll | 系統核心呼叫：等待、鎖、記憶體配置 | 30.0% | 75.0% |', table)
            self.assertIn('| amdxc64.dll | AMD 顯示驅動程式 | 10.0% | 25.0% |', table)
            self.assertIn('| ntdll.dll | `helper` | `sub_82439530` | 25.00% |', table)
            self.assertIn('| amdxc64.dll | `sfr::NativeRenderer::draw` | `（堆疊上沒找到）` | 10.00% |', table)
            self.assertLess(table.index('| ntdll.dll | `helper`'), table.index('| amdxc64.dll | `sfr::'))


if __name__ == '__main__':
    unittest.main()

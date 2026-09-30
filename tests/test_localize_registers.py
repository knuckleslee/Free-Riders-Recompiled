import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
import localize_registers as localize


def function(name, body, address=0x82000000):
    return (f'__attribute__((alias("__imp__{name}"))) PPC_WEAK_FUNC({name});\n'
            f'PPC_FUNC_IMPL(__imp__{name}) {{\n\tPPC_FUNC_PROLOGUE();\n'
            f'\tsfr::enter_function(ctx, "{name}", 0x{address:08X});\n{body}}}\n\n')


def unit(*functions):
    return '#include "diagnostic_hooks.h"\n#include "ppc_recomp_shared.h"\n\n' + ''.join(functions)


def bodies(text):
    return {name: body for name, _, _, _, body in
            localize.parse_functions(text.split('\n', 1)[1], 'test.cpp')}


class LocalizeRegistersTest(unittest.TestCase):
    def run_one(self, *functions, hooks=()):
        out, report = localize.localize({'ppc_recomp.0.cpp': unit(*functions)}, set(hooks))
        return bodies(out['ppc_recomp.0.cpp']), report

    def test_callee_saved_and_condition_registers_become_locals(self):
        result, report = self.run_one(function('sub_82000000', (
            '\t// mflr r12\n\tctx.r12.u64 = ctx.lr;\n'
            '\t// bl 0x82001000\n\tctx.lr = 0x82000008;\n\t__savegprlr_29(ctx, base);\n'
            '\t// mr r31,r3\n\tctx.r31.u64 = ctx.r3.u64;\n'
            '\t// cmpwi cr6,r31,0\n\tctx.cr6.compare<int32_t>(ctx.r31.s32, 0, ctx.xer);\n'
            '\t// beq cr6,0x82000020\n\tif (ctx.cr6.eq) goto loc_82000020;\n'
            '\t// mr r3,r31\n\tctx.r3.u64 = ctx.r31.u64;\n'
            'loc_82000020:\n'
            '\t// b 0x82001100\n\t__restgprlr_29(ctx, base);\n\treturn;\n')))
        body = result['sub_82000000']
        for local in ('PPCRegister r12 = ctx.r12;', 'PPCRegister r31 = ctx.r31;', 'PPCCRRegister cr6 = ctx.cr6;'):
            self.assertIn(local, body)
        # A comparison reads xer's summary overflow bit: read before written, it stays.
        self.assertIn('cr6.compare<int32_t>(r31.s32, 0, ctx.xer);', body)
        code = body.split('sfr::enter_function')[1]  # after the locals take their first values
        self.assertNotIn('ctx.r31', code)
        self.assertNotIn('ctx.cr6', code)
        self.assertIn('ctx.r3.u64 = r31.u64;', body)
        # The save stays, with the link register it stores; the restore goes.
        self.assertIn('\tctx.r12 = r12;\n\t__savegprlr_29(ctx, base);\n', body)
        self.assertNotIn('__restgprlr_29(ctx, base);', body)
        self.assertEqual((report['helper_calls_removed'], report['helper_calls_kept']), (1, 1))
        self.assertLess(body.index('PPCRegister r31 = ctx.r31;'), body.index('sfr::enter_function'))
        self.assertNotIn('SfrRestore', body)  # it stores no callee-saved register to the context

    def test_saving_a_callee_saved_register_to_the_frame_is_not_an_input(self):
        result, _ = self.run_one(function('sub_82000000', (
            '\tPPC_STORE_U64(ctx.r1.u32 + -8, ctx.r31.u64);\n'
            '\tctx.r31.u64 = ctx.r4.u64;\n\tctx.r3.u64 = ctx.r31.u64;\n'
            '\tctx.r31.u64 = PPC_LOAD_U64(ctx.r1.u32 + -8);\n')))
        body = result['sub_82000000']
        self.assertIn('PPCRegister r31 = ctx.r31;', body)
        self.assertIn('PPC_STORE_U64(ctx.r1.u32 + -8, r31.u64);', body)

    def test_a_register_stored_as_data_is_an_input(self):
        # A continuation of a function XenonRecomp split: r31 is what the
        # first part left there, stored to the frame above the stack pointer.
        result, report = self.run_one(function('sub_82000000', (
            '\tPPC_STORE_U32(ctx.r1.u32 + 80, ctx.r31.u32);\n\tctx.r31.u64 = 0;\n')))
        self.assertNotIn('PPCRegister r31', result['sub_82000000'])
        self.assertEqual(report['inputs_outside_convention'], {'sub_82000000': ['r31']})

    def test_an_input_outside_the_convention_stays_in_the_context_and_callers_pass_it(self):
        probe = function('sub_82000100', '\tctx.r1.u64 = ctx.r1.u64 - ctx.r12.u64;\n\tctx.r12.u64 = 0;\n', 0x82000100)
        thunk = function('sub_82000200', '\tsub_82000100(ctx, base);\n\treturn;\n', 0x82000200)
        caller = function('sub_82000300', (
            '\tctx.r12.u64 = 4096;\n\tsub_82000200(ctx, base);\n\tctx.r3.u64 = ctx.r12.u64;\n'), 0x82000300)
        result, report = self.run_one(probe, thunk, caller)
        self.assertIn('ctx.r12.u64', result['sub_82000100'])
        self.assertNotIn('PPCRegister r12', result['sub_82000100'])
        body = result['sub_82000300']
        self.assertIn('\tctx.r12 = r12;\n\tsub_82000200(ctx, base);\n\tr12 = ctx.r12;\n', body)
        self.assertEqual(report['inputs_outside_convention'], {'sub_82000100': ['r12']})

    def test_host_calls_see_the_callers_callee_saved_registers(self):
        caller = function('sub_82000300', (
            '\tctx.r30.u64 = ctx.r4.u64;\n\tctx.r11.u64 = 1;\n'
            '\tsub_82000400(ctx, base);\n'
            '\tctx.ctr.u64 = ctx.r5.u64;\n'
            '\tPPC_CALL_INDIRECT_FUNC(ctx.ctr.u32);\n'
            '\tctx.r3.u64 = ctx.r30.u64 + ctx.r11.u64;\n'), 0x82000300)
        hooked = function('sub_82000400', '\tctx.r3.u64 = 0;\n', 0x82000400)
        result, _ = self.run_one(caller, hooked, hooks={'sub_82000400'})
        body = result['sub_82000300']
        self.assertIn('\tctx.r30 = r30;\n\tsub_82000400(ctx, base);\n', body)
        self.assertIn('\tctx.r30 = r30;\n\tPPC_CALL_INDIRECT_FUNC(ctr.u32);\n', body)
        self.assertNotIn('ctx.r11 = r11', body)  # host code reads no volatile scratch register

    def test_host_code_reading_a_register_further_up_gets_it(self):
        # The sign-in audit reads r30 and r31 of the function two calls up.
        middle = function('sub_82000200', (
            '\tctx.r3.u64 = ctx.r4.u64;\n\t__imp__XamUserGetSigninState(ctx, base);\n'), 0x82000200)
        outer = function('sub_82000100', (
            '\tctx.r30.u64 = ctx.r3.u64;\n\tctx.r31.u64 = ctx.r5.u64;\n\tsub_82000200(ctx, base);\n'
            '\tctx.r3.u64 = ctx.r30.u64 + ctx.r31.u64;\n'), 0x82000100)
        result, _ = self.run_one(outer, middle)
        self.assertIn('\tctx.r30 = r30;\n\tctx.r31 = r31;\n\tsub_82000200(ctx, base);\n', result['sub_82000100'])

    def test_a_function_handing_its_context_to_host_code_is_left_alone(self):
        source = function('sub_82000000', (
            '\tctx.r12.u64 = ctx.lr;\n\t__savegprlr_20(ctx, base);\n\tctx.r23.u64 = ctx.r3.u64;\n'
            '\tsfr::synchronize_resource_memory(ctx);\n\t__restgprlr_20(ctx, base);\n\treturn;\n'))
        result, report = self.run_one(source)
        self.assertEqual(result['sub_82000000'], localize.parse_functions(
            unit(source).split('\n', 1)[1], 'x').__next__()[4])
        self.assertEqual(report['opaque_functions'], 1)

    def test_helpers_given_one_register_do_not_hide_the_function(self):
        result, report = self.run_one(function('sub_82000000', (
            '\tctx.r31.u64 = ctx.r3.u64;\n\tsfr::addc(ctx.r4.u64, ctx.r31.u64, ctx.r5.u64, ctx.xer.ca);\n')))
        self.assertEqual(report['opaque_functions'], 0)
        self.assertIn('sfr::addc(ctx.r4.u64, r31.u64, ctx.r5.u64, ctx.xer.ca);', result['sub_82000000'])

    def test_a_function_host_code_inspects_is_left_alone(self):
        source = function('sub_824F19E8', '\tctx.r31.u64 = ctx.r3.u64;\n\tctx.r3.u64 = ctx.r31.u64;\n', 0x824F19E8)
        result, report = self.run_one(source)
        self.assertNotIn('PPCRegister', result['sub_824F19E8'])
        self.assertEqual(report['opaque_functions'], 1)

    def test_a_conditional_store_takes_cr0_back(self):
        result, _ = self.run_one(function('sub_82000000', (
            'loc_82000000:\n\tsfr::guest_checkpoint();\n'
            '\tctx.r10.u64 = sfr::load_reserved_word(ctx, uint32_t(ctx.r8.u32));\n'
            '\tctx.r10.s64 = ctx.r10.s64 + 1;\n'
            '\tsfr::store_conditional_word(ctx, uint32_t(ctx.r8.u32), ctx.r10.u32);\n'
            '\tif (!ctx.cr0.eq) goto loc_82000000;\n')))
        body = result['sub_82000000']
        self.assertIn('ctx.r10.u32);\n\tcr0 = ctx.cr0;\n\tif (!cr0.eq) goto loc_82000000;', body)
        self.assertNotIn('ctx.r30 =', body)

    def test_a_helper_moving_a_kept_register_is_still_called(self):
        # r31 is read before it is written (an input): the helper that saves
        # and restores it stays.
        result, report = self.run_one(function('sub_82000000', (
            '\t__savegprlr_30(ctx, base);\n\tctx.r3.u64 = ctx.r31.u64;\n'
            '\tctx.r30.u64 = ctx.r3.u64;\n\t__restgprlr_30(ctx, base);\n\treturn;\n')))
        body = result['sub_82000000']
        self.assertIn('__savegprlr_30(ctx, base);', body)
        self.assertEqual(report['helper_calls_kept'], 2)

    def test_a_restore_an_audit_watches_is_still_called(self):
        result, report = self.run_one(function('sub_82000000', (
            '\tctx.r12.u64 = ctx.lr;\n\t__savegprlr_27(ctx, base);\n\tctx.r31.u64 = ctx.r3.u64;\n'
            '\t__restgprlr_27(ctx, base);\n\treturn;\n')))
        self.assertIn('__restgprlr_27(ctx, base);', result['sub_82000000'])
        self.assertEqual(report['helper_calls_removed'], 0)

    def test_vector_loads_write_their_register(self):
        result, _ = self.run_one(function('sub_82000000', (
            '\tsfr::load_vector_memory(uint32_t(ctx.r3.u32), ctx.v64.u8);\n'
            '\tsfr::store_vector_memory(uint32_t(ctx.r4.u32), ctx.v64.u8);\n')))
        body = result['sub_82000000']
        self.assertIn('PPCVRegister v64 = ctx.v64;', body)
        self.assertIn('sfr::load_vector_memory(uint32_t(ctx.r3.u32), v64.u8);', body)

    def test_the_command_writes_a_localized_copy(self):
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            generated, output = directory / 'diagnostic', directory / 'diagnostic-local'
            generated.mkdir()
            (generated / 'report.json').write_text('{}\n')
            (generated / 'ppc_func_mapping.cpp').write_text('// mapping\n')
            (generated / 'ppc_recomp.0.cpp').write_bytes(unit(function('sub_82000000', (
                '\tctx.r31.u64 = ctx.r3.u64;\r\n\tctx.r3.u64 = ctx.r31.u64;\r\n'))).encode())
            sources = directory / 'src'
            sources.mkdir()
            (sources / 'hooks.cpp').write_text('SFR_HOOK(sub_82000100) {}\n')
            result = subprocess.run([sys.executable, str(ROOT / 'scripts/localize_registers.py'), str(generated),
                                     str(output), '--sources', str(sources)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertTrue((output / 'report.json').exists() and (output / 'ppc_func_mapping.cpp').exists())
            text = (output / 'ppc_recomp.0.cpp').read_bytes().decode()
            self.assertIn('PPCRegister r31 = ctx.r31;', text)
            self.assertIn('\tr31.u64 = ctx.r3.u64;\r\n', text)  # newlines kept as they were
            report = json.loads((output / 'localize_report.json').read_text())
            self.assertEqual((report['functions'], report['hooks']), (1, 1))
            again = subprocess.run([sys.executable, str(ROOT / 'scripts/localize_registers.py'), str(generated),
                                    str(output)], capture_output=True, text=True)
            self.assertNotEqual(again.returncode, 0, 'an existing output is never overwritten')

    @unittest.skipUnless(shutil.which('clang++'), 'needs clang++')
    def test_the_localized_code_compiles(self):
        source = unit(
            function('sub_82000100', '\tctx.r1.u64 = ctx.r1.u64 - ctx.r12.u64;\n', 0x82000100),
            function('sub_82000000', (
                '\tctx.r12.u64 = ctx.lr;\n\t__savegprlr_29(ctx, base);\n'
                '\tctx.r31.u64 = ctx.r3.u64;\n\tctx.f31.f64 = double(ctx.f1.f64);\n'
                '\tsfr::load_vector_memory(uint32_t(ctx.r3.u32), ctx.v64.u8);\n'
                '\tctx.cr6.compare<int32_t>(ctx.r31.s32, 0, ctx.xer);\n'
                '\tctx.ctr.u64 = ctx.r31.u64;\n'
                '\tif (ctx.cr6.eq) {\n\t\tsub_82000100(ctx, base);\n\t\treturn;\n\t}\n'
                '\tPPC_CALL_INDIRECT_FUNC(ctx.ctr.u32);\n'
                '\tctx.r3.u64 = ctx.r31.u64;\n\t__restgprlr_29(ctx, base);\n\treturn;\n')))
        out, _ = localize.localize({'ppc_recomp.0.cpp': source})
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            (directory / 'ppc_config.h').write_text(
                '#pragma once\n#define PPC_CONFIG_H_INCLUDED\n#define PPC_IMAGE_BASE 0x82000000ull\n'
                '#define PPC_IMAGE_SIZE 0x1000000ull\n#define PPC_CODE_BASE 0x82000000ull\n'
                '#define PPC_CODE_SIZE 0x1000000ull\n')
            (directory / 'diagnostic_hooks.h').write_text(
                '#pragma once\n#include "ppc_config.h"\n#include "ppc_context.h"\n'
                'namespace sfr { void enter_function(PPCContext&, const char*, uint32_t);\n'
                'void load_vector_memory(uint32_t, uint8_t (&)[16]); }\n'
                'PPC_EXTERN_FUNC(__savegprlr_29); PPC_EXTERN_FUNC(__restgprlr_29);\n')
            (directory / 'ppc_recomp_shared.h').write_text(
                '#pragma once\nPPC_EXTERN_FUNC(sub_82000000); PPC_EXTERN_FUNC(sub_82000100);\n')
            (directory / 'unit.cpp').write_text(out['ppc_recomp.0.cpp'])
            result = subprocess.run(['clang++', '-std=c++20', '-fsyntax-only', '-Wno-unused-variable',
                                     '-I', str(directory), '-I', str(ROOT / 'tools/XenonRecomp/XenonUtils'),
                                     '-I', str(ROOT / 'tools/XenonRecomp/thirdparty/simde'),
                                     str(directory / 'unit.cpp')], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr[-3000:])


    @unittest.skipUnless(shutil.which('clang++'), 'needs clang++')
    def test_callers_find_their_registers_as_they_left_them(self):
        # O keeps its registers in the context (it hands the context to host
        # code); F stores its own r31 there for a hook to read. O must still
        # find its r31, and a split part reading r30 its caller left must get it.
        source = unit(
            function('sub_82000100', (
                '\tctx.r31.u64 = 7;\n\tctx.r30.u64 = 5;\n\tsfr::synchronize_resource_memory(ctx);\n'
                '\tsub_82000200(ctx, base);\n\tctx.r3.u64 = ctx.r31.u64;\n'), 0x82000100),
            function('sub_82000200', (
                '\tctx.r12.u64 = ctx.lr;\n\t__savegprlr_29(ctx, base);\n'
                '\tctx.r31.u64 = 99;\n\tctx.r29.u64 = 1;\n\tsub_82000400(ctx, base);\n'
                '\tctx.r4.u64 = ctx.r31.u64 + ctx.r29.u64;\n\tctx.r30.u64 = 11;\n'
                '\tsub_82000300(ctx, base);\n\t__restgprlr_29(ctx, base);\n\treturn;\n'), 0x82000200),
            function('sub_82000300', (
                '\tPPC_STORE_U32(ctx.r1.u32 + 80, ctx.r30.u32);\n\tctx.r5.u64 = ctx.r30.u64;\n'
                '\tctx.r30.u64 = 0;\n'), 0x82000300),
            function('sub_82000400', '\tctx.r3.u64 = 0;\n', 0x82000400))
        out, report = localize.localize({'ppc_recomp.0.cpp': source}, {'sub_82000400'})
        self.assertEqual(report['opaque_functions'], 1)
        self.assertIn('SfrRestore', bodies(out['ppc_recomp.0.cpp'])['sub_82000200'])
        harness = (
            '#include "diagnostic_hooks.h"\n#include "ppc_recomp_shared.h"\n#include <cstdio>\n#include <vector>\n'
            'uint32_t seen;\nnamespace sfr {\nvoid enter_function(PPCContext&, const char*, uint32_t) {}\n'
            'void synchronize_resource_memory(PPCContext&) {}\n}\n'
            # The helpers as the game has them: through the frame, with the link register.
            'PPC_FUNC(__savegprlr_29) { PPC_STORE_U64(ctx.r1.u32 - 32, ctx.r29.u64); '
            'PPC_STORE_U64(ctx.r1.u32 - 24, ctx.r30.u64); PPC_STORE_U64(ctx.r1.u32 - 16, ctx.r31.u64); '
            'PPC_STORE_U32(ctx.r1.u32 - 8, ctx.r12.u32); }\n'
            'PPC_FUNC(__restgprlr_29) { ctx.r29.u64 = PPC_LOAD_U64(ctx.r1.u32 - 32); '
            'ctx.r30.u64 = PPC_LOAD_U64(ctx.r1.u32 - 24); ctx.r31.u64 = PPC_LOAD_U64(ctx.r1.u32 - 16); }\n'
            'PPC_FUNC(sub_82000400) { seen = ctx.r31.u32 * 100 + ctx.r29.u32; }\n'
            'int main() { std::vector<uint8_t> memory(0x10000); PPCContext ctx{}; ctx.r1.u64 = 0x8000;\n'
            '  ctx.r31.u64 = 3; sub_82000100(ctx, memory.data());\n'
            '  std::printf("%u %u %u %u %u\\n", ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, seen, ctx.r31.u32); }\n')
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            (directory / 'ppc_config.h').write_text(
                '#pragma once\n#define PPC_CONFIG_H_INCLUDED\n#define PPC_IMAGE_BASE 0x82000000ull\n'
                '#define PPC_IMAGE_SIZE 0x1000000ull\n#define PPC_CODE_BASE 0x82000000ull\n'
                '#define PPC_CODE_SIZE 0x1000000ull\n')
            (directory / 'diagnostic_hooks.h').write_text(
                '#pragma once\n#include "ppc_config.h"\n#include "ppc_context.h"\n'
                'namespace sfr { void enter_function(PPCContext&, const char*, uint32_t);\n'
                'void synchronize_resource_memory(PPCContext&); }\n'
                'PPC_EXTERN_FUNC(__savegprlr_29); PPC_EXTERN_FUNC(__restgprlr_29);\n')
            (directory / 'ppc_recomp_shared.h').write_text(
                '#pragma once\nPPC_EXTERN_FUNC(sub_82000100); PPC_EXTERN_FUNC(sub_82000200);\n'
                'PPC_EXTERN_FUNC(sub_82000300); PPC_EXTERN_FUNC(sub_82000400);\n')
            # The hook replaces the generated function, as the runtime's do.
            generated = out['ppc_recomp.0.cpp'].replace(
                '__attribute__((alias("__imp__sub_82000400"))) PPC_WEAK_FUNC(sub_82000400);\n', '')
            (directory / 'unit.cpp').write_text(generated)
            (directory / 'main.cpp').write_text(harness)
            binary = directory / 'run'
            result = subprocess.run(['clang++', '-std=c++20', '-O2', '-Wno-unused-variable',
                                     '-I', str(directory), '-I', str(ROOT / 'tools/XenonRecomp/XenonUtils'),
                                     '-I', str(ROOT / 'tools/XenonRecomp/thirdparty/simde'),
                                     str(directory / 'unit.cpp'), str(directory / 'main.cpp'), '-o', str(binary)],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr[-3000:])
            run = subprocess.run([str(binary)], capture_output=True, text=True)
            # O's r31 (7) survives F; F's r31 and r29 reach the hook and its own
            # sum; the split part reads the r30 F left; O's own r31 (7) is what it returns with.
            self.assertEqual(run.stdout.split(), ['7', '100', '11', '9901', '7'])

if __name__ == '__main__':
    unittest.main()

#!/usr/bin/env python3
"""Keep the recompiled game's PowerPC registers in local variables.

XenonRecomp can emit the condition, count, fixed-point exception and
reservation registers, and the registers no call passes or preserves for
its caller (r0, r2, r11, r12, f0, v32-v63) and the ones a callee saves
(r14-r31, f14-f31, v14-v31, v64-v127), as each function's own local
variables instead of PPCContext fields (cr_as_local, ctr_as_local,
xer_as_local, reserved_as_local, non_argument_as_local,
non_volatile_as_local: what Unleashed Recompiled builds with). A field is
memory the compiler must write back and read again around every store the
game makes (the build has no strict aliasing, so any store may change it);
a local lives in a host register. This build had them all off.

This does the same to the checked diagnostic sources generate_diagnostic.py
writes, so none of its rewrites or audits change: every "ctx.<register>" of
a function becomes a local of that function, and the __rest helpers that
only moved non-volatile registers from the stack back to the context are no
longer called. On top of what XenonRecomp does, it keeps these correct,
which its options take on trust:

- A function that reads a register before it writes it (not counting a
  store of a callee-saved register to its own stack frame) takes that
  register as an input outside the calling convention (a helper such as a
  stack probe). It keeps that register in the context, and its callers,
  directly or through functions that never touch the register, store their
  local there before the call and take it back after.
- Host code reads some callee-saved registers of the guest caller (hooks,
  imports and synchronize_resource_memory look at r20-r31 for context), and
  store_conditional_* sets cr0 in the context. Before a call to host code
  (a hook, an import, an sfr:: helper that takes the context, an indirect
  call) the caller's local r14-r31 are stored to the context; after a
  conditional store cr0 is taken back.
- Functions left as they are keep their registers in the context. A
  localized function that stores a callee-saved local there (for host code
  or a callee's input) puts back what it found on entry when it returns.
- Each local starts as the context has it, so a path the analysis missed
  (a jump over the first write) still reads the context's value, and the
  __save helpers are still called: the frame holds the caller's registers
  and the link register for a restore in a split-off part of the function,
  or for host code reading the stack.

    python scripts/localize_registers.py out/recomp/diagnostic out/recomp/diagnostic-local

writes the transformed copy (the input is left as it is) and a
localize_report.json beside it; build with
scripts/build_tools.ps1 -Diagnostic -DiagnosticDirectory out/recomp/diagnostic-local.
"""
import argparse
import json
import re
import shutil
import sys
import uuid
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from generate_diagnostic import parse_functions  # noqa: E402

GPR = {0, 2, 11, 12} | set(range(14, 32))
FPR = {0} | set(range(14, 32))
VR = set(range(14, 128))
NONVOLATILE_GPR = {f'r{i}' for i in range(14, 32)}
# The condition, count, fixed-point exception and reservation registers.
# XenonRecomp's options trust that no call passes them; here they are
# analysed like the others, since the second part of a function XenonRecomp
# split can test the comparison its first part made.
SPECIAL = {f'cr{i}' for i in range(8)} | {'ctr', 'xer', 'reserved'}
LOCALIZABLE = ({f'r{i}' for i in GPR} | {f'f{i}' for i in FPR} | {f'v{i}' for i in VR} | SPECIAL)
TYPES = {'r': 'PPCRegister', 'f': 'PPCRegister', 'v': 'PPCVRegister'}
SPECIAL_TYPES = {'ctr': 'PPCRegister', 'reserved': 'PPCRegister', 'xer': 'PPCXERRegister'}

REFERENCE = re.compile(r'\bctx\.(r[0-9]+|f[0-9]+|v[0-9]+|cr[0-7]|ctr|xer|reserved)\b')
PROLOGUE = '\tPPC_FUNC_PROLOGUE();'
CALL = re.compile(r'^(\t+)([A-Za-z_][A-Za-z0-9_]*)\(ctx, base\);(\r?)$', re.MULTILINE)
INDIRECT = re.compile(r'^(\t+).*\bPPC_CALL_INDIRECT_FUNC\(', re.MULTILINE)
# A helper taking the whole context (not one of its registers, as sfr::addc does).
HOST_HELPER = re.compile(r'^(\t+).*\bsfr::([a-z_0-9]+)\(ctx[,)]', re.MULTILINE)
CONDITIONAL_STORE = re.compile(r'^\t+.*\bsfr::store_conditional_(?:word|doubleword)\(ctx\b.*$', re.MULTILINE)
SAVE_REST = re.compile(r'^__(save|rest)(gprlr|fpr|vmx)_(\d+)$')
# sfr:: helpers taking the context that read none of the guest caller's
# registers beyond r1, r13 and the link register (or only write cr0).
PURE_HELPERS = {'enter_function', 'unsupported_function', 'load_reserved_word', 'load_reserved_doubleword',
                'store_conditional_word', 'store_conditional_doubleword'}
# Functions host code inspects while they run, left as they are: the video
# global's import variable handler reads r31 and the link register the
# function's __savegprlr stored (diagnostic_main.cpp, 0x82000664).
OPAQUE_FUNCTIONS = {'sub_824F19E8'}
# A full write: the whole register assigned from an expression.
FULL_WRITE = {
    'r': re.compile(r'^\s*ctx\.(r[0-9]+)\.(?:u64|s64) = (.*)$'),
    'f': re.compile(r'^\s*ctx\.(f[0-9]+)\.(?:u64|f64) = (.*)$'),
    'v': re.compile(r'^\s*simde_mm_store_(?:si128|ps)\(\((?:simde__m128i|float)\*\)ctx\.(v[0-9]+)\.(?:u8|f32), (.*)$'),
    # lvx through the runtime's checked vector load (generate_diagnostic.py).
    'lvx': re.compile(r'^\s*sfr::load_vector_memory\(uint32_t\((.*)\), ctx\.(v[0-9]+)\.u8\);'),
    # A comparison sets all four bits of its field.
    'cr': re.compile(r'^\s*ctx\.(cr[0-7])\.(?:compare(?:<[^>]*>)?|setFromMask)\((.*)$'),
    'ctr': re.compile(r'^\s*ctx\.(ctr)\.u64 = (.*)$'),
    'reserved': re.compile(r'^\s*ctx\.(reserved)\.u(?:32|64) = (.*)$'),
}
# A callee-saved register stored below the stack pointer, where a prologue
# saves them before it moves the pointer: saving reads the caller's value
# only to restore it, so it does not make the register an input. A store at
# a positive offset is data (a fragment of a function XenonRecomp split may
# spill what the first part left in r31), so it counts as a read.
FRAME_SAVE = re.compile(r'^\s*PPC_STORE_U(?:32|64)\(ctx\.r1\.u32 \+ -[0-9]+, ctx\.((?:r|f)[0-9]+)\.u(?:32|64)\);')


def kind(register):
    return register.rstrip('0123456789') if not register.startswith('cr') else 'cr'


def callee_saved(register):
    number = int(register[1:]) if register[0] in 'rfv' and register[1:].isdigit() else -1
    return (register[0] in 'rf' and 14 <= number <= 31) or (register[0] == 'v' and (14 <= number <= 31 or number >= 64))


def helper_range(name):
    """The registers a __save/__rest helper moves, or None for another name."""
    match = SAVE_REST.match(name)
    if not match:
        return None
    _, family, first = match.groups()
    first = int(first)
    if family == 'gprlr':
        return {f'r{i}' for i in range(first, 32)}
    if family == 'fpr':
        return {f'f{i}' for i in range(first, 32)}
    return {f'v{i}' for i in range(first, 32 if first < 64 else 128)}


def analyse(body):
    """(registers the body names, registers it reads before writing them)."""
    referenced, written, inputs = set(), set(), set()
    for line in body.splitlines():
        if CONDITIONAL_STORE.match(line):
            # The runtime's conditional store sets all of cr0 (localize()
            # takes it back from the context after the call).
            referenced.add('cr0')
            if 'cr0' not in inputs:
                written.add('cr0')
        names = [name for name in REFERENCE.findall(line) if name in LOCALIZABLE]
        if not names:
            continue
        full = None
        for pattern in FULL_WRITE.values():
            match = pattern.match(line)
            if match:
                full = match
                break
        save = FRAME_SAVE.match(line)
        for name in dict.fromkeys(names):
            referenced.add(name)
            if name in written:
                continue
            if save and save.group(1) == name and callee_saved(name):
                continue
            target, source = (full.group(2), full.group(1)) if full and full.re is FULL_WRITE['lvx'] else \
                (full.groups() if full else (None, None))
            if full and target == name and not re.search(r'\bctx\.' + name + r'\b', source):
                written.add(name)
                continue
            inputs.add(name)
    return referenced, inputs


def localize(sources, hooks=frozenset()):
    """{filename: source} -> ({filename: transformed source}, report). hooks: the
    guest functions the runtime defines itself, whose calls are host calls."""
    functions = {}
    for filename, source in sources.items():
        text = source.split('\n', 1)[1] if source.startswith('#include "diagnostic_hooks.h"') else source
        for name, _, _, _, body in parse_functions(text, filename):
            functions[name] = body
    analysed = {name: analyse(body) for name, body in functions.items()}
    calls = {name: set(match[2] for match in CALL.finditer(body)) for name, body in functions.items()}
    # Inputs reach through callers that never name the register.
    inputs = {name: set(found[1]) for name, found in analysed.items()}
    changed = True
    while changed:
        changed = False
        for name, callees in calls.items():
            referenced = analysed[name][0]
            for callee in callees:
                target = callee
                if target not in inputs or helper_range(callee) is not None:
                    continue
                extra = {register for register in inputs[target] if register not in referenced} - inputs[name]
                if extra:
                    inputs[name] |= extra
                    changed = True
    every_input = set().union(*inputs.values()) if inputs else set()
    report = {'functions': len(functions), 'localized_functions': 0, 'localized_registers': 0,
              'kept_in_context': 0, 'helper_calls_removed': 0, 'helper_calls_kept': 0,
              'call_syncs': 0, 'host_syncs': 0, 'opaque_functions': 0, 'restoring_functions': 0, 'inputs_outside_convention': {}}

    def transform(name, body):
        referenced, _ = analysed[name]
        if helper_range(name) is not None:
            return body  # the helpers move context registers; they stay as they are
        # A function that hands its context to host code reading more than its
        # arguments (synchronize_resource_memory reads r20-r30 and two stack
        # slots, one of them where __savegprlr keeps the link register) is left
        # exactly as it is: its registers stay in the context and its helpers
        # still fill the frame.
        if name in OPAQUE_FUNCTIONS or any(match[2] not in PURE_HELPERS for match in HOST_HELPER.finditer(body)):
            report['opaque_functions'] += 1
            return body
        kept = referenced & inputs[name]
        local = referenced - kept
        if kept - {'xer'}:  # most comparisons read xer's summary overflow bit
            report['inputs_outside_convention'][name] = sorted(kept)
        report['kept_in_context'] += len(kept)
        if not local:
            return body
        report['localized_functions'] += 1
        report['localized_registers'] += len(local)
        out = REFERENCE.sub(lambda m: m[1] if m[1] in local else m[0], body)
        nonvolatile = sorted(local & NONVOLATILE_GPR, key=lambda r: int(r[1:]))
        # The callee-saved registers this function stores to the context for
        # code it calls: its caller finds them as they were on return.
        stored = set()

        def sync(indent, registers, newline):
            stored.update(register for register in registers if callee_saved(register))
            return ''.join(f'{indent}ctx.{r} = {r};{newline}\n' for r in registers)

        def back(indent, registers, newline):
            return ''.join(f'{indent}{r} = ctx.{r};{newline}\n' for r in registers)

        lines = out.split('\n')
        result = []
        for index, line in enumerate(lines):
            call = CALL.match(line)
            if call:
                indent, callee, newline = call.groups()
                moved = helper_range(callee)
                if moved is not None:
                    # The saves stay: they read the context's callee-saved
                    # registers, which are the caller's still, and fill the
                    # frame (the link register too) for whatever reads it: a
                    # restore in a part of the function XenonRecomp split off,
                    # or host code looking at the stack.
                    saving = callee.startswith('__save')
                    if not saving and not (moved & (kept | inputs[name])):
                        report['helper_calls_removed'] += 1
                        continue
                    report['helper_calls_kept'] += 1
                    # r12 (the link register, or where the vector and float
                    # registers go) and r11, whatever the helper's own analysis.
                    registers = sorted(({'r11', 'r12'} | inputs.get(callee, set())) & local)
                    result.append(sync(indent, registers, newline) + line)
                    continue
                target = callee
                if target in functions and callee not in hooks:
                    registers = sorted(inputs[target] & local)
                    if registers:
                        report['call_syncs'] += 1
                    result.append(sync(indent, registers, newline) + line)
                    if registers:
                        result.append(back(indent, registers, newline).rstrip('\n'))
                    continue
                # A hook or an import: host code may read the caller's r14-r31.
                registers = sorted(set(nonvolatile) | (inputs.get(target, set()) & local))
                if registers:
                    report['host_syncs'] += 1
                result.append(sync(indent, registers, newline) + line)
                volatile = sorted(inputs.get(target, set()) & local)
                if volatile:
                    result.append(back(indent, volatile, newline).rstrip('\n'))
                continue
            newline = '\r' if line.endswith('\r') else ''
            indirect = INDIRECT.match(line)
            helper = HOST_HELPER.match(line)
            if indirect or (helper and helper[2] not in PURE_HELPERS):
                indent = (indirect or helper)[1]
                registers = sorted(set(nonvolatile) | (every_input & local))
                if registers:
                    report['host_syncs'] += 1
                result.append(sync(indent, registers, newline) + line)
                taken = sorted(every_input & local)
                if taken:
                    result.append(back(indent, taken, newline).rstrip('\n'))
                continue
            result.append(line)
            if CONDITIONAL_STORE.match(line) and 'cr0' in local:
                indent = re.match(r'\t+', line)[0]
                result.append(f'{indent}cr0 = ctx.cr0;{newline}')
                if 'xer' in local:
                    result.append(f'{indent}cr0.so = xer.so;{newline}')
        out = '\n'.join(result)

        def type_of(register):
            family = kind(register)
            return 'PPCCRRegister' if family == 'cr' else SPECIAL_TYPES.get(register) or TYPES[family]

        # Each local starts as the context has it: a path the analysis does
        # not see (a jump over the first write) still reads what the context
        # held, and a frame save stores the caller's value. The compiler drops
        # the load wherever every path writes first.
        order = sorted(local, key=lambda r: (kind(r), int(r.lstrip('rfvc') or 0) if r[-1].isdigit() else 0, r))
        declarations = [f'\t{type_of(register)} {register} = ctx.{register};' for register in order]
        if stored:
            report['restoring_functions'] += 1
            saved = sorted(stored, key=lambda r: (kind(r), int(r[1:])))
            members = ' '.join(f'{type_of(register)} {register};' for register in saved)
            restores = ' '.join(f'c.{register} = {register};' for register in saved)
            values = ', '.join(f'ctx.{register}' for register in saved)
            declarations.append(f'\tstruct SfrRestore {{ PPCContext& c; {members} '
                                f'~SfrRestore() {{ {restores} }} }} sfr_restore{{ctx, {values}}};')
        newline = '\r' if PROLOGUE + '\r' in out else ''
        if out.count(PROLOGUE) != 1:
            raise ValueError(f'{name}: missing or duplicate prologue')
        return out.replace(PROLOGUE, PROLOGUE + ''.join(f'{newline}\n{line}' for line in declarations), 1)

    transformed = {}
    for filename, source in sources.items():
        header, text = ('', source)
        if source.startswith('#include "diagnostic_hooks.h"'):
            header, text = source.split('\n', 1)
            header += '\n'
        chunks, cursor = [header], 0
        for name, _, body_start, end, body in parse_functions(text, filename):
            chunks.append(text[cursor:body_start])
            chunks.append(transform(name, body) + '}')
            cursor = end
        chunks.append(text[cursor:])
        transformed[filename] = ''.join(chunks)
    return transformed, report


HOOK = re.compile(r'\bSFR_(?:CONCURRENT_)?HOOK\(([A-Za-z_][A-Za-z0-9_]*)\)|^PPC_FUNC\(([A-Za-z_][A-Za-z0-9_]*)\)', re.MULTILINE)


def hooked_functions(source_dir):
    """Guest functions the runtime defines itself (SFR_HOOK, PPC_FUNC)."""
    names = set()
    for path in Path(source_dir).glob('*.cpp'):
        for match in HOOK.finditer(path.read_text(encoding='utf-8', errors='replace')):
            names.add(match[1] or match[2])
    return names


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('input', type=Path, help='a generate_diagnostic.py output directory')
    parser.add_argument('output', type=Path, help='where the localized copy goes (must not exist)')
    parser.add_argument('--sources', type=Path, default=Path(__file__).resolve().parents[1] / 'src',
                        help='the runtime sources, for the hooks they define')
    args = parser.parse_args()
    if args.output.exists():
        raise SystemExit(f'{args.output} already exists')
    files = sorted(args.input.glob('ppc_recomp.*.cpp'))
    if not files:
        raise SystemExit(f'no ppc_recomp.*.cpp in {args.input}')
    hooks = hooked_functions(args.sources)
    sources = {}
    for path in files:
        # Newlines as they are (Path.read_text takes no newline before 3.13).
        with open(path, encoding='utf-8', newline='') as source:
            sources[path.name] = source.read()
    transformed, report = localize(sources, hooks)
    staging = args.output.parent / (args.output.name + '.staging-' + uuid.uuid4().hex)
    shutil.copytree(args.input, staging)
    try:
        for filename, text in transformed.items():
            (staging / filename).write_text(text, encoding='utf-8', newline='')
        report['hooks'] = len(hooks)
        (staging / 'localize_report.json').write_text(json.dumps(report, indent=1, sort_keys=True) + '\n')
        staging.rename(args.output)
    except BaseException:
        shutil.rmtree(staging, ignore_errors=True)
        raise
    print(json.dumps({key: value for key, value in report.items() if key != 'inputs_outside_convention'}, indent=1))
    print(f'functions keeping an input register in the context: {len(report["inputs_outside_convention"])}')


if __name__ == '__main__':
    main()

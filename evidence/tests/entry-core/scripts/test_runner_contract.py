#!/usr/bin/env python3
"""Strict three-model runner and its failure oracle (stdlib only, no assert).

Use --output-dir NEW_DIR [--cxx g++-14] [--ubsan] [--with-self-test].
--self-test runs only runner fixtures plus deliberate source mutations.
Every invocation owns a new output directory; binaries live in a fresh tempdir.
--pwsh PATH enables PowerShell wrapper fixtures on the current host, not Windows C++.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import tempfile

TARGETS = ('entry_ticket', 'entry_session', 'ds_allocator')
HERE = Path(__file__).resolve().parent
FLAGS = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-pedantic']


def sha(data):
    return hashlib.sha256(data).hexdigest()


def execute(command, cwd=None, timeout=60, env=None):
    try:
        p = subprocess.run(command, cwd=cwd, timeout=timeout, env=env,
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        return {'command': command, 'exit': p.returncode,
                'stdout': p.stdout.decode('utf-8', errors='replace'),
                'stderr': p.stderr.decode('utf-8', errors='replace'), 'timed_out': False}
    except subprocess.TimeoutExpired as e:
        return {'command': command, 'exit': 124, 'stdout': (e.stdout or b'').decode('utf-8', errors='replace'),
                'stderr': (e.stderr or b'').decode('utf-8', errors='replace') + '\nTIMEOUT\n', 'timed_out': True}
    except OSError as e:
        return {'command': command, 'exit': 127, 'stdout': '', 'stderr': str(e), 'timed_out': False}


def valid_result(run):
    lines = run['stdout'].splitlines()
    candidates = [line for line in lines if line.startswith('RESULT')]
    if run['exit'] != 0 or run['timed_out'] or len(candidates) != 1:
        return False
    match = re.fullmatch(r'RESULT pass=([1-9][0-9]*) fail=0', candidates[0])
    # 不能只伪造一行绿色summary；逐条断言与summary必须相符。
    return bool(match and int(match[1]) == sum(line.startswith('PASS  ') for line in lines)
                and not any(line.startswith('FAIL') for line in lines))


def process_text(label, record):
    if record is None:
        return f'# {label}: NOT RUN\n'
    return (f'# {label}_command: {json.dumps(record["command"], ensure_ascii=False)}\n'
            f'# {label}_exit: {record["exit"]}\n# {label}_timed_out: {record["timed_out"]}\n'
            f'--- {label} stdout ---\n{record["stdout"]}\n--- {label} stderr ---\n{record["stderr"]}\n')


def write_new(path, text):
    # x模式防止重试/并发过程覆盖已有raw。任何写入异常由main转非零。
    with path.open('x', encoding='utf-8', newline='\n') as stream:
        stream.write(text)
        stream.flush()
        os.fsync(stream.fileno())


def run_models(args, output, common):
    results = []
    flags = FLAGS + (['-fsanitize=undefined', '-fno-sanitize-recover=all', '-fno-omit-frame-pointer'] if args.ubsan else [])
    version = execute([args.cxx, '--version'], timeout=args.compile_timeout)
    for target in args.targets:
        source = args.source_dir / (target + '.cpp')
        record = {'target': target, 'source_sha256': None, 'compile': None, 'run': None, 'ok': False}
        text = common + f'# target: {target}\n' + process_text('compiler_version', version)
        if version['exit'] != 0:
            text += '# build skipped: compiler version lookup failed\n'
        else:
            try:
                content = source.read_bytes()
                record['source_sha256'] = sha(content)
                text += '# source_sha256: ' + record['source_sha256'] + '\n'
                with tempfile.TemporaryDirectory(prefix='entry-build-') as temporary:
                    build = Path(temporary)
                    (build / source.name).write_bytes(content)
                    exe = target + ('.exe' if os.name == 'nt' else '')
                    # 相对源码/二进制参数使raw不暴露工作区/用户名；cwd是新的临时目录。
                    command = [args.cxx] + flags + [source.name, '-o', exe]
                    record['compile'] = execute(command, cwd=build, timeout=args.compile_timeout)
                    if record['compile']['exit'] == 0 and (build / exe).is_file():
                        record['run'] = execute([str(Path('.') / exe) if os.name == 'nt' else './' + exe],
                                                cwd=build, timeout=args.timeout)
                        record['ok'] = valid_result(record['run'])
                    if source.read_bytes() != content:
                        record['ok'] = False
                        text += '# source changed during run; result rejected\n'
            except OSError as e:
                text += '# source/build IO error: ' + str(e) + '\n'
        text += process_text('compile', record['compile']) + process_text('run', record['run'])
        text += '# accepted_by_runner: ' + str(record['ok']) + '\n'
        raw = output / (target + '-linux.txt' if platform.system() == 'Linux' else target + '-host.txt')
        write_new(raw, text)
        record['raw'] = raw.name
        record['raw_sha256'] = sha(raw.read_bytes())
        results.append(record)
        print(f'{target}: {"PASS" if record["ok"] else "FAIL"}', flush=True)
    return results


def runner_contract(args, output, common):
    checks, details = [], []
    python = Path(sys.executable).name
    resolved_python = shutil.which(python)
    if not resolved_python or Path(resolved_python).resolve() != Path(sys.executable).resolve():
        python = sys.executable
    details.append(process_text('bash_version', execute(['bash', '--version'])))
    if args.pwsh:
        details.append(process_text('pwsh_version', execute([args.pwsh, '--version'])))

    def check(ok, name, observation=None):
        checks.append((bool(ok), name))
        details.append(('PASS  ' if ok else 'FAIL  ') + name)
        if observation is not None:
            details.append(process_text('fixture', observation))

    with tempfile.TemporaryDirectory(prefix='entry-runner-fixtures-') as temporary:
        root = Path(temporary)
        local_scripts = root / 'scripts'; local_scripts.mkdir()
        for filename in ('run_all.sh', 'build_run.ps1', 'test_runner_contract.py'):
            shutil.copyfile(HERE / filename, local_scripts / filename)
        fixture = root / 'src'; fixture.mkdir()
        fake = root / 'fixture-compiler'
        # 合成编译器用于精确注入退出/RESULT/超时；不是真实编译证据。
        fake.write_text('''#!/usr/bin/env python3
import os, pathlib, sys
if '--version' in sys.argv:
    print('synthetic fixture compiler 1'); raise SystemExit(0)
mode = pathlib.Path(sys.argv[-3]).read_text().strip()
if mode == 'compile_fail':
    print('SYNTHETIC COMPILE FAILURE', file=sys.stderr); raise SystemExit(9)
if mode == 'compile_timeout':
    import time; time.sleep(3)
programs = {
 'ok': 'print("PASS  fixture")\\nprint("RESULT pass=1 fail=0")',
 'run_fail': 'print("PASS  fixture")\\nprint("RESULT pass=1 fail=0")\\nraise SystemExit(7)',
 'missing': 'print("PASS  fixture")',
 'bad': 'print("PASS  fixture")\\nprint("RESULT pass=garbage fail=0")',
 'duplicate': 'print("PASS  fixture")\\nprint("RESULT pass=1 fail=0")\\nprint("RESULT pass=1 fail=0")',
 'false_green': 'print("FAIL  fixture")\\nprint("RESULT pass=1 fail=0")',
 'count_lie': 'print("PASS  fixture")\\nprint("RESULT pass=2 fail=0")',
 'run_timeout': 'import time; time.sleep(3)',
 'negative_result': 'print("PASS  fixture")\\nprint("RESULT pass=-1 fail=0")',
}
out = pathlib.Path(sys.argv[-1])
out.write_text('#!/usr/bin/env python3\\n' + programs.get(mode, programs['ok']) + '\\n')
out.chmod(0o755)
''', encoding='utf-8')
        fake.chmod(0o755)
        # stdout中不引用真实认证/网络/主机信息；只运行临时测试脚本。
        wrappers = [('bash', ['bash', str(local_scripts / 'run_all.sh')])]
        if args.pwsh:
            wrappers.append(('pwsh-linux' if platform.system() == 'Linux' else 'pwsh-host', [args.pwsh, '-NoProfile', '-File', str(local_scripts / 'build_run.ps1')]))
        else:
            details.append('NOT RUN: PowerShell wrapper (pass --pwsh to include it); Windows C++ unverified')
        for wrapper, prefix in wrappers:
            def invoke(mode, destination, compiler=str(fake), extra_env=None):
                (fixture / 'entry_ticket.cpp').write_text(mode, encoding='utf-8')
                if wrapper == 'bash':
                    command = prefix + ['--output-dir', str(destination), '--source-dir', str(fixture),
                        '--cxx', compiler, '--targets', 'entry_ticket', '--timeout', '1', '--compile-timeout', '1']
                else:
                    command = prefix + ['-OutputDir', str(destination), '-SourceDir', str(fixture),
                        '-Cxx', compiler, '-Targets', 'entry_ticket', '-Timeout', '1', '-CompileTimeout', '1']
                env = dict(os.environ)
                if extra_env: env.update(extra_env)
                return execute(command, timeout=30, env=env)
            destination = root / (wrapper + '-ok')
            success = invoke('ok', destination)
            check(success['exit'] == 0 and (destination / 'run-manifest.json').exists(), wrapper + ': successful fixture accepted', success)
            original = (destination / 'entry_ticket-linux.txt').read_bytes() if platform.system() == 'Linux' and (destination / 'entry_ticket-linux.txt').exists() else b''
            repeat = invoke('ok', destination)
            check(repeat['exit'] != 0 and (not original or (destination / 'entry_ticket-linux.txt').read_bytes() == original), wrapper + ': existing output refused without overwrite', repeat)
            empty = root / (wrapper + '-empty'); empty.mkdir()
            check(invoke('ok', empty)['exit'] != 0, wrapper + ': existing empty directory also refused')
            for mode in ('compile_fail', 'run_fail', 'missing', 'bad', 'duplicate', 'false_green', 'count_lie', 'negative_result', 'run_timeout', 'compile_timeout'):
                dest = root / (wrapper + '-' + mode)
                result = invoke(mode, dest)
                manifest_path = dest / 'run-manifest.json'
                manifest = json.loads(manifest_path.read_text()) if manifest_path.exists() else {}
                records = manifest.get('runs', [])
                diagnostics = bool(records and not records[0]['ok'])
                if mode.startswith('compile_'):
                    diagnostics = diagnostics and records[0]['compile'] is not None and records[0]['run'] is None
                check(result['exit'] != 0 and diagnostics, wrapper + ': ' + mode + ' rejected and diagnosed', result)
            missing = invoke('ok', root / (wrapper + '-tool-missing'), str(root / 'absent-compiler'))
            check(missing['exit'] != 0, wrapper + ': missing compiler rejected', missing)
            # 父路径是普通文件，是跨root权限环境都能复现的输出失败。
            blocked = root / (wrapper + '-not-directory'); blocked.write_text('fixture')
            cant_write = invoke('ok', blocked / 'run')
            check(cant_write['exit'] != 0, wrapper + ': output creation failure rejected', cant_write)
            # Python运行器也必须存在；wrapper不能把shell/native错误吞成0。
            if wrapper == 'bash':
                missing_python = invoke('ok', root / (wrapper + '-missing-python'), extra_env={'PYTHON': str(root / 'absent-python')})
            else:
                missing_python = execute(prefix + ['-OutputDir', str(root / (wrapper + '-missing-python')),
                                                   '-Python', str(root / 'absent-python')], timeout=30)
            check(missing_python['exit'] != 0, wrapper + ': missing Python rejected', missing_python)
        # 新build目录没有旧binary可用；额外放置旧build诱饵，验证编译失败不执行它。
        stale = fixture.parent / 'build'; stale.mkdir()
        marker = root / 'STALE_EXECUTED'
        binary = stale / 'entry_ticket'; binary.write_text('#!/bin/sh\ntouch "' + str(marker) + '"\n', encoding='utf-8'); binary.chmod(0o755)
        (fixture / 'entry_ticket.cpp').write_text('compile_fail')
        stale_result = execute([python, '-B', str(local_scripts / 'test_runner_contract.py'), '--output-dir', str(root / 'stale-run'),
            '--source-dir', str(fixture), '--targets', 'entry_ticket', '--cxx', str(fake)])
        check(stale_result['exit'] != 0 and not marker.exists(), 'failed compilation never executes a stale binary', stale_result)
        # 从完整main路径注入raw/manifest写失败，验证顶层非零及真实诊断。
        (fixture / 'entry_ticket.cpp').write_text('ok', encoding='utf-8')
        for fail_file in ('entry_ticket-linux.txt', 'run-manifest.json'):
            destination = root / ('write-fail-' + fail_file.replace('.', '-'))
            injection = f"""import runpy, sys
n = runpy.run_path({str(local_scripts / 'test_runner_contract.py')!r})
g = n['main'].__globals__
original = g['write_new']
def failing(path, text):
    if path.name == {fail_file!r}:
        raise OSError('SYNTHETIC WRITE FAILURE')
    return original(path, text)
g['write_new'] = failing
sys.argv = ['runner', '--output-dir', {str(destination)!r}, '--source-dir', {str(fixture)!r},
            '--targets', 'entry_ticket', '--cxx', {str(fake)!r}]
sys.exit(n['main']())
"""
            result = execute([python, '-B', '-c', injection], timeout=30)
            check(result['exit'] != 0 and 'runner error: SYNTHETIC WRITE FAILURE' in result['stderr'],
                  'top-level main rejects ' + fail_file + ' write failure', result)
        history = root / 'results'; history.mkdir()
        (history / 'entry_ticket.txt').write_text('PASS  old fixture\nRESULT pass=999 fail=0\n')
        (fixture / 'entry_ticket.cpp').write_text('missing', encoding='utf-8')
        old_result = execute([python, '-B', str(local_scripts / 'test_runner_contract.py'), '--output-dir', str(root / 'old-result-run'),
            '--source-dir', str(fixture), '--targets', 'entry_ticket', '--cxx', str(fake)], timeout=30)
        check(old_result['exit'] != 0 and '999' not in old_result['stdout'], 'historical PASS is never used to fill a missing current RESULT', old_result)
        # 真实编译刻意破坏后的三个模型；应编译成功、运行失败并有FAIL断言。
        mutations = {
            'entry_ticket': ('t.expireAt <= t.issuedAt ||', 'false ||'),
            'entry_session': ('if (s.acquiredNew && s.handle)', 'if (s.handle)'),
            'ds_allocator': ('old->second = next; ++lastEpoch; *out = next;', '++servers.at(next.handle.ds).load; old->second = next; ++lastEpoch; *out = next;'),
        }
        for target, (needle, replacement) in mutations.items():
            original_source = (args.source_dir / (target + '.cpp')).read_text(encoding='utf-8')
            if original_source.count(needle) != 1:
                check(False, target + ': mutation anchor must occur exactly once'); continue
            build = root / ('mutation-' + target); build.mkdir()
            source = build / (target + '.cpp'); source.write_text(original_source.replace(needle, replacement), encoding='utf-8')
            flags = FLAGS + (['-fsanitize=undefined', '-fno-sanitize-recover=all'] if args.ubsan else [])
            compile_result = execute([args.cxx] + flags + [source.name, '-o', target], cwd=build, timeout=args.compile_timeout)
            details.append('# mutation_target: ' + target + '\n# original_source_sha256: ' + sha(original_source.encode('utf-8')) + '\n# mutated_source_sha256: ' + sha(source.read_bytes()))
            details.append(process_text('mutation_compile', compile_result))
            run = execute(['./' + target], cwd=build, timeout=args.timeout) if compile_result['exit'] == 0 else None
            check(compile_result['exit'] == 0 and run is not None and run['exit'] != 0 and
                  any(line.startswith('FAIL  ') for line in run['stdout'].splitlines()),
                  target + ': deliberate semantic defect is caught by the test oracle', run or compile_result)
    good = sum(ok for ok, _ in checks); bad = len(checks) - good
    raw = output / ('runner-contract-linux.txt' if platform.system() == 'Linux' else 'runner-contract-host.txt')
    text = common + '# fixtures are synthetic; mutations use the actual compiler\n' + '\n'.join(details) + f'\nRESULT pass={good} fail={bad}\n'
    write_new(raw, text)
    print(f'runner-contract: {"PASS" if bad == 0 else "FAIL"} ({good}/{len(checks)})', flush=True)
    return {'target': 'runner-contract', 'ok': bad == 0, 'pass': good, 'fail': bad, 'raw': raw.name, 'raw_sha256': sha(raw.read_bytes())}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir', required=True, type=Path)
    parser.add_argument('--source-dir', type=Path, default=HERE.parent / 'src')
    parser.add_argument('--cxx', default=os.environ.get('CXX', 'g++'))
    parser.add_argument('--targets', nargs='+', choices=TARGETS, default=list(TARGETS))
    parser.add_argument('--timeout', type=float, default=15)
    parser.add_argument('--compile-timeout', type=float, default=60)
    parser.add_argument('--ubsan', action='store_true')
    group = parser.add_mutually_exclusive_group()
    group.add_argument('--self-test', action='store_true')
    group.add_argument('--with-self-test', action='store_true')
    parser.add_argument('--pwsh', default='')
    args = parser.parse_args()
    if any(not math.isfinite(n) or n <= 0 for n in (args.timeout, args.compile_timeout)) or len(set(args.targets)) != len(args.targets):
        parser.error('timeouts must be finite positive values and targets must be unique')
    args.source_dir = args.source_dir.resolve()
    # CXX有路径时先解析，避免临时cwd改变相对路径语义。普通命令从PATH找。
    if os.path.dirname(args.cxx): args.cxx = str(Path(args.cxx).resolve())
    if args.pwsh and os.path.dirname(args.pwsh): args.pwsh = str(Path(args.pwsh).resolve())
    try:
        args.output_dir.mkdir(parents=True, exist_ok=False)
        stamp = datetime.now(timezone.utc).isoformat()
        host = {'system': platform.system(), 'release': platform.release(), 'machine': platform.machine(), 'libc': platform.libc_ver()}
        common = '# generated_utc: ' + stamp + '\n# host: ' + json.dumps(host) + '\n# python: ' + sys.version.replace('\n', ' ') + '\n# build_cwd: fresh temporary directory\n'
        manifest = {'schema': 1, 'generated_utc': stamp, 'host': host, 'scope': list(args.targets),
                    'ubsan': args.ubsan, 'jip': 'not run; known zero-delta snapshot defect remains',
                    'windows_cpp': 'not verified by Linux/pwsh wrapper fixtures',
                    'runner_sha256': {f: sha((HERE / f).read_bytes()) for f in ('run_all.sh', 'build_run.ps1', 'test_runner_contract.py')},
                    'runs': []}
        if not args.self_test:
            manifest['runs'] = run_models(args, args.output_dir, common)
        if args.self_test or args.with_self_test:
            manifest['runner_contract'] = runner_contract(args, args.output_dir, common)
        manifest['ok'] = all(run['ok'] for run in manifest['runs']) and manifest.get('runner_contract', {'ok': True})['ok']
        # 历史证据仅记指纹，不读取PASS汇总，不重写。
        historical = HERE.parent / 'results'
        manifest['historical_raw_sha256'] = {target + '.txt': sha((historical / (target + '.txt')).read_bytes())
                                            for target in (*TARGETS, 'jip_resync') if (historical / (target + '.txt')).is_file()}
        write_new(args.output_dir / 'run-manifest.json', json.dumps(manifest, indent=2, ensure_ascii=False) + '\n')
        return 0 if manifest['ok'] else 1
    except (OSError, ValueError) as error:
        print('runner error: ' + str(error), file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())

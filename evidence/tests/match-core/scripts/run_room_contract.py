#!/usr/bin/env python3
"""Bounded room-only contracts and real negative controls; never run benchmarks.
Public read-only dependency: ../gameplay-core/scripts/run_inventory_contract.py
provides capture/path helpers only, never its Driver, tests or legacy entry point.
One commands.jsonl per run is the authoritative command/raw stream. Summaries
contain references, not copies. Use a fresh output directory outside both roots.
"""
from __future__ import annotations
import argparse
import base64
from datetime import datetime, timezone
import importlib.util
import json
import math
import os
from pathlib import Path
import re
import shutil
import sys

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[4]
HERE = Path('evidence/tests/match-core')
HELPER_REL = Path('evidence/tests/gameplay-core/scripts/run_inventory_contract.py')
HELPER = ROOT / HELPER_REL
spec = importlib.util.spec_from_file_location('room_readonly_capture', HELPER)
if spec is None or spec.loader is None:
    raise RuntimeError('cannot load required read-only Inventory capture helper')
helpers = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helpers)
sha, write_new, fresh_output = helpers.sha, helpers.write_new, helpers.fresh_output
run, okay = helpers.run, helpers.okay
SOURCES = [HERE/'src/room_fsm.cpp', HERE/'tests/room_contract.cpp',
           HERE/'scripts/run_room_contract.py', HELPER_REL]
MODES = {'o0-ndebug': ['-O0', '-DNDEBUG'], 'o2': ['-O2'],
         'ubsan': ['-O1', '-g', '-fsanitize=undefined', '-fno-sanitize-recover=undefined']}
FLAGS = ['-std=c++17', '-Wall', '-Wextra', '-Werror', '-pedantic']
# Versioned exact manifest, never inferred from a tested executable's output.
CASES = dict(happy=20, capacity_retry=15, capacity_release_retry=25, two_room_drop=28, decline_timeout=24,
             waiting_drop=19, cross_exit=28, deadlines=18, inprogress=18,
             duplicate_create=75, backfill=18, predicate_synthetic_guards=17)


def utc():
    return datetime.now(timezone.utc).isoformat()


def semantics(record, kind='contract', target=None):
    errors = []
    lines = record['stdout'].splitlines()
    mutant = kind == 'mutant'
    if kind == 'retained':
        statuses, results = {}, []
        for line in lines:
            if line.startswith(('PASS', 'FAIL')):
                found = re.fullmatch(r'(PASS|FAIL)  (R\d+) .*', line)
                if not found:
                    errors.append('malformed retained check'); continue
                status, name = found.groups()
                if name in statuses: errors.append('duplicate retained check')
                statuses[name] = status
            if line.startswith('RESULT'):
                found = re.fullmatch(r'RESULT pass=(\d+) fail=(\d+)', line)
                if not found: errors.append('malformed retained result')
                else: results.append(tuple(map(int, found.groups())))
        if statuses != {'R'+str(n): 'PASS' for n in range(1, 32)}:
            errors.append('exact passing R1-R31 required')
        if results != [(31, 0)]: errors.append('exact retained result required')
    else:
        checks, completions, results = {}, {}, []
        if lines.count('ROOM_CONTRACT version=1') != 1:
            errors.append('one supported header required')
        for line in lines:
            if line == 'ROOM_CONTRACT version=1': continue
            found = re.fullmatch(r'CHECK (\S+) ([1-9]\d*) (PASS|FAIL)', line)
            if found:
                name, n, status = found.groups(); key = (name, int(n))
                if key in checks: errors.append('duplicate check')
                checks[key] = status; continue
            found = re.fullmatch(r'CASE (\S+) checks=(\d+) fail=(\d+)', line)
            if found:
                name, count, failures = found.groups()
                if name in completions: errors.append('duplicate case')
                completions[name] = (int(count), int(failures)); continue
            found = re.fullmatch(r'RESULT version=1 cases=(\d+) checks=(\d+) fail=(\d+)', line)
            if found: results.append(tuple(map(int, found.groups()))); continue
            errors.append('unknown/malformed protocol: '+line[:80])
        expected = {(name, n) for name, count in CASES.items() for n in range(1, count+1)}
        if set(checks) != expected: errors.append('exact check manifest mismatch')
        if set(completions) != set(CASES): errors.append('exact case manifest mismatch')
        failed = sum(v == 'FAIL' for v in checks.values())
        for name, count in CASES.items():
            failures = sum(v == 'FAIL' for (case, _), v in checks.items() if case == name)
            if completions.get(name) != (count, failures): errors.append('case counters inconsistent: '+name)
        if results != [(len(CASES), sum(CASES.values()), failed)]: errors.append('exact single RESULT required')
        if mutant:
            if not failed or not any(name == target and status == 'FAIL' for (name, _), status in checks.items()):
                errors.append('intended semantic mutant case must fail')
        elif failed: errors.append('unexpected semantic failure')
    if record['exit_code'] != (1 if mutant else 0): errors.append('native exit mismatch')
    if record['timed_out'] or not record['capture_complete']: errors.append('timeout/incomplete capture')
    if record['stderr_bytes']: errors.append('unexpected stderr/sanitizer diagnostic')
    return errors


def mutations(model):
    def changed(old, new):
        if model.count(old) != 1: raise ValueError('mutation site not unique: '+old)
        return model.replace(old, new)
    predicate = changed('if (m.slot != Slot::kAccepted) return false;',
                        'if (m.slot == Slot::kWaitConfirm) return false;')
    ownership = changed('if (!r.ownsAllocation) return;', '/* release ownership guard removed */')
    invalidation = changed('if (r.state == RoomState::kClosed || r.memberSetInvalidated) return false;',
                           'if (r.state == RoomState::kClosed) return false;')
    cross = changed('if (m->slot != Slot::kWaitConfirm) return false;\n        m->slot = Slot::kDeclined;',
                    'if (m->slot == Slot::kDeclined || m->slot == Slot::kAccepted) return false;\n        m->slot = Slot::kDeclined;')
    cross = cross.replace('if (m->slot != Slot::kWaitConfirm && m->slot != Slot::kAccepted) return false;',
                          'if (m->slot == Slot::kDropped) return false;')
    duplicate = changed('if (rooms.find(id) != rooms.end()) throw std::invalid_argument("room ID already exists");',
                        '/* duplicate Create rejection removed: historical append/reset behavior */')
    start = changed('if (r.state != RoomState::kReady || r.memberSetInvalidated ||\n            !r.ownsAllocation || !r.AllAccepted()) return false;',
                    'if (r.state != RoomState::kReady) return false;')
    begin = model.index('    bool Drop('); end = model.index('    // 确认超时', begin)
    segment = model[begin:end]
    if segment.count('if (!m || !PreStart(r)) return false;') != 1: raise ValueError('Drop mutation site missing')
    inprogress = model[:begin]+segment.replace('if (!m || !PreStart(r)) return false;',
        'if (!m || (!PreStart(r) && r.state != RoomState::kInProgress)) return false;')+model[end:]
    timeout = changed('if (now <= r.confirmDeadline) return false;', 'if (now < r.confirmDeadline) return false;')
    confirm = changed('if (now > r.confirmDeadline) return false;', 'if (now >= r.confirmDeadline) return false;')
    return [('all-decided', predicate, 'predicate_synthetic_guards'),
            ('unguarded-release', ownership, 'two_room_drop'),
            ('reconfirm-invalidated', invalidation, 'decline_timeout'),
            ('cross-exit-requeue', cross, 'cross_exit'),
            ('duplicate-create', duplicate, 'duplicate_create'),
            ('inprogress-drop', inprogress, 'inprogress'),
            ('start-guard', start, 'predicate_synthetic_guards'),
            ('timeout-equality', timeout, 'deadlines'),
            ('confirm-equality', confirm, 'deadlines')]


class Journal:
    """One exclusive raw stream; each completed command is flushed immediately."""
    def __init__(self, path):
        self.path = path
        self.stream = path.open('x', encoding='utf-8', newline='\n')
        self.count = 0
    def add(self, record):
        record['record_number'] = self.count+1
        try:
            self.stream.write(json.dumps(record, ensure_ascii=True, separators=(',', ':'))+'\n')
            self.stream.flush(); os.fsync(self.stream.fileno())
        except OSError as error:
            print('RAW_LOG_FAILURE '+str(error), file=sys.stderr, flush=True)
            raise
        self.count += 1
    def close(self):
        self.stream.close()


class Driver:
    def __init__(self, args, output):
        self.args, self.output = args, output
        self.env = helpers.child_environment(args.cxx)
        self.source_identity = {str(p): sha(args.root/p) for p in SOURCES}
        self.executing = {str(p.resolve()): sha(p) for p in (Path(__file__), HELPER)}
        self.journal = Journal(output/'commands.jsonl')
        self.started = utc(); self.failed = False; self.outcomes = []
        (output/'build').mkdir()
    def unchanged(self):
        return (all(sha(self.args.root/p) == digest for p, digest in self.source_identity.items()) and
                all(sha(Path(p)) == digest for p, digest in self.executing.items()))
    def command(self, label, argv, kind=None, target=None, metadata=None):
        if not self.unchanged(): raise RuntimeError('source/runner/helper changed during run')
        start = utc()
        record = run(argv, self.args.root, self.env, self.args.timeout_seconds, self.output)
        record.update(label=label, started_utc=start, ended_utc=utc(), metadata=metadata or {})
        errors = semantics(record, kind, target) if kind else ([] if okay(record) else ['command failed/timeout/incomplete capture'])
        record.update(expected_exit_code=1 if kind == 'mutant' else 0,
                      semantic_kind=kind, target_case=target, valid=not errors, errors=errors)
        self.journal.add(record)
        self.outcomes.append(dict(record_number=record['record_number'], label=label, valid=not errors, errors=errors))
        if errors: self.failed = True
        return record
    def build(self, name, source, flags, kind='contract', target=None, included=None):
        binary = self.output/'build'/(name+'.exe')
        definitions = [f'-DROOM_SOURCE="{included}"'] if included else []
        identity = {'translation_unit': str(source), 'translation_unit_sha256': sha(source),
                    'model_sha256': sha(included or (source if kind == 'retained' else self.args.root/HERE/'src/room_fsm.cpp'))}
        built = self.command(name+'-compile', [self.args.cxx]+FLAGS+flags+definitions+[str(source), '-o', str(binary)], metadata=identity)
        if okay(built):
            self.command(name+'-run', [str(binary)], kind, target, dict(identity, binary_sha256=sha(binary)))
    def execute(self):
        try:
            version = self.command('compiler-version', [self.args.cxx, '--version'])
            target = self.command('compiler-target', [self.args.cxx, '-dumpmachine'])
            if okay(version) and okay(target):
                model = self.args.root/HERE/'src/room_fsm.cpp'
                test = self.args.root/HERE/'tests/room_contract.cpp'
                modes = MODES if self.args.mode == 'all' else {self.args.mode: MODES[self.args.mode]}
                for name, flags in modes.items(): self.build(name, test, flags)
                self.build('retained31', model, ['-O2'], 'retained')
                if self.args.negative_controls:
                    directory = self.output/'mutations'; directory.mkdir()
                    for name, content, case in mutations(model.read_text(encoding='utf-8')):
                        source = directory/(name+'.cpp'); write_new(source, content)
                        self.build(name, test, ['-O2'], 'mutant', case, source)
                        # These deliberately restore blind spots. The old 31
                        # assertions still pass; that does NOT bless the mutant.
                        self.build(name+'-old31', source, ['-O2'], 'retained')
            return self.finish()
        finally:
            self.journal.close()
    def finish(self):
        # No success verdict before the authoritative raw file closes successfully.
        self.journal.close()
        if not self.unchanged(): self.failed = True
        provenance = dict(version=1, started_utc=self.started, ended_utc=utc(), root=str(self.args.root),
            source_sha256=self.source_identity, actual_executing_code_sha256=self.executing,
            python=sys.version, python_optimize=sys.flags.optimize, platform=sys.platform,
            mode=self.args.mode, negative_controls=self.args.negative_controls, cases=CASES,
            benchmark_executed=False, capture_helper=str(HELPER),
            ignored_flag_variables_set={key: key in os.environ for key in ('CXXFLAGS', 'CPPFLAGS', 'LDFLAGS')},
            raw_stream='commands.jsonl', capture_boundary='Finite regular-file snapshots; escaped descendants not guaranteed captured/terminated',
            model_boundary='Single service, serial memory model. No production lease, persistence, DS, concurrency or MMR policy')
        try:
            write_new(self.output/'provenance.json', json.dumps(provenance, indent=2)+'\n')
            write_new(self.output/'summary.json', json.dumps(dict(failed=self.failed, commands=self.journal.count, outcomes=self.outcomes), indent=2)+'\n')
        except OSError as error:
            self.failed = True; print('FINAL_LOG_FAILURE '+str(error), file=sys.stderr, flush=True)
        print('DRIVER_RESULT '+('FAIL' if self.failed else 'PASS'), flush=True)
        return int(self.failed)


def fixture_report(mode):
    lines = ['ROOM_CONTRACT version=1']
    for name, count in CASES.items():
        lines += [f'CHECK {name} {n} PASS' for n in range(1, count+1)]
        lines += [f'CASE {name} checks={count} fail=0']
    result = f'RESULT version=1 cases={len(CASES)} checks={sum(CASES.values())} fail=0'
    lines.append(result)
    if mode == 'empty': return ''
    if mode == 'missing-check': lines.remove('CHECK happy 1 PASS')
    if mode == 'missing-case': lines.remove('CASE happy checks=20 fail=0')
    if mode == 'missing-result': lines.pop()
    if mode == 'duplicate-check': lines.insert(1, 'CHECK happy 1 PASS')
    if mode == 'duplicate-case': lines.insert(1, 'CASE happy checks=20 fail=0')
    if mode == 'duplicate-result': lines.append(result)
    if mode == 'wrong-version': lines[0] = 'ROOM_CONTRACT version=2'
    if mode == 'malformed': lines[-1] = 'RESULT version=1 cases=11 checks=bad fail=0'
    if mode == 'fail-zero':
        lines[1] = 'CHECK happy 1 FAIL'
        lines[21] = 'CASE happy checks=20 fail=1'
        lines[-1] = result[:-1]+'1'
    return '\n'.join(lines)+'\n'


FIXTURE = r'''#!PYTHON
import json, os, pathlib, sys
mode=os.environ['ROOM_FIXTURE_MODE']
events=pathlib.Path(os.environ['ROOM_FIXTURE_EVENTS'])
def event(kind,name):
    with events.open('a') as f: f.write(json.dumps([kind,name])+"\n")
if sys.argv[1:]==['--version']:
    print('synthetic room fixture compiler'); sys.exit(19 if mode=='compiler-fail' else 0)
if sys.argv[1:]==['-dumpmachine']: print('synthetic-target'); sys.exit(0)
out=pathlib.Path(sys.argv[sys.argv.index('-o')+1]); name=out.stem
event('compile',name)
code='#!'+sys.executable+'\nimport sys,os,time,json\n'
code+='with open('+repr(str(events))+',"a") as f: f.write(json.dumps(["run",'+repr(name)+'])+"\\n")\n'
if name=='retained31':
    code+='print("\\n".join("PASS  R%d retained synthetic case"%i for i in range(1,32)))\nprint("RESULT pass=31 fail=0")\n'
else:
    code+='os.write(1,'+repr(pathlib.Path(os.environ['ROOM_FIXTURE_REPORT']).read_bytes())+')\n'
    if mode=='binary-output': code+='os.write(1,b"\\xff|\\\\xff\\r\\n\\x00\\xfe");os.write(2,b"\\xfe|\\\\xfe\\r\\n\\x00\\xff")\n'
    if mode=='stderr': code+='print("unexpected diagnostic",file=sys.stderr)\n'
    if mode=='timeout': code+='time.sleep(10)\n'
    if mode=='runtime-fail': code+='sys.exit(31)\n'
out.write_text(code); out.chmod(0o700)
if name=='o2':
    if mode=='compile-fail': sys.exit(23) # runnable stale trap must NEVER run
    if mode=='log-directory': (out.parent.parent/'summary.json').mkdir()
    if mode=='log-symlink': (out.parent.parent/'summary.json').symlink_to(os.environ['ROOM_FIXTURE_SENTINEL'])
'''


def self_test(args, output):
    if os.name != 'posix': raise RuntimeError('self-test fixtures cover POSIX only; native Windows NOT_RUN')
    initial = {str(p): sha(args.root/p) for p in SOURCES}
    fixture = output/'fixture repository 中文 空格'
    for relative in SOURCES:
        path = fixture/relative; path.parent.mkdir(parents=True, exist_ok=True); shutil.copyfile(args.root/relative, path)
    compiler = output/'fixture compiler 中文 空格'
    write_new(compiler, FIXTURE.replace('PYTHON', sys.executable, 1)); compiler.chmod(0o700)
    sentinel = output/'untouched sentinel'; write_new(sentinel, 'ORIGINAL\n'); digest = sha(sentinel)
    driver = fixture/HERE/'scripts/run_room_contract.py'
    journal = Journal(output/'commands.jsonl')
    checks, failures, outcomes = 0, [], []
    def require(condition, label):
        nonlocal checks
        checks += 1
        if not condition: failures.append(label); print('SELFTEST_FAIL '+label, flush=True)
    def invoke(name, mode='ok', destination=None, cxx=None, extra=None, limited=False, close_failure=False, runner=driver):
        destination = destination or output/(name+' output 中文')
        report = output/(name+' protocol.txt'); write_new(report, fixture_report(mode))
        events = output/(name+' events.jsonl')
        env = dict(os.environ, ROOM_FIXTURE_MODE=mode, ROOM_FIXTURE_EVENTS=str(events),
                   ROOM_FIXTURE_REPORT=str(report), ROOM_FIXTURE_SENTINEL=str(sentinel),
                   CXXFLAGS='SYNTHETIC_IGNORED_FLAG_VALUE')
        argv = [sys.executable, '-B']+(['-O'] if sys.flags.optimize else [])+[str(runner), '--root', str(fixture),
                '--cxx', str(cxx or compiler), '--mode', 'o2', '--timeout-seconds', '0.3', '--output-dir', str(destination)]+(extra or [])
        if close_failure:
            launcher = output/(name+' close launcher.py')
            write_new(launcher, 'import importlib.util,os,sys\n'+
                'sys.dont_write_bytecode=True\n'+
                'spec=importlib.util.spec_from_file_location("room_close_fixture",'+repr(str(runner))+')\n'+
                'm=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)\n'+
                'original=m.Journal.close\n'+
                'def broken(self):\n    if not self.stream.closed: os.close(self.stream.fileno())\n    return original(self)\n'+
                'm.Journal.close=broken\nsys.exit(m.main())\n')
            argv[argv.index(str(runner))] = str(launcher)
        if limited:
            limiter = output/(name+' limiter.py')
            write_new(limiter, 'import os,resource,sys\nresource.setrlimit(resource.RLIMIT_FSIZE,(4096,4096))\nos.execv(sys.executable,[sys.executable]+sys.argv[1:])\n')
            argv = [sys.executable, str(limiter)]+argv[1:]
        start = utc(); record = run(argv, fixture, env, 15, output)
        record.update(label=name, fixture=True, started_utc=start, ended_utc=utc())
        journal.add(record)
        rows = [json.loads(line) for line in events.read_text().splitlines()] if events.exists() else []
        outcomes.append(dict(record_number=journal.count, label=name, exit_code=record['exit_code'], timed_out=record['timed_out']))
        return record, rows, destination
    modes = ['ok', 'compiler-fail', 'compile-fail', 'runtime-fail', 'timeout', 'log-directory', 'log-symlink',
             'empty', 'missing-check', 'missing-case', 'missing-result', 'duplicate-check', 'duplicate-case',
             'duplicate-result', 'wrong-version', 'malformed', 'fail-zero', 'stderr', 'binary-output']
    try:
        for mode in modes:
            record, events, destination = invoke('cli-'+mode, mode)
            require(okay(record) == (mode == 'ok'), mode+' native outcome')
            require(('DRIVER_RESULT PASS' in record['stdout']) == (mode == 'ok'), mode+' honest final verdict')
            require(sha(sentinel) == digest, mode+' original preserved')
            path = destination/'commands.jsonl'
            entries = [json.loads(line) for line in path.read_text().splitlines()] if path.is_file() else []
            by_label = {r['label']: r for r in entries}
            if mode == 'ok':
                provenance = json.loads((destination/'provenance.json').read_text())
                require(provenance['python_optimize'] == sys.flags.optimize, 'actual requested Python optimization')
                require(provenance['benchmark_executed'] is False and all('--benchmark' not in r['argv'] for r in entries), 'no hidden benchmark')
                require('SYNTHETIC_IGNORED_FLAG_VALUE' not in json.dumps(provenance), 'unused flag values not exposed')
                require(provenance['ignored_flag_variables_set']['CXXFLAGS'], 'ignored flag presence recorded')
            elif mode == 'compiler-fail': require(not events, 'compiler probe blocks compilation')
            elif mode == 'compile-fail':
                require(by_label['o2-compile']['exit_code'] == 23 and 'o2-run' not in by_label, 'compile failure blocks stale executable')
                require(['run', 'o2'] not in events, 'stale executable really not run')
            elif mode == 'runtime-fail': require(by_label['o2-run']['exit_code'] == 31, 'runtime native exit preserved')
            elif mode == 'timeout':
                require(by_label['o2-run']['timed_out'] and not by_label['o2-run']['capture_complete'], 'timeout remains incomplete')
                require(record['elapsed_seconds'] < 10, 'timeout bounded')
            elif mode in ('log-directory', 'log-symlink'):
                require('FINAL_LOG_FAILURE' in record['stderr'], mode+' real I/O failure')
            else:
                require(by_label['o2-run']['exit_code'] == 0 and not by_label['o2-run']['valid'], mode+' exit0 protocol rejected')
            if mode == 'binary-output':
                for channel, suffix in [('stdout', b'\xff|\\xff\r\n\x00\xfe'), ('stderr', b'\xfe|\\xfe\r\n\x00\xff')]:
                    r = by_label['o2-run']; raw = base64.b64decode(r[channel+'_base64'], validate=True)
                    require(raw.endswith(suffix) and len(raw) == r[channel+'_bytes'], channel+' original binary bytes')
                    require(helpers.hashlib.sha256(raw).hexdigest() == r[channel+'_sha256'], channel+' original hash')
        record, events, destination = invoke('launch-failure', cxx=output/'absent compiler')
        require(not okay(record) and not events, 'launch failure is failure without compiler output')
        rows = [json.loads(line) for line in (destination/'commands.jsonl').read_text().splitlines()]
        require(rows[0]['exit_code'] is None and rows[0]['capture_origin'] == 'synthetic_launch_diagnostic', 'launch diagnostic explicitly synthetic')
        record, _, _ = invoke('actual-log-write-failure', limited=True)
        require(not okay(record) and 'DRIVER_RESULT PASS' not in record['stdout'], 'real OS log write failure is nonzero')
        require('RAW_LOG_FAILURE' in record['stderr'], 'real EFBIG diagnostic')
        record, _, _ = invoke('actual-log-close-failure', close_failure=True)
        require(not okay(record) and 'DRIVER_RESULT PASS' not in record['stdout'], 'real raw close failure has no success verdict')
        require('Bad file descriptor' in record['stderr'], 'real close EBADF diagnostic')
        for kind in ('directory', 'file', 'symlink', 'dangling-symlink', 'missing-parent', 'repo-path', 'repo-alias'):
            destination = output/('refused-'+kind)
            if kind == 'directory': destination.mkdir(); write_new(destination/'keep', 'keep')
            elif kind == 'file': write_new(destination, 'keep')
            elif kind == 'symlink': destination.symlink_to(sentinel)
            elif kind == 'dangling-symlink': destination.symlink_to(output/'absent')
            elif kind == 'missing-parent': destination = destination/'missing'/'out'
            elif kind == 'repo-path': destination = fixture/'forbidden'
            elif kind == 'repo-alias':
                alias = output/'repo alias'; alias.symlink_to(fixture, target_is_directory=True); destination = alias/'forbidden'
            record, events, _ = invoke('protect-'+kind, destination=destination)
            require(not okay(record) and not events, kind+' refusal before compiler')
            require(sha(sentinel) == digest, kind+' sentinel unchanged')
            if kind == 'directory': require((destination/'keep').read_text() == 'keep', 'existing directory intact')
            if kind == 'file': require(destination.read_text() == 'keep', 'existing file intact')
        for name, extra in [('bad-arg', ['--invalid']), ('nan-timeout', ['--timeout-seconds', 'nan']),
                            ('infinite-timeout', ['--timeout-seconds', 'inf']), ('zero-timeout', ['--timeout-seconds', '0'])]:
            record, events, _ = invoke(name, extra=extra)
            require(not okay(record) and not events, name+' rejected before compile')
        record, _, destination = invoke('selected-root-identity', runner=Path(__file__).resolve())
        require(okay(record), 'separate selected root success')
        provenance = json.loads((destination/'provenance.json').read_text())
        require(provenance['actual_executing_code_sha256'] == {str(p.resolve()): sha(p) for p in (Path(__file__), HELPER)}, 'actual executing code distinct from selected root')
        require(not (fixture/HERE/'build').exists() and not (fixture/HERE/'results').exists(), 'fixture repo output free')
        require(all(sha(args.root/p) == digest for p, digest in initial.items()), 'source/helper unchanged')
        journal.close() # close failures must precede any self-test success report
        write_new(output/'self-test-summary.json', json.dumps(dict(version=1, checks=checks, failures=failures, outcomes=outcomes,
                  python_optimize=sys.flags.optimize, source_sha256=initial, raw_stream='commands.jsonl',
                  coverage='POSIX synthetic CLI fixtures; retained capture helper unchanged; no benchmark', ended_utc=utc()), indent=2)+'\n')
        print(f'SELFTEST_RESULT version=1 checks={checks} fail={len(failures)}', flush=True)
        return int(bool(failures))
    finally:
        journal.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=ROOT)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--cxx', default=os.environ.get('CXX', 'g++'))
    parser.add_argument('--mode', choices=['all', *MODES], default='all')
    parser.add_argument('--negative-controls', action='store_true')
    parser.add_argument('--self-test', action='store_true')
    parser.add_argument('--timeout-seconds', type=float, default=30)
    args = parser.parse_args(); args.root = args.root.resolve()
    if not math.isfinite(args.timeout_seconds) or not 0 < args.timeout_seconds <= 600:
        parser.error('--timeout-seconds must be finite and in (0,600]')
    try:
        output = fresh_output(args.output_dir, [ROOT, args.root])
        return self_test(args, output) if args.self_test else Driver(args, output).execute()
    except (OSError, ValueError, RuntimeError) as error:
        print('DRIVER_ERROR '+type(error).__name__+': '+str(error), file=sys.stderr, flush=True)
        return 1

if __name__ == '__main__': sys.exit(main())

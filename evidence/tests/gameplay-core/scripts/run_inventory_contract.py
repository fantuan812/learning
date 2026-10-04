#!/usr/bin/env python3
"""Bounded Inventory contracts and isolated five-target runner protection fixtures.

Default: O0+NDEBUG, O2, UBSan plus retained seven cases and three mutation controls.
--self-test: only fake compilers/programs; never execute other gameplay benchmarks.
--legacy-five-targets: explicit old five-program run; some programs benchmark.
Every mode requires a fresh output directory outside the real and selected repository.
"""
from __future__ import annotations
import argparse
import base64
import hashlib
import json
import os
import re
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import time
import tempfile

ROOT = Path(__file__).resolve().parents[4]
NAMES = ('inventory_txn', 'buff_conflict', 'skill_pipeline', 'attr_modifier_bench', 'entity_lifecycle')
HERE = Path('evidence/tests/gameplay-core')
MODES = {'o0-ndebug': ['-O0', '-DNDEBUG'], 'o2': ['-O2'],
         'ubsan': ['-O1', '-g', '-fsanitize=undefined', '-fno-sanitize-recover=undefined']}


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_new(path: Path, text: str) -> None:
    # Every log is exclusive, including symlink refusal. flush/fsync/close failures count.
    with path.open('x', encoding='utf-8', newline='\n') as stream:
        stream.write(text)
        stream.flush()
        os.fsync(stream.fileno())


def fresh_output(path: Path, roots: list[Path]) -> Path:
    resolved = path.absolute().resolve()
    for root in roots:
        if resolved == root.resolve() or root.resolve() in resolved.parents:
            raise ValueError(f'output must be outside repository (including aliases): {resolved}')
    # Do not resolve away a final existing/dangling symlink before refusing it.
    if os.path.lexists(path):
        raise FileExistsError(f'output already exists: {path}')
    resolved.mkdir(parents=False, exist_ok=False)
    return resolved


def child_environment(cxx: str) -> dict[str, str]:
    env = dict(os.environ)
    resolved = shutil.which(cxx)
    if resolved:
        env['PATH'] = str(Path(resolved).resolve().parent) + os.pathsep + env.get('PATH', '')
    return env


def run(argv: list[str], cwd: Path, env: dict[str, str], timeout: float, capture_dir: Path) -> dict:
    record = {'argv': argv, 'cwd': str(cwd), 'timeout_seconds': timeout,
              'capture_method': 'regular-file finite snapshots; never wait for pipe EOF',
              'capture_scope': 'launched process lifetime; escaped descendants are not guaranteed captured or terminated'}
    started = time.monotonic()
    stdout, stderr = b'', b''
    # A descendant may retain these descriptors. Reading a fixed regular-file
    # snapshot and closing our descriptors cannot wait for that descendant's EOF.
    with tempfile.TemporaryFile(dir=capture_dir) as out_file, tempfile.TemporaryFile(dir=capture_dir) as err_file:
        try:
            process = subprocess.Popen(argv, cwd=cwd, env=env, stdout=out_file,
                                       stderr=err_file, start_new_session=os.name == 'posix')
            record.update(timed_out=False, termination_attempted=False, root_reaped=False)
            try:
                process.wait(timeout=timeout)
                record['root_reaped'] = True
            except subprocess.TimeoutExpired:
                record.update(timed_out=True, termination_attempted=True)
                if os.name == 'posix':
                    record['termination_scope'] = 'original POSIX process group; descendants that escaped it are out of scope'
                    try:
                        os.killpg(process.pid, signal.SIGKILL)
                        record['tree_termination_result'] = 'SIGKILL sent to original group'
                    except ProcessLookupError:
                        record['tree_termination_result'] = 'original group already absent'
                    except OSError as error:
                        record['tree_termination_result'] = str(error)
                else:
                    # Native Windows remains untested in the Linux evidence.
                    record['termination_scope'] = 'best-effort taskkill /T plus direct root kill'
                    try:
                        killed = subprocess.run(['taskkill', '/PID', str(process.pid), '/T', '/F'],
                                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                                                timeout=5, check=False)
                        record['tree_termination_exit_code'] = killed.returncode
                    except (OSError, subprocess.TimeoutExpired) as error:
                        record['tree_termination_error'] = str(error)
                if process.poll() is None:
                    try: process.kill()
                    except OSError as error: record['root_kill_error'] = str(error)
                try:
                    process.wait(timeout=1)
                    record['root_reaped'] = True
                except subprocess.TimeoutExpired:
                    record['cleanup_error'] = 'root did not exit within the bounded one-second cleanup wait'
            record['exit_code'] = process.poll()
            record['capture_origin'] = 'child_process_bytes'
            lengths = [os.fstat(stream.fileno()).st_size for stream in (out_file, err_file)]
            def snapshot(stream, length: int) -> bytes:
                if hasattr(os, 'pread'):
                    chunks=[]; offset=0
                    while offset < length:
                        data=os.pread(stream.fileno(), min(length-offset, 1048576), offset)
                        if not data: break
                        chunks.append(data); offset += len(data)
                    return b''.join(chunks)
                # Untested Windows fallback: regular-file read is finite, but a
                # concurrent inherited writer may share this seek offset.
                stream.seek(0)
                return stream.read(length)
            record['snapshot_read_method'] = 'pread, writer offset unchanged' if hasattr(os, 'pread') else 'seek/read, concurrent writer offset not guaranteed'
            record['snapshot_is_atomic_against_concurrent_writers'] = False
            stdout, stderr = snapshot(out_file, lengths[0]), snapshot(err_file, lengths[1])
            record['snapshot_sizes'] = dict(zip(('stdout', 'stderr'), lengths))
            record['stream_changed_during_snapshot'] = any(os.fstat(stream.fileno()).st_size != length
                                                          for stream, length in zip((out_file, err_file), lengths))
            record['capture_complete'] = (not record['timed_out'] and record['root_reaped'] and
                                          not record['stream_changed_during_snapshot'] and
                                          [len(stdout), len(stderr)] == lengths)
            record['capture_may_be_truncated'] = not record['capture_complete']
            if record['timed_out']:
                record['descendant_cleanup'] = 'not guaranteed for escaped descendants; do not infer all descendants are gone'
        except OSError as error:
            record.update(exit_code=None, timed_out=False, capture_origin='synthetic_launch_diagnostic',
                          capture_complete=False, capture_may_be_truncated=True)
            stdout, stderr = b'', f'{type(error).__name__}: {error}\n'.encode('utf-8')
    for name, raw in [('stdout', stdout), ('stderr', stderr)]:
        record[name] = raw.decode('utf-8', errors='backslashreplace')
        record[name+'_base64'] = base64.b64encode(raw).decode('ascii')
        record[name+'_bytes'] = len(raw)
        record[name+'_sha256'] = hashlib.sha256(raw).hexdigest()
    record['text_encoding'] = 'utf-8/backslashreplace DISPLAY ONLY; base64 is authoritative'
    record['elapsed_seconds'] = round(time.monotonic() - started, 6)
    return record


def okay(record: dict) -> bool:
    return record['exit_code'] == 0 and not record['timed_out'] and record['capture_complete']


def format_record(record: dict) -> str:
    return ('COMMAND_JSON ' + json.dumps({k: v for k, v in record.items() if k not in ('stdout', 'stderr')}, ensure_ascii=True) +
            '\n--- stdout ---\n' + record['stdout'] + '\n--- stderr ---\n' + record['stderr'] + '\n--- end ---\n')


class Driver:
    def __init__(self, args: argparse.Namespace, output: Path):
        self.args, self.output = args, output
        self.env = child_environment(args.cxx)
        self.records: list[dict] = []
        self.failed = False
        (output / 'build').mkdir()
        (output / 'logs').mkdir()
        self.sources = {}
        for relative in [HERE/'src/inventory_txn.cpp', HERE/'tests/inventory_contract.cpp',
                         HERE/'scripts/run_inventory_contract.py', HERE/'scripts/run_all.sh', HERE/'scripts/build_run.ps1']:
            path = args.root / relative
            if path.is_file(): self.sources[str(relative)] = sha(path)

    def command(self, label: str, argv: list[str], expected_failure: bool = False) -> dict:
        record = run(argv, self.args.root, self.env, self.args.timeout_seconds, self.output)
        record.update(label=label, expected_failure=expected_failure)
        self.records.append(record)
        matched = (record['exit_code'] == 1 and not record['timed_out'] and record['capture_complete']) if expected_failure else okay(record)
        if not matched: self.failed = True
        return record

    def semantics(self, record: dict, kind: str) -> None:
        text = record['stdout']
        errors = []
        if len(re.findall(r'^RESULT(?:\s.*)?$', text, re.MULTILINE)) != 1:
            errors.append('expected exactly one RESULT line, including malformed candidates')
        if kind == 'retained-seven':
            summaries = re.findall(r'^RESULT pass=(\d+) fail=(\d+)$', text, re.MULTILINE)
            if summaries != [('7', '0')]: errors.append('expected exactly one retained RESULT pass=7 fail=0')
            names = re.findall(r'^PASS  T([1-7]) ', text, re.MULTILINE)
            if sorted(names) != list('1234567'): errors.append('missing/duplicate retained seven PASS cases')
        else:
            summaries = re.findall(r'^RESULT checks=(\d+) fail=(\d+) \(explicit checks remain active with NDEBUG\)$', text, re.MULTILINE)
            if len(summaries) != 1 or int(summaries[0][0]) <= 0:
                errors.append('expected exactly one complete nonempty contract RESULT')
            elif (int(summaries[0][1]) > 0) != (kind == 'mutant'):
                errors.append('RESULT fail count contradicts expected outcome')
            if kind != 'mutant':
                groups = re.findall(r'^PASS_GROUP (\S+)$', text, re.MULTILINE)
                expected_groups = ['independent_stacking_remove_original_result_conflict',
                    'inputs_retry_retained_memory_wide_totals', 'no_throw_results_and_external_serialization_boundary']
                if sorted(groups) != sorted(expected_groups): errors.append('missing/duplicate semantic groups')
                discoveries = re.findall(r'^ALLOCATION_DISCOVERY scenario=(\S+) calls=(\d+) buckets_before=\d+ buckets_after=\d+$', text, re.MULTILINE)
                expected_scenarios = ['empty_small','empty_max_batch','topup_and_new_slots','map_growth',
                                      'capacity_rejection_plan','tight_prefilled_max_batch']
                if sorted(name for name,_ in discoveries) != sorted(expected_scenarios) or any(not 0 < int(n) < 128 for _,n in discoveries):
                    errors.append('missing/duplicate/bad six allocation discoveries')
        fail_lines = re.findall(r'^FAIL(?:\s.*)?$', text, re.MULTILINE)
        if bool(fail_lines) != (kind == 'mutant'): errors.append('FAIL markers contradict expected outcome')
        expected_exit = 1 if kind == 'mutant' else 0
        if record['exit_code'] != expected_exit or record['timed_out'] or not record['capture_complete']: errors.append('native exit/timeout/incomplete capture contradicts report')
        record['semantic_valid'] = not errors
        record['semantic_errors'] = errors
        if errors: self.failed = True

    def log(self, name: str, records: list[dict], extra: dict | None = None) -> bool:
        try:
            write_new(self.output/'logs'/f'{name}.txt', 'SOURCE_SHA256 '+json.dumps(self.sources, ensure_ascii=True)+'\n'+
                      'METADATA '+json.dumps(extra or {}, ensure_ascii=True)+'\n'+''.join(format_record(r) for r in records))
            print(f'wrote {self.output / "logs" / (name+".txt")}', flush=True)
            return True
        except OSError as error:
            self.failed = True
            print(f'LOG_FAILURE {name}: {error}', file=sys.stderr, flush=True)
            return False

    def compiler(self) -> bool:
        records = [self.command('compiler-version', [self.args.cxx, '--version']),
                   self.command('compiler-target', [self.args.cxx, '-dumpmachine'])]
        return self.log('compiler', records) and all(okay(r) for r in records)

    def finish(self) -> int:
        try:
            write_new(self.output/'commands.json', json.dumps(self.records, indent=2, ensure_ascii=True)+'\n')
            write_new(self.output/'provenance.json', json.dumps({'source_sha256': self.sources, 'root':str(self.args.root),
                      'python':sys.version, 'platform':sys.platform, 'mode': 'legacy-five-targets' if self.args.legacy_five_targets else 'focused',
                      'no_new_benchmark_in_focused_mode': True, 'failed':self.failed}, indent=2, ensure_ascii=True)+'\n')
        except OSError as error:
            self.failed = True
            print(f'FINAL_LOG_FAILURE: {error}', file=sys.stderr)
        print('DRIVER_RESULT '+('FAIL' if self.failed else 'PASS'), flush=True)
        return int(self.failed)

    def legacy(self) -> int:
        if not self.compiler(): return self.finish()
        for name in NAMES:
            source = self.args.root/HERE/'src'/f'{name}.cpp'
            try: self.sources[str(HERE/'src'/source.name)] = sha(source)
            except OSError as error:
                self.failed=True; self.log(name,[],{'source_error':str(error)}); continue
            binary = self.output/'build'/f'{name}.exe'
            compile_record = self.command(name+'-compile',[self.args.cxx,'-std=c++17','-O2','-o',str(binary),str(source)])
            records=[compile_record]
            if okay(compile_record): records.append(self.command(name+'-run',[str(binary)]))
            self.log(name,records,{'compile_succeeded':okay(compile_record),'run_attempted':len(records)==2})
        return self.finish()

    def focused(self) -> int:
        if not self.compiler(): return self.finish()
        source = self.args.root/HERE/'src/inventory_txn.cpp'
        test = self.args.root/HERE/'tests/inventory_contract.cpp'
        common = [self.args.cxx,'-std=c++17','-Wall','-Wextra','-Werror','-pedantic']
        for mode, flags in MODES.items():
            records=[]; obj=self.output/'build'/f'allocator-{mode}.o'; binary=self.output/'build'/f'contract-{mode}.exe'
            records.append(self.command(mode+'-allocator',common+flags+['-DINVENTORY_ALLOCATOR_TRANSLATION_UNIT','-c',str(test),'-o',str(obj)]))
            if okay(records[-1]): records.append(self.command(mode+'-compile',common+flags+[str(test),str(obj),'-o',str(binary)]))
            if okay(records[-1]):
                records.append(self.command(mode+'-run',[str(binary)]))
                self.semantics(records[-1], 'contract')
            self.log(mode,records)
        binary=self.output/'build'/'retained-seven.exe'
        records=[self.command('retained-seven-compile',common+['-O2',str(source),'-o',str(binary)])]
        if okay(records[-1]):
            records.append(self.command('retained-seven-run',[str(binary)]))
            self.semantics(records[-1], 'retained-seven')
        self.log('retained-seven',records,{'benchmark':False})
        # Exact, auditable source mutations; no feature switches in the production model.
        original=source.read_text(encoding='utf-8')
        mutations={
          'publish-before-record': ('        const auto inserted = processed_.emplace(requestId, SuccessRecord{itemId, count, prepared});',
                                    '        Publish(plan, itemId);\n        const auto inserted = processed_.emplace(requestId, SuccessRecord{itemId, count, prepared});'),
          'ignore-intent-conflict': ('if (found->second.itemId != itemId || found->second.count != count)', 'if (false)'),
          'recompute-replay-result': ('out = found->second.result;', 'out = found->second.result; out.after = Count(itemId);')}
        mutation_dir=self.output/'mutations'; mutation_dir.mkdir()
        for name,(before,after) in mutations.items():
            if original.count(before)!=1:
                self.failed=True; self.log(name,[],{'mutation_error':'exact mutation site is not unique'}); continue
            changed=original.replace(before,after)
            if name=='publish-before-record':
                # Move (rather than duplicate) the publication to reproduce the actual old window.
                changed=changed.replace('        Publish(plan, itemId);\n        out = prepared;', '        out = prepared;')
            mutant=mutation_dir/(name+'.cpp'); write_new(mutant,changed)
            binary=self.output/'build'/f'{name}.exe'
            records=[self.command(name+'-compile',common+['-O2',f'-DINVENTORY_SOURCE="{mutant}"',str(test),str(self.output/'build/allocator-o2.o'),'-o',str(binary)])]
            if okay(records[-1]):
                records.append(self.command(name+'-run',[str(binary)],expected_failure=True))
                self.semantics(records[-1], 'mutant')
            self.log(name,records,{'mutant_sha256':sha(mutant),'expected_nonzero_test_exit':True})
        return self.finish()


FIXTURE_COMPILER = r'''#!PYTHON
import json, os, pathlib, sys
mode=os.environ.get('FIXTURE_MODE','ok'); target=os.environ.get('FIXTURE_TARGET','skill_pipeline')
def event(kind,name):
    with open(os.environ['FIXTURE_EVENTS'],'a',encoding='utf-8') as f: f.write(json.dumps([kind,name])+"\n")
if sys.argv[1:] == ['--version']:
    print('fixture compiler'); sys.exit(19 if mode=='version-fail' else 0)
if sys.argv[1:] == ['-dumpmachine']: print('fixture-target'); sys.exit(0)
output=pathlib.Path(sys.argv[sys.argv.index('-o')+1]); name=output.stem
# Legacy source names remain the canonical five; focused uses output names.
sources=[pathlib.Path(x).stem for x in sys.argv if x.endswith('.cpp')]
name=sources[0] if len(sources)==1 and sources[0] in ('inventory_txn','buff_conflict','skill_pipeline','attr_modifier_bench','entity_lifecycle') else name
event('compile',name)
if '-c' in sys.argv:
    output.write_text('fixture object'); sys.exit(21 if mode=='driver-compile-fail' else 0)
code='#!'+sys.executable+'\nimport os, json, sys, time\n'
code+='with open(os.environ["FIXTURE_EVENTS"],"a") as f: f.write(json.dumps(["run",'+repr(name)+'])+"\\n")\n'
code+='print("fixture stdout '+name+'",flush=True)\nprint("fixture stderr '+name+'",file=sys.stderr,flush=True)\n'
if mode=='binary-output':
    code+='os.write(1, '+repr(b'\xff|\\xff\r\n\x00\xfe')+')\n'
    code+='os.write(2, '+repr(b'\xfe|\\xfe\r\n\x00\xff')+')\n'
if name.startswith('contract-'):
    lines=[]
    if mode not in ('driver-empty','driver-missing-result'):
        lines=['PASS_GROUP independent_stacking_remove_original_result_conflict', 'PASS_GROUP inputs_retry_retained_memory_wide_totals',
               'PASS_GROUP no_throw_results_and_external_serialization_boundary']
        for scenario in ('empty_small','empty_max_batch','topup_and_new_slots','map_growth','capacity_rejection_plan','tight_prefilled_max_batch'):
            lines.append('ALLOCATION_DISCOVERY scenario='+scenario+' calls=1 buckets_before=1 buckets_after=13')
        result='RESULT checks=1 fail=0 (explicit checks remain active with NDEBUG)'
        if mode=='driver-malformed-result': result='RESULT checks=not-a-number fail=0'
        elif mode=='driver-reported-failure': result='RESULT checks=1 fail=1 (explicit checks remain active with NDEBUG)'
        lines.append(result)
        if mode=='driver-duplicate-result': lines.append(result)
        if mode in ('driver-fail-marker','driver-reported-failure'): lines.append('FAIL injected semantic failure')
    if mode=='driver-empty':
        code='#!'+sys.executable+'\nimport os,json,sys,time\nwith open(os.environ["FIXTURE_EVENTS"],"a") as f: f.write(json.dumps(["run",'+repr(name)+'])+"\\n")\n'
    code+='print('+repr('\n'.join(lines))+',flush=True)\n'
elif name=='inventory_txn' and mode!='binary-output':
    code+='print('+repr('\n'.join('PASS  T'+str(n)+' fixture' for n in range(1,8))+'\nRESULT pass=7 fail=0')+',flush=True)\n'
if mode in ('group-child-timeout','escaped-child-timeout'):
    child_dir=pathlib.Path(os.environ['FIXTURE_CHILD_DIR'])
    marker=child_dir/(name+'.done'); pidfile=child_dir/(name+'.pid')
    child_code='import pathlib,sys,time; print("child stdout",flush=True); print("child stderr",file=sys.stderr,flush=True); time.sleep('+('1.5' if mode=='escaped-child-timeout' else '10')+'); pathlib.Path('+repr(str(marker))+').write_text("done")'
    code+='import subprocess, pathlib\nchild=subprocess.Popen([sys.executable,"-c",'+repr(child_code)+'],start_new_session='+repr(mode=='escaped-child-timeout')+')\n'
    code+='pathlib.Path('+repr(str(pidfile))+').write_text(str(child.pid))\ntime.sleep(10)\n'
if mode=='timeout' or mode=='driver-timeout': code+='time.sleep(10)\n'
if mode=='write-limit' and name==target: code+='print("x"*16384,flush=True)\n'
status=31 if (mode=='run-fail' and name==target) or mode=='driver-test-fail' or (mode=='driver-o2-fail' and '-O2' in sys.argv) else 0
code+='sys.exit('+str(status)+')\n'
output.write_text(code); output.chmod(0o700)
if mode in ('log-directory','log-symlink') and name==target:
    log=output.parent.parent/'logs'/ (name+'.txt')
    if mode=='log-directory': log.mkdir()
    else: log.symlink_to(os.environ['FIXTURE_SENTINEL'])
if mode=='compile-fail' and name==target: sys.exit(23) # leaves stale runnable trap intentionally
'''


def self_test(args: argparse.Namespace, output: Path) -> int:
    if os.name != 'posix': raise RuntimeError('runner fixtures currently require POSIX; native Windows is not covered')
    fixture=output/'fixture repository 空格'; (fixture/HERE/'scripts').mkdir(parents=True); (fixture/HERE/'src').mkdir(); (fixture/HERE/'tests').mkdir()
    for name in ('run_all.sh','build_run.ps1','run_inventory_contract.py'):
        shutil.copyfile(args.root/HERE/'scripts'/name,fixture/HERE/'scripts'/name)
    for name in NAMES: write_new(fixture/HERE/'src'/f'{name}.cpp','// isolated fixture: '+name+'\n')
    write_new(fixture/HERE/'tests/inventory_contract.cpp','// isolated fixture\n')
    compiler=output/'fixture compiler 空格'; write_new(compiler,FIXTURE_COMPILER.replace('PYTHON',sys.executable,1)); compiler.chmod(0o700)
    records=[]; failures=[]; assertions=0
    sentinel=output/'never overwrite sentinel'; write_new(sentinel,'OLD RAW MUST SURVIVE\n'); sentinel_hash=sha(sentinel)
    def require(condition: bool, label: str) -> None:
        nonlocal assertions
        assertions+=1
        if not condition: failures.append(label); print('SELFTEST_FAIL '+label,flush=True)
    def invoke(label: str, command: list[str], destination: Path, mode='ok', target='skill_pipeline', limit=False) -> tuple[dict,list]:
        events=output/(label+'.events.txt'); children=output/(label+' children'); children.mkdir(); env=dict(os.environ,CXX=str(compiler),PYTHON=sys.executable,FIXTURE_EVENTS=str(events),
              FIXTURE_MODE=mode,FIXTURE_TARGET=target,FIXTURE_SENTINEL=str(sentinel),FIXTURE_CHILD_DIR=str(children))
        # Real OS write failure in the driver's subprocess, not a mock returning success.
        if limit:
            limiter=output/(label+' python write limiter')
            write_new(limiter, '#!'+sys.executable+'\nimport os,resource,sys\nresource.setrlimit(resource.RLIMIT_FSIZE,(4096,4096))\nos.execv('+repr(sys.executable)+', ['+repr(sys.executable)+']+sys.argv[1:])\n')
            limiter.chmod(0o700)
            env['PYTHON']=str(limiter)
        r=run(command,fixture,env,20,output); r.update(label=label,fixture=True); records.append(r)
        write_new(output/(label+'.txt'),format_record(r))
        rows=[json.loads(line) for line in events.read_text().splitlines()] if events.exists() else []
        return r,rows
    runners={'shell':lambda out:['bash',str(fixture/HERE/'scripts/run_all.sh'),'--output-dir',str(out)]}
    pwsh=args.pwsh or shutil.which('pwsh')
    if pwsh:
        runners['pwsh']=lambda out:[pwsh,'-NoLogo','-NoProfile','-File',str(fixture/HERE/'scripts/build_run.ps1'),'-Root',str(fixture),'-Gxx',str(compiler),'-OutputDir',str(out)]
    else:
        require(False,'pwsh unavailable: provide --pwsh; no silent PowerShell coverage skip')
    for runner,make in runners.items():
        cases=[('success','ok','skill_pipeline'),('binary-output','binary-output','skill_pipeline')]
        for mode in ('compile-fail','run-fail'):
            for target in (NAMES[0],NAMES[2],NAMES[-1]): cases.append((mode+'-'+target,mode,target))
        cases += [('version-fail','version-fail',NAMES[0]),('log-directory','log-directory',NAMES[2]),
                  ('log-symlink','log-symlink',NAMES[2]),('write-limit','write-limit',NAMES[2])]
        for case,mode,target in cases:
            label=runner+'-'+case; destination=output/(label+' output 空格')
            r,events=invoke(label,make(destination),destination,mode,target,limit=mode=='write-limit')
            require(okay(r)==(mode in ('ok','binary-output')),label+' exit reflects outcome')
            compiles=[n for kind,n in events if kind=='compile']; runs=[n for kind,n in events if kind=='run']
            if mode=='version-fail': require(not compiles,label+' no compile after failed compiler probe')
            else:
                require(compiles==list(NAMES),label+' all five compile attempted in order')
                require(runs==[n for n in NAMES if not(mode=='compile-fail' and n==target)],label+' no stale executable; later targets run')
            if mode=='ok':
                for name in NAMES:
                    log=destination/'logs'/f'{name}.txt'
                    require(log.is_file() and 'fixture stdout '+name in log.read_text() and 'fixture stderr '+name in log.read_text(),label+' complete raw '+name)
                    require('"exit_code": 0' in log.read_text() and 'SOURCE_SHA256' in log.read_text(),label+' metadata '+name)
            if mode=='binary-output':
                captured=json.loads((destination/'commands.json').read_text())
                for name in NAMES:
                    entry=next(row for row in captured if row['label']==name+'-run')
                    for channel,payload in [('stdout',b'\xff|\\xff\r\n\x00\xfe'),('stderr',b'\xfe|\\xfe\r\n\x00\xff')]:
                        raw=base64.b64decode(entry[channel+'_base64'],validate=True)
                        require(payload in raw and entry[channel+'_bytes']==len(raw) and entry[channel+'_sha256']==hashlib.sha256(raw).hexdigest(),label+' exact binary '+name+' '+channel)
                        require(raw.startswith(('fixture '+channel+' '+name+'\n').encode()),label+' raw prefix '+name+' '+channel)
                        require(raw==('fixture '+channel+' '+name+'\n').encode()+payload,label+' exact complete bytes '+name+' '+channel)
            if mode in ('log-directory','log-symlink','write-limit'):
                require('wrote '+str(destination/'logs'/f'{target}.txt') not in r['stdout'],label+' no false wrote')
                require('DRIVER_RESULT PASS' not in r['stdout'],label+' no false success footer')
            require(sha(sentinel)==sentinel_hash,label+' sentinel preserved')
        for kind in ('directory','file','symlink','dangling-symlink','missing-parent','repo-path','repo-alias','missing-compiler'):
            label=runner+'-'+kind; destination=output/(label+' output')
            if kind=='directory': destination.mkdir(); write_new(destination/'keep','keep\n')
            elif kind=='file': write_new(destination,'keep\n')
            elif kind=='symlink': destination.symlink_to(sentinel)
            elif kind=='dangling-symlink': destination.symlink_to(output/'absent')
            elif kind=='missing-parent': destination=destination/'no-parent'/'out'
            elif kind=='repo-path': destination=fixture/'forbidden-output'
            elif kind=='repo-alias':
                alias=output/(label+' alias'); alias.symlink_to(fixture,target_is_directory=True); destination=alias/'forbidden-output'
            command=make(destination)
            if kind=='missing-compiler':
                # Both wrappers preserve single executable semantics; bypass only env for Shell.
                compiler.rename(output/'compiler temporarily unavailable')
                try: r,events=invoke(label,command,destination)
                finally: (output/'compiler temporarily unavailable').rename(compiler)
            else: r,events=invoke(label,command,destination)
            require(not okay(r),label+' refuses'); require(not events,label+' refuses before fixture compile/run')
            require(sha(sentinel)==sentinel_hash,label+' sentinel intact')
            if kind=='directory': require((destination/'keep').read_text()=='keep\n',label+' existing content intact')
            if kind=='file': require(destination.read_text()=='keep\n',label+' existing file intact')
    driver=fixture/HERE/'scripts/run_inventory_contract.py'
    for mode in ('timeout','group-child-timeout','escaped-child-timeout','driver-compile-fail','driver-test-fail','driver-o2-fail','driver-timeout',
                 'driver-empty','driver-missing-result','driver-malformed-result','driver-reported-failure','driver-fail-marker','driver-duplicate-result'):
        label='core-'+mode; destination=output/(label+' output')
        command=[sys.executable,str(driver),'--output-dir',str(destination),'--root',str(fixture),'--cxx',str(compiler),'--timeout-seconds','0.3']
        if mode in ('timeout','group-child-timeout','escaped-child-timeout'): command.append('--legacy-five-targets')
        r,events=invoke(label,command,destination,mode)
        require(not okay(r),label+' nonzero')
        executed=json.loads((destination/'commands.json').read_text())
        by_label={entry['label']:entry for entry in executed}
        if mode=='driver-compile-fail':
            require(all(by_label[m+'-allocator']['exit_code']==21 and m+'-run' not in by_label for m in MODES), label+' actual compile failures, no stale run')
        elif mode=='driver-test-fail':
            require(all(by_label[m+'-run']['exit_code']==31 for m in MODES), label+' actual test failures in every mode')
        elif mode=='driver-o2-fail':
            require(by_label['o0-ndebug-run']['exit_code']==0 and by_label['o0-ndebug-run']['semantic_valid'] and by_label['o2-run']['exit_code']==31, label+' O0 succeeds and optimized test actually fails')
        elif mode in ('driver-empty','driver-missing-result','driver-malformed-result','driver-reported-failure','driver-fail-marker','driver-duplicate-result'):
            require(all(by_label[m+'-run']['exit_code']==0 and not by_label[m+'-run']['semantic_valid'] for m in MODES),label+' actual zero exits rejected for semantic diagnostics')
        if 'timeout' in mode:
            require('"timed_out": true' in (destination/'commands.json').read_text(),label+' timeout diagnostic retained')
            require(r['elapsed_seconds']<12,label+' bounded outer execution')
            timed=[entry for entry in executed if entry['timed_out']]
            require(bool(timed) and all(not entry['capture_complete'] and entry['root_reaped'] for entry in timed),label+' timeout capture incomplete and root reaped')
        if mode in ('group-child-timeout','escaped-child-timeout'):
            require(len(timed)==5 and all(entry['elapsed_seconds']<1.2 for entry in timed),label+' no escaped-pipe EOF wait')
            children=output/(label+' children')
            pids=[int(path.read_text()) for path in children.glob('*.pid')]
            require(len(pids)==5,label+' five controlled child fixtures launched')
            def running(pid: int) -> bool:
                try: os.kill(pid,0)
                except ProcessLookupError: return False
                stat=Path('/proc')/str(pid)/'stat'
                if stat.exists():
                    try: return stat.read_text().split(') ')[1].split()[0] != 'Z'
                    except FileNotFoundError: return False
                return True
            # Short-lived escaped children are intentionally outside the group.
            # Wait for their bounded fixture lifetime, then verify none remain running.
            deadline=time.monotonic()+4
            while any(running(pid) for pid in pids) and time.monotonic()<deadline: time.sleep(0.02)
            live=[pid for pid in pids if running(pid)]
            if live:
                for pid in live:
                    try: os.kill(pid,signal.SIGKILL)
                    except ProcessLookupError: pass
            require(not live,label+' no long-lived fixture descendants left')
            done=list(children.glob('*.done'))
            require(len(done)==(5 if mode=='escaped-child-timeout' else 0),label+' escape naturally exits; same-group child terminated')
    # Default driver's own existing-root rejection, before compiler or files.
    destination=output/'existing focused output'; destination.mkdir(); write_new(destination/'keep','keep')
    r,events=invoke('focused-existing',[sys.executable,str(driver),'--output-dir',str(destination),'--root',str(fixture),'--cxx',str(compiler)],destination)
    require(not okay(r) and not events and (destination/'keep').read_text()=='keep','focused driver refuses existing directory')
    # Fixtures may not create repository build/results, even for failures.
    require(not (fixture/HERE/'build').exists() and not (fixture/HERE/'results').exists(),'fixture repositories remain output-free')
    write_new(output/'self-test.json',json.dumps({'assertions':assertions,'failures':failures,'runs':records,
              'source_sha256':{name:sha(args.root/HERE/'scripts'/name) for name in ('run_inventory_contract.py','run_all.sh','build_run.ps1')},
              'coverage':'POSIX Shell and Linux PowerShell, fake compiler/programs only; not native Windows'},indent=2)+'\n')
    print(f'SELFTEST_RESULT assertions={assertions} fail={len(failures)}',flush=True)
    return int(bool(failures))


def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir',required=True,type=Path)
    parser.add_argument('--root',type=Path,default=ROOT)
    parser.add_argument('--cxx',default=os.environ.get('CXX','g++'))
    parser.add_argument('--pwsh',help='explicit PowerShell executable for --self-test')
    parser.add_argument('--timeout-seconds',type=float,default=30)
    modes=parser.add_mutually_exclusive_group(); modes.add_argument('--self-test',action='store_true'); modes.add_argument('--legacy-five-targets',action='store_true')
    args=parser.parse_args(); args.root=args.root.resolve()
    if not 0 < args.timeout_seconds <= 600: parser.error('--timeout-seconds must be in (0,600]')
    try:
        output=fresh_output(args.output_dir,[ROOT,args.root])
        if args.self_test: return self_test(args,output)
        driver=Driver(args,output)
        return driver.legacy() if args.legacy_five_targets else driver.focused()
    except (OSError,ValueError,RuntimeError) as error:
        print(f'DRIVER_ERROR {type(error).__name__}: {error}',file=sys.stderr,flush=True)
        return 1


if __name__=='__main__': sys.exit(main())

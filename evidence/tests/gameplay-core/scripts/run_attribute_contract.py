#!/usr/bin/env python3
"""Bounded Attribute-only contracts. No default or fixture invokes --benchmark.
--mode all: O0+NDEBUG, O2, UBSan; --negative-controls: real model/harness mutations.
--self-test: isolated compiler/program CLI fixtures, active with Python -O.
All modes require a fresh external output directory and preserve original bytes.
"""
from __future__ import annotations
import argparse
import base64
import importlib.util
import json
import math
import os
from pathlib import Path
import re
import shutil
import sys

# Reuse capture and path protection read-only; never invoke Inventory's driver/tests.
sys.dont_write_bytecode = True
HELPER = Path(__file__).with_name('run_inventory_contract.py')
spec = importlib.util.spec_from_file_location('attribute_capture_helpers', HELPER)
if spec is None or spec.loader is None:
    raise RuntimeError('cannot load required read-only capture helper')
helpers = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helpers)
sha, write_new, fresh_output = helpers.sha, helpers.write_new, helpers.fresh_output
run, okay, format_record = helpers.run, helpers.okay, helpers.format_record
ROOT = Path(__file__).resolve().parents[4]
HERE = Path('evidence/tests/gameplay-core')
SOURCE_PATHS = [HERE/'src/attr_modifier_bench.cpp', HERE/'tests/attribute_contract.cpp',
                HERE/'scripts/run_attribute_contract.py', HERE/'scripts/run_inventory_contract.py']
MODES = {'o0-ndebug':['-O0','-DNDEBUG'], 'o2':['-O2'],
         'ubsan':['-O1','-g','-fsanitize=undefined','-fno-sanitize-recover=undefined']}
SAFE_FLAGS = ['-std=c++17','-Wall','-Wextra','-Werror','-pedantic',
              '-fno-fast-math','-fno-associative-math','-ffp-contract=off']
# Versioned before first run. Every case must report exactly checks 1..N once.
CASE_CHECKS = dict.fromkeys(('base category_order duplicate_source signed_negative negative_mul zero_mul '
    'add_order_forward add_order_reverse underflow_allowed mul_order_forward mul_order_reverse').split(), 12)
CASE_CHECKS.update(mutation_sequence=80, override_order=49, coalescing_and_barrier=12,
                  per_attribute_failure=26, explicit_refresh_failure=5)
for _bad in ('nan','posinf','neginf'):
    for _op in ('base','add','mul','override'):
        CASE_CHECKS[_op+'_'+_bad] = 15
CASE_CHECKS.update(dict.fromkeys(('masked_base masked_override zero_masks_add invalid_op negative_op masked_invalid_op '
    'sum_overflow sum_overflow_cancels product_overflow product_overflow_zero final_add_overflow '
    'final_mul_overflow negative_overflow').split(),15))
VERSION = 1


def semantic_errors(record: dict, kind: str = 'contract', target: str | None = None) -> list[str]:
    errors = []
    text = record['stdout']
    lines = text.splitlines()
    if kind == 'examples':
        if lines != ['EXAMPLE_RESULT version=1 checks=10 fail=0 benchmark=off']:
            errors.append('exact bounded example protocol required')
    else:
        if lines.count('ATTRIBUTE_CONTRACT version=1') != 1:
            errors.append('exactly one supported version header required')
        checks = {}; completions = {}; result = []
        for line in lines:
            if line == 'ATTRIBUTE_CONTRACT version=1': continue
            m = re.fullmatch(r'CHECK (\S+) ([1-9]\d*) (PASS|FAIL)', line)
            if m:
                name, number, status = m.groups(); key = (name, int(number))
                if key in checks: errors.append('duplicate check: '+str(key))
                checks[key] = status
                continue
            m = re.fullmatch(r'CASE (\S+) checks=(\d+) fail=(\d+)', line)
            if m:
                name, count, failures = m.groups()
                if name in completions: errors.append('duplicate case: '+name)
                completions[name] = (int(count), int(failures))
                continue
            m = re.fullmatch(r'RESULT version=1 cases=(\d+) checks=(\d+) fail=(\d+)', line)
            if m:
                result.append(tuple(map(int, m.groups()))); continue
            errors.append('unrecognized/malformed protocol line: '+line[:100])
        expected = {(name, n) for name, count in CASE_CHECKS.items() for n in range(1, count+1)}
        if set(checks) != expected: errors.append('missing/unknown checks in exact versioned manifest')
        if set(completions) != set(CASE_CHECKS): errors.append('missing/unknown cases in exact versioned manifest')
        actual_fails = sum(v == 'FAIL' for v in checks.values())
        for name, count in CASE_CHECKS.items():
            failures = sum(status == 'FAIL' for (case, _), status in checks.items() if case == name)
            if completions.get(name) != (count, failures): errors.append('case count/fail mismatch: '+name)
        if result != [(len(CASE_CHECKS), sum(CASE_CHECKS.values()), actual_fails)]:
            errors.append('exact single RESULT counts required')
        if kind == 'mutant':
            if not actual_fails or not any(case == target and value == 'FAIL' for (case, _), value in checks.items()):
                errors.append('mutant must explicitly fail intended semantic case')
        elif actual_fails:
            errors.append('unexpected semantic FAIL')
    if record['exit_code'] != (1 if kind == 'mutant' else 0): errors.append('unexpected native exit')
    if record['timed_out'] or not record['capture_complete']: errors.append('timeout/incomplete capture is not semantic success')
    if record['stderr_bytes']: errors.append('unexpected stderr/sanitizer diagnostic')
    return errors


def mutation_specs(model: str, test: str) -> list[tuple[str, str, str, str]]:
    """Exact mutations touch behavior, never expected literals/reference definitions."""
    def replaced(text: str, old: str, new: str) -> str:
        if text.count(old) != 1: raise ValueError('mutation site not unique: '+old)
        return text.replace(old, new)
    first = replaced(model, 'if (m.op == kOverride) value = m.value;',
                     'if (m.op == kOverride) { value = m.value; break; }')
    begin = model.index('    double value = a.base;')
    end = model.index('\n}\n\n// Does not roll back', begin)
    input_order = model[:begin] + '''    double value = a.base;
    for (const Modifier& m : a.mods) {
        if (m.op == kAdd) value += m.value;
        else if (m.op == kMul) value *= m.value;
        else value = m.value;
        if (!std::isfinite(value)) return {EvalError::kRange, 0.0};
    }
    return {EvalError::kNone, value};''' + model[end:]
    nodirty = replaced(test, 'void MarkDirty(Attribute& dut) { dut.dirty = true; }',
                       'void MarkDirty(Attribute& dut) { (void)dut; }')
    norefresh = replaced(test, '(void)TryRefresh(a);', '(void)a;')
    drop = replaced(test, 'entities[0].mods.push_back({kAdd, 15.0, 10}); // MUTATION_SITE dropped_modifier',
                    '// MUTATION_SITE dropped_modifier: intended Add is lost only from DUT')
    dirty_failure = replaced(model, '        a.dirty = true;\n        return candidate.error;',
                            '        a.dirty = false;\n        return candidate.error;')
    masked = replaced(model, 'if (!std::isfinite(a.base)) return {EvalError::kNonfiniteInput, 0.0};',
                      'if (a.mods.empty() && !std::isfinite(a.base)) return {EvalError::kNonfiniteInput, 0.0};')
    return [('missing-dirty', model, nodirty, 'mutation_sequence'),
            ('missing-refresh', model, norefresh, 'mutation_sequence'),
            ('dropped-modifier', model, drop, 'mutation_sequence'),
            ('first-override', first, test, 'override_order'),
            ('input-order', input_order, test, 'category_order'),
            ('failure-clears-dirty', dirty_failure, test, 'explicit_refresh_failure'),
            ('masked-base-unchecked', masked, test, 'masked_base')]


class Driver:
    def __init__(self, args: argparse.Namespace, output: Path):
        self.args, self.output = args, output
        self.env = helpers.child_environment(args.cxx)
        self.sources = {str(p): sha(args.root/p) for p in SOURCE_PATHS}
        self.executing_code = {str(p.resolve()): sha(p) for p in (Path(__file__), HELPER)}
        self.records = []; self.failed = False
        (output/'build').mkdir(); (output/'logs').mkdir()

    def unchanged(self) -> bool:
        return (all(sha(self.args.root/p) == value for p, value in self.sources.items()) and
                all(sha(Path(p)) == value for p, value in self.executing_code.items()))

    def command(self, label: str, argv: list[str], exit_code: int | None = 0) -> dict:
        if not self.unchanged(): raise RuntimeError('source/helper/runner changed during run; do not mix versions')
        record = run(argv, self.args.root, self.env, self.args.timeout_seconds, self.output)
        record.update(label=label, expected_exit_code=exit_code)
        self.records.append(record)
        if exit_code is not None and (record['exit_code'] != exit_code or record['timed_out'] or not record['capture_complete']):
            self.failed = True
        return record

    def log(self, name: str, records: list[dict], metadata: dict | None = None) -> None:
        try:
            write_new(self.output/'logs'/(name+'.txt'), 'SOURCE_SHA256 '+json.dumps(self.sources,sort_keys=True)+'\n'+
                'METADATA '+json.dumps(metadata or {},sort_keys=True)+'\n'+''.join(format_record(r) for r in records))
            print('wrote '+str(self.output/'logs'/(name+'.txt')),flush=True)
        except OSError as e:
            self.failed=True; print('LOG_FAILURE '+name+': '+str(e),file=sys.stderr,flush=True)

    def semantic(self, record: dict, kind='contract', target=None) -> None:
        errors=semantic_errors(record,kind,target)
        record.update(semantic_valid=not errors,semantic_errors=errors)
        if errors: self.failed=True

    def build_run(self, name: str, source: Path, flags: list[str], kind='contract', target=None, definitions=None, included_model=None) -> None:
        binary=self.output/'build'/(name+'.exe')
        records=[self.command(name+'-compile',[self.args.cxx]+SAFE_FLAGS+flags+(definitions or [])+[str(source),'-o',str(binary)])]
        if okay(records[-1]):
            records.append(self.command(name+'-run',[str(binary)],1 if kind=='mutant' else 0))
            self.semantic(records[-1],kind,target)
        self.log(name,records,{'kind':kind,'target_case':target,'no_benchmark':True,
            'translation_unit':str(source),'translation_unit_sha256':sha(source),
            'included_model_sha256':sha(included_model or self.args.root/HERE/'src/attr_modifier_bench.cpp'),
            'binary_sha256':sha(binary) if binary.is_file() and okay(records[0]) else None})

    def execute(self) -> int:
        probes=[self.command('compiler-version',[self.args.cxx,'--version']),
                self.command('compiler-target',[self.args.cxx,'-dumpmachine'])]
        self.log('compiler',probes)
        if not all(okay(r) for r in probes): return self.finish()
        model=self.args.root/HERE/'src/attr_modifier_bench.cpp'
        test=self.args.root/HERE/'tests/attribute_contract.cpp'
        modes=MODES if self.args.mode=='all' else {self.args.mode:MODES[self.args.mode]}
        for name, flags in modes.items(): self.build_run(name,test,flags)
        self.build_run('examples',model,['-O2'],'examples')
        binary=self.output/'build/examples.exe'
        if binary.exists() and any(r['label']=='examples-compile' and okay(r) for r in self.records):
            records=[self.command('unknown-argument',[str(binary),'--unknown'],2),
                     self.command('too-many-arguments',[str(binary),'--help','extra'],2),
                     self.command('help',[str(binary),'--help'])]
            if records[-1]['stdout'].strip() != 'usage: attr_modifier_bench [--benchmark|--help]; default runs bounded examples': self.failed=True
            self.log('cli',records)
        # These are expected compiler rejections, not counted as killed semantic mutants.
        for name,flag in [('fast-math','-ffast-math'),('finite-only','-ffinite-math-only')]:
            record=self.command('reject-'+name,[self.args.cxx]+SAFE_FLAGS+['-O2',flag,str(model),'-o',str(self.output/'build'/('rejected-'+name+'.exe'))],None)
            valid=(isinstance(record['exit_code'],int) and record['exit_code']>0 and not record['timed_out'] and
                   record['capture_complete'] and 'Attribute contracts require finite checks' in record['stderr'])
            record['expected_build_rejection_valid']=valid
            if not valid: self.failed=True
            self.log('reject-'+name,[record],{'not_a_semantic_mutant':True})
        if self.args.negative_controls:
            directory=self.output/'mutations'; directory.mkdir()
            for name, changed_model, changed_test, target in mutation_specs(model.read_text(),test.read_text()):
                source=directory/(name+'-model.cpp'); contract=directory/(name+'-contract.cpp')
                write_new(source,changed_model); write_new(contract,changed_test)
                self.build_run(name,contract,['-O2'],'mutant',target,[f'-DATTRIBUTE_SOURCE="{source}"'],source)
        return self.finish()

    def finish(self) -> int:
        if not self.unchanged(): self.failed=True
        provenance={'source_sha256':self.sources,'actual_executing_code_sha256':self.executing_code,'root':str(self.args.root),'python':sys.version,'python_optimize':sys.flags.optimize,
            'platform':sys.platform,'mode':self.args.mode,'negative_controls':self.args.negative_controls,
            'safe_flags':SAFE_FLAGS,'ignored_flag_variables_set':{k:k in os.environ for k in ('CXXFLAGS','CPPFLAGS','LDFLAGS')},
            'version':VERSION,'case_checks':CASE_CHECKS,'benchmark_executed':False,
            'capture_boundary':'Inherited finite regular-file snapshots; escaped descendant capture/termination not guaranteed',
            'macro_guard_scope':'Detected fast-math/finite-only macros; not all compiler FP modes or reassociation flags'}
        try:
            write_new(self.output/'commands.json',json.dumps(self.records,indent=2,ensure_ascii=True)+'\n')
            write_new(self.output/'provenance.json',json.dumps(provenance,indent=2,ensure_ascii=True)+'\n')
            write_new(self.output/'summary.json',json.dumps({'failed':self.failed,'commands':len(self.records),
                'semantic_runs':[{'label':r['label'],'valid':r['semantic_valid'],'errors':r['semantic_errors']} for r in self.records if 'semantic_valid' in r]},indent=2)+'\n')
        except OSError as e:
            self.failed=True; print('FINAL_LOG_FAILURE: '+str(e),file=sys.stderr)
        print('DRIVER_RESULT '+('FAIL' if self.failed else 'PASS'),flush=True)
        return int(self.failed)


def fixture_report(mode: str) -> str:
    lines=['ATTRIBUTE_CONTRACT version=1']
    for name,count in CASE_CHECKS.items():
        lines += [f'CHECK {name} {n} PASS' for n in range(1,count+1)]
        lines.append(f'CASE {name} checks={count} fail=0')
    result=f'RESULT version=1 cases={len(CASE_CHECKS)} checks={sum(CASE_CHECKS.values())} fail=0'
    lines.append(result)
    if mode=='empty': return ''
    if mode=='low-check': return 'ATTRIBUTE_CONTRACT version=1\nCHECK base 1 PASS\nCASE base checks=1 fail=0\nRESULT version=1 cases=1 checks=1 fail=0\n'
    if mode=='missing-check': lines.remove('CHECK base 1 PASS')
    if mode=='missing-case': lines.remove('CASE base checks=12 fail=0')
    if mode=='missing-result': lines.pop()
    if mode=='duplicate-result': lines.append(result)
    if mode=='duplicate-check': lines.insert(1,'CHECK base 1 PASS')
    if mode=='duplicate-case': lines.insert(1,'CASE base checks=12 fail=0')
    if mode=='malformed': lines[-1]='RESULT version=1 cases=41 checks=bad fail=0'
    if mode=='wrong-version': lines[0]='ATTRIBUTE_CONTRACT version=2'
    if mode=='failure-exit-zero':
        lines[1]='CHECK base 1 FAIL'; lines[13]='CASE base checks=12 fail=1'
        lines[-1]=result[:-1]+'1'
    if mode=='fail-marker': lines.append('FAIL unrelated injected diagnostic')
    return '\n'.join(lines)+'\n'


FIXTURE_COMPILER = r'''#!PYTHON
import json, os, pathlib, sys
mode=os.environ['ATTRIBUTE_FIXTURE_MODE']
events=pathlib.Path(os.environ['ATTRIBUTE_FIXTURE_EVENTS'])
def event(kind,name):
    with events.open('a') as f: f.write(json.dumps([kind,name])+"\n")
if sys.argv[1:]==['--version']:
    print('attribute fixture compiler'); sys.exit(19 if mode=='compiler-fail' else 0)
if sys.argv[1:]==['-dumpmachine']: print('fixture-target'); sys.exit(0)
if '-ffast-math' in sys.argv or '-ffinite-math-only' in sys.argv:
    print('error: Attribute contracts require finite checks and ordered floating-point evaluation',file=sys.stderr); sys.exit(1)
output=pathlib.Path(sys.argv[sys.argv.index('-o')+1]); name=output.stem
event('compile',name)
code='#!'+sys.executable+'\nimport os,json,sys,time\n'
code+='with open('+repr(str(events))+',"a") as f: f.write(json.dumps(["run",'+repr(name)+'])+"\\n")\n'
if name=='examples':
    code+='if len(sys.argv)>1:\n    if sys.argv[1:]==["--help"]: print("usage: attr_modifier_bench [--benchmark|--help]; default runs bounded examples"); sys.exit(0)\n    sys.exit(2)\n'
    code+='print("EXAMPLE_RESULT version=1 checks=10 fail=0 benchmark=off")\n'
else:
    code+='os.write(1,'+repr(pathlib.Path(os.environ['ATTRIBUTE_FIXTURE_REPORT']).read_bytes())+')\n'
    if mode=='binary-output': code+='os.write(1,b"\\xff|\\\\xff\\r\\n\\x00\\xfe"); os.write(2,b"\\xfe|\\\\xfe\\r\\n\\x00\\xff")\n'
    if mode=='timeout': code+='time.sleep(10)\n'
    if mode=='runtime-fail': code+='sys.exit(31)\n'
output.write_text(code); output.chmod(0o700)
if name=='o2':
    if mode=='compile-fail': sys.exit(23) # leaves a real runnable stale trap
    if mode in ('log-directory','log-symlink'):
        log=output.parent.parent/'logs/o2.txt'
        if mode=='log-directory': log.mkdir()
        else: log.symlink_to(os.environ['ATTRIBUTE_FIXTURE_SENTINEL'])
'''


def self_test(args: argparse.Namespace, output: Path) -> int:
    if os.name!='posix': raise RuntimeError('these fixtures cover POSIX only; native Windows is not claimed')
    initial={str(p):sha(args.root/p) for p in SOURCE_PATHS}
    fixture=output/'fixture repository 空格'
    for relative in SOURCE_PATHS:
        path=fixture/relative; path.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(args.root/relative,path)
    compiler=output/'fixture compiler 空格'
    write_new(compiler,FIXTURE_COMPILER.replace('PYTHON',sys.executable,1)); compiler.chmod(0o700)
    sentinel=output/'original raw sentinel'; write_new(sentinel,'DO NOT OVERWRITE\n'); original=sha(sentinel)
    driver=fixture/HERE/'scripts/run_attribute_contract.py'
    checks=0; failures=[]; records=[]
    def require(condition: bool, message: str) -> None:
        nonlocal checks
        checks+=1
        if not condition: failures.append(message); print('SELFTEST_FAIL '+message,flush=True)
    def invoke(name: str, mode='ok', destination=None, extra=None, limited=False, runner=driver):
        destination=destination or output/(name+' output')
        report=output/(name+' report.txt'); write_new(report,fixture_report(mode))
        events=output/(name+' events.jsonl')
        env=dict(os.environ,CXXFLAGS='ATTRIBUTE_SYNTHETIC_UNUSED_PRIVATE_VALUE',ATTRIBUTE_FIXTURE_MODE=mode,ATTRIBUTE_FIXTURE_EVENTS=str(events),
                 ATTRIBUTE_FIXTURE_REPORT=str(report),ATTRIBUTE_FIXTURE_SENTINEL=str(sentinel))
        command=[sys.executable,'-O','-B',str(runner),'--root',str(fixture),'--cxx',str(compiler),
                 '--mode','o2','--timeout-seconds','0.3','--output-dir',str(destination)]
        if extra: command += extra
        if limited:
            limiter=output/(name+' limiter.py')
            write_new(limiter,'import os,resource,sys\nresource.setrlimit(resource.RLIMIT_FSIZE,(65536,65536))\nos.execv(sys.executable,[sys.executable]+sys.argv[1:])\n')
            command=[sys.executable,str(limiter)]+command[1:]
        record=run(command,fixture,env,20,output); record.update(label=name,fixture=True)
        records.append(record); write_new(output/(name+'.txt'),format_record(record))
        rows=[json.loads(line) for line in events.read_text().splitlines()] if events.exists() else []
        return record, rows, destination
    modes=['ok','compiler-fail','compile-fail','runtime-fail','timeout','log-directory','log-symlink',
           'empty','low-check','missing-check','missing-case','missing-result','duplicate-result',
           'duplicate-check','duplicate-case','malformed','wrong-version','failure-exit-zero','fail-marker','binary-output']
    for mode in modes:
        record,events,destination=invoke('cli-'+mode,mode)
        require(okay(record)==(mode=='ok'),mode+' native CLI outcome')
        require(('DRIVER_RESULT PASS' in record['stdout'])==(mode=='ok'),mode+' no false success tail')
        require(sha(sentinel)==original,mode+' sentinel preserved')
        command_path=destination/'commands.json'
        entries=json.loads(command_path.read_text()) if command_path.exists() else []
        by_label={r['label']:r for r in entries}
        if mode=='ok':
            require(by_label['o2-run']['semantic_valid'] and by_label['examples-run']['semantic_valid'],'successful complete report')
            require(json.loads((destination/'provenance.json').read_text())['python_optimize']==1,'nested Python -O active')
            require('ATTRIBUTE_SYNTHETIC_UNUSED_PRIVATE_VALUE' not in (destination/'provenance.json').read_text() and
                    json.loads((destination/'provenance.json').read_text())['ignored_flag_variables_set']['CXXFLAGS'] is True,
                    'ignored flag presence recorded without leaking unused values')
            require(all('--benchmark' not in e['argv'] for e in entries),'no benchmark invoked')
        elif mode=='compiler-fail': require(not events,'compiler probe failure prevents build')
        elif mode=='compile-fail':
            require(by_label['o2-compile']['exit_code']==23 and 'o2-run' not in by_label,'compile failure never runs stale binary')
            require(['run','o2'] not in events,'stale trap not executed')
        elif mode=='runtime-fail': require(by_label['o2-run']['exit_code']==31,'native runtime failure propagated')
        elif mode=='timeout':
            require(by_label['o2-run']['timed_out'] and not by_label['o2-run']['capture_complete'],'timeout explicitly incomplete')
            require(record['elapsed_seconds']<10,'timeout bounded')
        elif mode in ('log-directory','log-symlink'):
            require('LOG_FAILURE o2' in record['stderr'],'real log-open failure')
        else:
            require(by_label['o2-run']['exit_code']==0 and not by_label['o2-run']['semantic_valid'],mode+' exit0 bad protocol rejected')
        if mode=='binary-output':
            for channel,raw in [('stdout',b'\xff|\\xff\r\n\x00\xfe'),('stderr',b'\xfe|\\xfe\r\n\x00\xff')]:
                entry=by_label['o2-run']; captured=base64.b64decode(entry[channel+'_base64'],validate=True)
                require(captured.endswith(raw) and entry[channel+'_bytes']==len(captured),channel+' exact binary bytes')
    record,_,destination=invoke('selected-root-provenance',runner=Path(__file__).resolve())
    require(okay(record),'separate selected root can run isolated fixture')
    actual=json.loads((destination/'provenance.json').read_text())['actual_executing_code_sha256']
    require(actual=={str(p.resolve()):sha(p) for p in (Path(__file__),HELPER)},'actual executing runner/helper identified independently of selected root')
    # 64 KiB file limit is real OS EFBIG during log write, not synthetic permission errors.
    record,_,_=invoke('cli-write-failure',limited=True)
    require(not okay(record) and 'DRIVER_RESULT PASS' not in record['stdout'],'real log-write failure propagates')
    require('LOG_FAILURE' in record['stderr'] or 'FINAL_LOG_FAILURE' in record['stderr'],'real log-write diagnostic')
    for kind in ('directory','file','symlink','dangling-symlink','missing-parent','repo-path','repo-alias'):
        destination=output/('refused-'+kind)
        if kind=='directory': destination.mkdir(); write_new(destination/'keep','keep')
        elif kind=='file': write_new(destination,'keep')
        elif kind=='symlink': destination.symlink_to(sentinel)
        elif kind=='dangling-symlink': destination.symlink_to(output/'absent')
        elif kind=='missing-parent': destination=destination/'missing'/'out'
        elif kind=='repo-path': destination=fixture/'forbidden-output'
        elif kind=='repo-alias':
            alias=output/'repo alias'; alias.symlink_to(fixture,target_is_directory=True); destination=alias/'forbidden-output'
        record,events,_=invoke('protect-'+kind,destination=destination)
        require(not okay(record) and not events,kind+' rejected before compiler')
        require(sha(sentinel)==original,kind+' original bytes intact')
        if kind=='directory': require((destination/'keep').read_text()=='keep','existing directory intact')
        if kind=='file': require(destination.read_text()=='keep','existing file intact')
    for name,extra in [('bad-arg',['--invalid']),('nan-timeout',['--timeout-seconds','nan']),('infinite-timeout',['--timeout-seconds','inf'])]:
        record,events,_=invoke(name,extra=extra)
        require(not okay(record) and not events,name+' invalid CLI rejected')
    require(not (fixture/HERE/'build').exists() and not (fixture/HERE/'results').exists(),'fixture repository output-free')
    require(all(sha(args.root/p)==digest for p,digest in initial.items()),'source and helper hashes stable during self-test')
    write_new(output/'self-test.json',json.dumps({'checks':checks,'failures':failures,'records':records,
        'source_sha256':initial,'python_optimize':sys.flags.optimize,'coverage':'POSIX isolated CLI fixtures only; no benchmark; inherited capture helper unchanged'},indent=2)+'\n')
    print(f'SELFTEST_RESULT version=1 checks={checks} fail={len(failures)}',flush=True)
    return int(bool(failures))


def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir',type=Path,required=True)
    parser.add_argument('--root',type=Path,default=ROOT)
    parser.add_argument('--cxx',default=os.environ.get('CXX','g++'))
    parser.add_argument('--mode',choices=['all',*MODES],default='all')
    parser.add_argument('--negative-controls',action='store_true')
    parser.add_argument('--timeout-seconds',type=float,default=30)
    parser.add_argument('--self-test',action='store_true')
    args=parser.parse_args(); args.root=args.root.resolve()
    if not math.isfinite(args.timeout_seconds) or not 0<args.timeout_seconds<=600:
        parser.error('--timeout-seconds must be finite and in (0,600]')
    try:
        output=fresh_output(args.output_dir,[ROOT,args.root])
        if args.self_test: return self_test(args,output)
        return Driver(args,output).execute()
    except (OSError,ValueError,RuntimeError) as e:
        print('DRIVER_ERROR '+type(e).__name__+': '+str(e),file=sys.stderr,flush=True)
        return 1

if __name__=='__main__': sys.exit(main())

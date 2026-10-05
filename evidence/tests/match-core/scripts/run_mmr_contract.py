#!/usr/bin/env python3
"""Bounded mmr-only contracts and real negative controls; never run benchmarks.
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
spec = importlib.util.spec_from_file_location('mmr_readonly_capture', HELPER)
if spec is None or spec.loader is None:
    raise RuntimeError('cannot load required read-only Inventory capture helper')
helpers = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helpers)
sha, write_new, fresh_output = helpers.sha, helpers.write_new, helpers.fresh_output
run, okay = helpers.run, helpers.okay
SOURCES = [HERE/'src/mmr_pool.cpp', HERE/'tests/mmr_contract.cpp',
           HERE/'scripts/run_mmr_contract.py', HELPER_REL]
MODES = {'o0-ndebug': ['-O0', '-DNDEBUG'], 'o2': ['-O2'],
         'ubsan': ['-O1', '-g', '-fsanitize=undefined', '-fno-sanitize-recover=undefined']}
FLAGS = ['-std=c++17', '-Wall', '-Wextra', '-Werror', '-pedantic']
# Versioned exact manifest, never inferred from a tested executable's output.
FIXTURES = ('solo repeat seed_party nonseed_party two_parties nonseed_remaining oversized '
    'party_outside_window candidate_cap cap_below_group partly_matched future9 future10 future11 '
    'future_candidate future_party future_party_boundary anchor_not_allpair window_equal window_outside '
    'reciprocal capped_outlier reverse_order negative_mmr bucket_int_max bucket_int_min tick_int64_max capacity_int_bound').split()
CASES = {f'{name}.{mode}': 14 for mode in range(3) for name in FIXTURES}
CASES.update(helper_boundaries=6, rng_population=4, benchmark_inputs=4, cohort_accounting=8)


def utc():
    return datetime.now(timezone.utc).isoformat()


def strict_json(text):
    """Reject ambiguous/non-finite protocol JSON rather than accepting Python extensions."""
    def pairs(items):
        result = {}
        for key, value in items:
            if key in result: raise ValueError('duplicate JSON key: '+key)
            result[key] = value
        return result
    def constant(value):
        raise ValueError('non-finite JSON constant: '+value)
    def number(value):
        result = float(value)
        if not math.isfinite(result): raise ValueError('non-finite JSON number')
        return result
    return json.loads(text, object_pairs_hook=pairs, parse_constant=constant, parse_float=number)


def inspect_observation(row):
    """Independent literal-data oracle, deliberately separate from tested C++."""
    errors=[]
    mode = row['mode']
    if type(mode) is not int or mode not in (0, 1, 2) or row['case'].rsplit('.', 1)[-1] != str(mode):
        raise ValueError('OBS mode must be an integer in 0..2 matching its case suffix')
    before, after, matches = row['before'], row['after'], row['matches']
    team, base, growth, maximum, _, _wait = row['config']; tick=row['tick']; used={}
    if row['after_tick'] != tick or row['after_config'] != row['config']: errors.append('pool configuration/tick changed')
    if len(before) != len(after): return errors+['population length changed']
    for lobby, match in enumerate(matches):
        ids=match['indices']
        if len(ids) != 2*team: errors.append('capacity')
        if not ids or any(type(i) is not int or i<0 or i>=len(before) for i in ids):
            errors.append('index bounds'); continue
        anchor=before[ids[0]]
        for index in ids:
            player=before[index]
            if index in used: errors.append('duplicate member')
            used[index]=lobby
            if player[4] or player[3]>tick: errors.append('eligibility')
            windows=[min(maximum,base+(tick-p[3])*growth) for p in (anchor,player)]
            if abs(anchor[1]-player[1])>min(windows): errors.append('reciprocal anchor window')
        spread=max(before[i][1] for i in ids)-min(before[i][1] for i in ids)
        if match['tick']!=tick or match['spread']!=spread or match['relaxed']!=(spread>base): errors.append('diagnostic')
    for i,(old,new) in enumerate(zip(before,after)):
        if old[:4]!=new[:4]: errors.append('immutable input')
        if i not in used:
            if old!=new: errors.append('unselected full state')
        else:
            if new[4:]!=[True,tick]: errors.append('selected state')
            if old[2]:
                group=[j for j,p in enumerate(before) if p[2]==old[2]]
                if any(j not in used or used[j]!=used[i] for j in group): errors.append('snapshot party')
    return errors


def semantics(record, kind='contract', target=None):
    errors=[]; lines=record['stdout'].splitlines(); mutant=kind=='mutant'
    if kind=='retained':
        statuses={}; results=[]
        for line in lines:
            if line.startswith(('PASS','FAIL')):
                found=re.fullmatch(r'(PASS|FAIL)  (M\d+) .*',line)
                if not found: errors.append('malformed retained check'); continue
                status,name=found.groups()
                if name in statuses: errors.append('duplicate retained check')
                statuses[name]=status
            if line.startswith('RESULT'):
                found=re.fullmatch(r'RESULT pass=(\d+) fail=(\d+)',line)
                if found: results.append(tuple(map(int,found.groups())))
                else: errors.append('malformed retained result')
        if statuses!={'M'+str(n):'PASS' for n in range(1,18)} or results!=[(17,0)]: errors.append('exact passing retained M1-M17 required')
    else:
        checks={}; completions={}; results=[]; observations={}; populations={}; cohorts=[]; oracle_failures={}
        if lines.count('MMR_CONTRACT version=1')!=1: errors.append('one supported header required')
        for line in lines:
            if line=='MMR_CONTRACT version=1': continue
            found=re.fullmatch(r'CHECK (\S+) ([1-9]\d*) (PASS|FAIL)',line)
            if found:
                name,n,status=found.groups(); key=(name,int(n))
                if key in checks: errors.append('duplicate check')
                checks[key]=status; continue
            found=re.fullmatch(r'CASE (\S+) checks=(\d+) fail=(\d+)',line)
            if found:
                name,count,failed=found.groups()
                if name in completions: errors.append('duplicate case')
                completions[name]=(int(count),int(failed)); continue
            found=re.fullmatch(r'RESULT version=1 cases=(\d+) checks=(\d+) fail=(\d+)',line)
            if found: results.append(tuple(map(int,found.groups()))); continue
            try:
                if line.startswith('OBS '):
                    row=strict_json(line[4:]); name=row['case']
                    if name in observations: errors.append('duplicate observation')
                    observations[name]=row
                    findings=inspect_observation(row)
                    if findings: oracle_failures[name]=findings
                    continue
                if line.startswith('POP '):
                    row=strict_json(line[4:]); name=row['case']
                    if name in populations: errors.append('duplicate population')
                    populations[name]=row['players']; continue
                if line.startswith('COHORT '): cohorts.append(strict_json(line[7:])); continue
            except (ValueError,KeyError,TypeError,IndexError,OverflowError): errors.append('malformed data record'); continue
            errors.append('unknown/malformed protocol: '+line[:80])
        expected={(name,n) for name,count in CASES.items() for n in range(1,count+1)}
        if set(checks)!=expected: errors.append('exact check manifest mismatch')
        if set(completions)!=set(CASES): errors.append('exact case manifest mismatch')
        if set(observations)!={f'{name}.{mode}' for name in FIXTURES for mode in range(3)}: errors.append('exact observation manifest mismatch')
        if set(populations)!={'rng_population','benchmark_inputs'} or len(cohorts)!=1: errors.append('population/cohort manifest mismatch')
        golden_rng=[[i+1,mmr,0,queued,False,-1] for i,(mmr,queued) in enumerate(((1280,1),(1406,0),(1520,3),(1466,3)))]
        golden_bench=[[i+1,mmr,0,0,False,-1] for i,mmr in enumerate((1159,1472,1408,1233,1150,1586,1231,1158,1183,1526,1510,1494))]
        if populations.get('rng_population')!=golden_rng: oracle_failures['rng_population']=['literal four-player golden vector']
        if populations.get('benchmark_inputs')!=golden_bench: oracle_failures['benchmark_inputs']=['literal twelve-player golden population']
        if len(cohorts)==1:
            row=cohorts[0]; expected_stats=[]
            if len(row['before'])!=1220 or len(row['after'])!=1220: oracle_failures['cohort_accounting']=['cohort population length']
            for start,end in ((0,1200),(1200,1220)):
                after=row['after'][start:end]; waits=sorted(p[5]-p[3] for p in after if p[4])
                expected_stats.append([len(after),sum(p[4] for p in after),sum(not p[4] for p in after),waits])
            bad_state=len(row['before'])!=len(row['after']) or any(a[:4]!=b[:4] or b[4] and not b[3]<=b[5]<=200 or not b[4] and a!=b for a,b in zip(row['before'],row['after']))
            bad_labels=any(p[0]!=i+1 for i,p in enumerate(row['before']))
            if row['reported']!=expected_stats or bad_state or bad_labels: oracle_failures['cohort_accounting']=['independent cohort labels/metrics/state']
        failed=sum(v=='FAIL' for v in checks.values())
        for name,count in CASES.items():
            failures=sum(v=='FAIL' for (case,_),v in checks.items() if case==name)
            if completions.get(name)!=(count,failures): errors.append('case counters inconsistent: '+name)
        if results!=[(len(CASES),sum(CASES.values()),failed)]: errors.append('exact single RESULT required')
        if mutant:
            if not failed or not any(name==target and status=='FAIL' for (name,_),status in checks.items()): errors.append('intended semantic mutant case must fail')
        elif failed or oracle_failures: errors.append('semantic oracle failure')
        record['independent_oracle_failures']=oracle_failures
    if record['exit_code']!=(1 if mutant else 0): errors.append('native exit mismatch')
    if record['timed_out'] or not record['capture_complete']: errors.append('timeout/incomplete capture')
    if record['stderr_bytes']: errors.append('unexpected stderr/sanitizer diagnostic')
    return errors


def mutations(model):
    def replace(old,new):
        if model.count(old)!=1: raise ValueError('mutation site not unique: '+old)
        return model.replace(old,new)
    members='const std::vector<int>& members = p.partyId == 0 ? solo : groups.at(p.partyId);'
    individual=replace(members,'const std::vector<int>& members = (p.partyId == 0 || idx != seedIdx) ? solo : groups.at(p.partyId);')
    truncate=replace(members,'std::vector<int> members = p.partyId == 0 ? solo : groups.at(p.partyId); if (members.size() > capacity) members.resize(capacity);')
    roster=replace('if (players[i].partyId != 0)','if (players[i].partyId != 0 && players[i].queuedAt <= tick)')
    future=model.replace('if (a.queuedAt > tick || b.queuedAt > tick) return false;','/* eligibility guard removed */')
    future=future.replace('seed.matched || seed.queuedAt > tick','seed.matched').replace('c.matched || c.queuedAt > tick ||','c.matched ||')
    future=future.replace('!players[i].matched && players[i].queuedAt <= tick','!players[i].matched')
    intersection=replace('diff <= std::min(WindowFor(a), WindowFor(b))','diff <= std::max(WindowFor(a), WindowFor(b))')
    allpair=replace('const Player& c = players[static_cast<size_t>(member)];','const Player& c = players[static_cast<size_t>(member)]; for (int prior : chosen) if (!Compatible(players[static_cast<size_t>(prior)], c)) return false;')
    wrongids=replace('m.ids = chosen;','m.ids = chosen; for (int& index : m.ids) index = players[static_cast<size_t>(index)].id;')
    duplicate=replace('m.ids = chosen;','m.ids = chosen; m.ids[1] = m.ids[0];')
    pollution=replace('out->push_back(m);','out->push_back(m); for (Player& p : players) if (!p.matched) { p.matchedAtTick = tick; break; }')
    order=replace('const int mmr = 1400 + rng.Range(-120, 120);\n        const int64_t queued = rng.Range(0, 3);','const int64_t queued = rng.Range(0, 3);\n        const int mmr = 1400 + rng.Range(-120, 120);')
    regenerate=replace('return {canonical, canonical, canonical};','std::array<Pool, 3> copies{canonical, canonical, canonical}; for (size_t mode = 1; mode < copies.size(); ++mode) for (Player& p : copies[mode].players) p.mmr = 1400 + rng.Range(-250, 250); return copies;')
    labels=replace('(p.id > 1200) != extreme','(p.id <= 1200) != extreme')
    start=model.index('    return normal.total == 1200'); end=model.index('\n}',start)
    tautology=model[:start]+'    (void)normal; return extreme.matched <= 20;'+model[end:]
    resize=model.replace('        return out;\n    }','        if (!players.empty()) players.pop_back();\n        return out;\n    }')
    config=replace('out->push_back(m);','out->push_back(m); cfg.baseWindow += 1;')
    clock=model.replace('        return out;\n    }','        tick = 0;\n        return out;\n    }')
    return [('resize-population',resize,'solo.0'),('config-pollution',config,'solo.0'),('tick-pollution',clock,'future10.0'),('individual-fill',individual,'nonseed_remaining.0'),('truncate-party',truncate,'oversized.0'),
        ('waiting-only-roster',roster,'future_party.0'),('remove-eligibility',future,'future9.0'),
        ('union-window',intersection,'reciprocal.0'),('all-pair-policy',allpair,'anchor_not_allpair.0'),
        ('indices-as-ids',wrongids,'solo.0'),('duplicate-selection',duplicate,'solo.0'),
        ('unselected-pollution',pollution,'nonseed_remaining.0'),('swap-rng-order',order,'rng_population'),
        ('regenerate-populations',regenerate,'benchmark_inputs'),('reverse-cohort-labels',labels,'cohort_accounting'),
        ('restore-tautology',tautology,'cohort_accounting')]


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
        self.started = utc(); self.failed = False; self.outcomes = []; self.contract_output_sha256 = None
        (output/'build').mkdir()
    def unchanged(self):
        return (all(sha(self.args.root/p) == digest for p, digest in self.source_identity.items()) and
                all(sha(Path(p)) == digest for p, digest in self.executing.items()))
    def command(self, label, argv, kind=None, target=None, metadata=None):
        if not self.unchanged(): raise RuntimeError('source/runner/helper changed during run')
        start = utc()
        record = run(argv, self.args.root, self.env, self.args.timeout_seconds, self.output)
        record.update(label=label, started_utc=start, ended_utc=utc(), metadata=metadata or {})
        try:
            errors = semantics(record, kind, target) if kind else ([] if okay(record) else ['command failed/timeout/incomplete capture'])
        except (ValueError, TypeError, KeyError, IndexError, OverflowError) as error:
            errors = ['malformed semantic data: '+type(error).__name__]
        if kind == 'contract':
            if self.contract_output_sha256 is None: self.contract_output_sha256 = record['stdout_sha256']
            elif self.contract_output_sha256 != record['stdout_sha256']: errors.append('cross-mode/process output mismatch')
        record.update(expected_exit_code=1 if kind == 'mutant' else 0,
                      semantic_kind=kind, target_case=target, valid=not errors, errors=errors)
        self.journal.add(record)
        self.outcomes.append(dict(record_number=record['record_number'], label=label, valid=not errors, errors=errors))
        if errors: self.failed = True
        return record
    def build(self, name, source, flags, kind='contract', target=None, included=None):
        binary = self.output/'build'/(name+'.exe')
        definitions = [f'-DMMR_SOURCE="{included}"'] if included else []
        identity = {'translation_unit': str(source), 'translation_unit_sha256': sha(source),
                    'model_sha256': sha(included or self.args.root/HERE/'src/mmr_pool.cpp')}
        built = self.command(name+'-compile', [self.args.cxx]+FLAGS+flags+definitions+[str(source), '-o', str(binary)], metadata=identity)
        if okay(built):
            first = self.command(name+'-run', [str(binary)]+(['--retained'] if kind == 'retained' else []), kind, target, dict(identity, binary_sha256=sha(binary)))
            if kind == 'contract':
                repeat = self.command(name+'-repeat', [str(binary)], kind, metadata=dict(identity, binary_sha256=sha(binary)))
                if first['stdout_base64'] != repeat['stdout_base64']:
                    self.failed = True
    def execute(self):
        try:
            version = self.command('compiler-version', [self.args.cxx, '--version'])
            target = self.command('compiler-target', [self.args.cxx, '-dumpmachine'])
            if okay(version) and okay(target):
                model = self.args.root/HERE/'src/mmr_pool.cpp'
                test = self.args.root/HERE/'tests/mmr_contract.cpp'
                modes = MODES if self.args.mode == 'all' else {self.args.mode: MODES[self.args.mode]}
                for name, flags in modes.items(): self.build(name, test, flags)
                self.build('retained17', test, ['-O2'], 'retained')
                if self.args.negative_controls:
                    directory = self.output/'mutations'; directory.mkdir()
                    for name, content, case in mutations(model.read_text(encoding='utf-8')):
                        source = directory/(name+'.cpp'); write_new(source, content)
                        self.build(name, test, ['-O2','-DNDEBUG'], 'mutant', case, source)
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
            model_boundary='Fixed complete snapshot, serial admission, whole visible party in same lobby, reciprocal anchor window. No all-pair/co-team/OOM transaction/concurrency/optimality or performance claim')
        try:
            write_new(self.output/'provenance.json', json.dumps(provenance, indent=2)+'\n')
            write_new(self.output/'summary.json', json.dumps(dict(failed=self.failed, commands=self.journal.count, outcomes=self.outcomes), indent=2)+'\n')
        except OSError as error:
            self.failed = True; print('FINAL_LOG_FAILURE '+str(error), file=sys.stderr, flush=True)
        print('DRIVER_RESULT '+('FAIL' if self.failed else 'PASS'), flush=True)
        return int(self.failed)


def fixture_report(mode, template):
    lines=template.splitlines(); result=lines[-1]
    first='solo.0'; end=f'CASE {first} checks=14 fail=0'
    if mode=='empty': return ''
    if mode in ('nan-mode', 'duplicate-mode', 'mismatched-mode', 'boolean-mode', 'infinite-number'):
        index = next(i for i, line in enumerate(lines) if line.startswith('OBS '))
        replacements = {'nan-mode': ('"mode":0', '"mode":NaN'),
                        'duplicate-mode': ('"mode":0', '"mode":0,"mode":1'),
                        'mismatched-mode': ('"mode":0', '"mode":2'),
                        'boolean-mode': ('"mode":0', '"mode":false'),
                        'infinite-number': ('1000', '1e999')}
        old, new = replacements[mode]
        if old not in lines[index]: raise ValueError('missing strict JSON fixture site')
        lines[index] = lines[index].replace(old, new, 1)

    if mode=='missing-observation': lines=[line for line in lines if not line.startswith('OBS ')][:] # claims remain green, evidence missing
    if mode=='duplicate-observation': lines.insert(1,next(line for line in lines if line.startswith('OBS ')))
    if mode=='malformed-data': lines.insert(1,'OBS {"case":"solo.0"}')
    if mode=='missing-check': lines.remove('CHECK solo.0 1 PASS')
    if mode=='missing-case': lines.remove(end)
    if mode=='missing-result': lines.pop()
    if mode=='duplicate-check': lines.insert(1,'CHECK solo.0 1 PASS')
    if mode=='duplicate-case': lines.insert(1,end)
    if mode=='duplicate-result': lines.append(result)
    if mode=='wrong-version': lines[0]='MMR_CONTRACT version=2'
    if mode=='malformed': lines[-1]='RESULT version=1 cases=88 checks=bad fail=0'
    if mode=='fail-zero':
        lines[lines.index('CHECK solo.0 1 PASS')]='CHECK solo.0 1 FAIL'
        lines[lines.index(end)]=end[:-1]+'1'; lines[-1]=result[:-1]+'1'
    return '\n'.join(lines)+'\n'


FIXTURE = r'''#!PYTHON
import json, os, pathlib, sys
mode=os.environ['MMR_FIXTURE_MODE']
events=pathlib.Path(os.environ['MMR_FIXTURE_EVENTS'])
def event(kind,name):
    with events.open('a') as f: f.write(json.dumps([kind,name])+"\n")
if sys.argv[1:]==['--version']:
    print('synthetic mmr fixture compiler'); sys.exit(19 if mode=='compiler-fail' else 0)
if sys.argv[1:]==['-dumpmachine']: print('synthetic-target'); sys.exit(0)
out=pathlib.Path(sys.argv[sys.argv.index('-o')+1]); name=out.stem
event('compile',name)
code='#!'+sys.executable+'\nimport sys,os,time,json\n'
code+='with open('+repr(str(events))+',"a") as f: f.write(json.dumps(["run",'+repr(name)+'])+"\\n")\n'
if name=='retained17':
    code+='print("\\n".join("PASS  M%d retained synthetic case"%i for i in range(1,18)))\nprint("RESULT pass=17 fail=0")\n'
else:
    code+='os.write(1,'+repr(pathlib.Path(os.environ['MMR_FIXTURE_REPORT']).read_bytes())+')\n'
    if mode=='binary-output': code+='os.write(1,b"\\xff|\\\\xff\\r\\n\\x00\\xfe");os.write(2,b"\\xfe|\\\\xfe\\r\\n\\x00\\xff")\n'
    if mode=='stderr': code+='print("unexpected diagnostic",file=sys.stderr)\n'
    if mode=='timeout': code+='time.sleep(10)\n'
    if mode=='runtime-fail': code+='sys.exit(31)\n'
out.write_text(code); out.chmod(0o700)
if name=='o2':
    if mode=='compile-fail': sys.exit(23) # runnable stale trap must NEVER run
    if mode=='log-directory': (out.parent.parent/'summary.json').mkdir()
    if mode=='log-symlink': (out.parent.parent/'summary.json').symlink_to(os.environ['MMR_FIXTURE_SENTINEL'])
'''


def self_test(args, output):
    if os.name != 'posix': raise RuntimeError('self-test fixtures cover POSIX only; native Windows NOT_RUN')
    initial = {str(p): sha(args.root/p) for p in SOURCES}
    # A real passing template is required; synthetic compiler fixtures then test
    # the CLI/protocol without rerunning the native suite for each failure mode.
    template_binary=output/'selftest-template.exe'
    env=helpers.child_environment(args.cxx)
    journal = Journal(output/'commands.jsonl')
    compiled=run([args.cxx]+FLAGS+['-O0','-DNDEBUG',str(args.root/HERE/'tests/mmr_contract.cpp'),'-o',str(template_binary)],args.root,env,args.timeout_seconds,output)
    compiled['label']='selftest-template-compile'; journal.add(compiled)
    template_record=run([str(template_binary)],args.root,env,args.timeout_seconds,output) if okay(compiled) else None
    if template_record:
        template_record['label']='selftest-template-run'; journal.add(template_record)
    if template_record is None or semantics(template_record):
        journal.close(); raise RuntimeError('real self-test protocol template failed')
    template=template_record['stdout']
    fixture = output/'fixture repository 中文 空格'
    for relative in SOURCES:
        path = fixture/relative; path.parent.mkdir(parents=True, exist_ok=True); shutil.copyfile(args.root/relative, path)
    compiler = output/'fixture compiler 中文 空格'
    write_new(compiler, FIXTURE.replace('PYTHON', sys.executable, 1)); compiler.chmod(0o700)
    sentinel = output/'untouched sentinel'; write_new(sentinel, 'ORIGINAL\n'); digest = sha(sentinel)
    driver = fixture/HERE/'scripts/run_mmr_contract.py'
    checks, failures, outcomes = 0, [], []
    def require(condition, label):
        nonlocal checks
        checks += 1
        if not condition: failures.append(label); print('SELFTEST_FAIL '+label, flush=True)
    def invoke(name, mode='ok', destination=None, cxx=None, extra=None, limited=False, close_failure=False, runner=driver):
        destination = destination or output/(name+' output 中文')
        report = output/(name+' protocol.txt'); write_new(report, fixture_report(mode, template))
        events = output/(name+' events.jsonl')
        env = dict(os.environ, MMR_FIXTURE_MODE=mode, MMR_FIXTURE_EVENTS=str(events),
                   MMR_FIXTURE_REPORT=str(report), MMR_FIXTURE_SENTINEL=str(sentinel),
                   CXXFLAGS='SYNTHETIC_IGNORED_FLAG_VALUE')
        argv = [sys.executable, '-B']+(['-O'] if sys.flags.optimize else [])+[str(runner), '--root', str(fixture),
                '--cxx', str(cxx or compiler), '--mode', 'o2', '--timeout-seconds', '0.3', '--output-dir', str(destination)]+(extra or [])
        if close_failure:
            launcher = output/(name+' close launcher.py')
            write_new(launcher, 'import importlib.util,os,sys\n'+
                'sys.dont_write_bytecode=True\n'+
                'spec=importlib.util.spec_from_file_location("mmr_close_fixture",'+repr(str(runner))+')\n'+
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
             'duplicate-result', 'wrong-version', 'malformed', 'fail-zero', 'stderr', 'binary-output',
             'missing-observation', 'duplicate-observation', 'malformed-data',
             'nan-mode', 'duplicate-mode', 'mismatched-mode', 'boolean-mode', 'infinite-number']
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
        helper_copy=fixture/HELPER_REL
        hidden_helper=output/'temporarily hidden fixture helper.py'
        helper_copy.rename(hidden_helper)
        try:
            record,events,_=invoke('missing-helper')
            require(not okay(record) and not events and 'DRIVER_RESULT PASS' not in record['stdout'], 'missing helper refuses before compiler')
        finally: hidden_helper.rename(helper_copy)
        record, _, destination = invoke('selected-root-identity', runner=Path(__file__).resolve())
        require(okay(record), 'separate selected root success')
        provenance = json.loads((destination/'provenance.json').read_text())
        require(provenance['actual_executing_code_sha256'] == {str(p.resolve()): sha(p) for p in (Path(__file__), HELPER)}, 'actual executing code distinct from selected root')
        require(not (fixture/HERE/'build').exists() and not (fixture/HERE/'results').exists(), 'fixture repo output free')
        require(all(sha(args.root/p) == digest for p, digest in initial.items()), 'source/helper unchanged')
        journal.close() # close failures must precede any self-test success report
        write_new(output/'self-test-summary.json', json.dumps(dict(version=1, checks=checks, failures=failures, outcomes=outcomes,
                  python_optimize=sys.flags.optimize, source_sha256=initial, raw_stream='commands.jsonl',
                  coverage='POSIX real template + synthetic CLI fixtures; capture helper unchanged; no benchmark', ended_utc=utc()), indent=2)+'\n')
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

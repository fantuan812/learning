#!/usr/bin/env python3
"""Focused Skill-only bounded contracts, retained S1-S14 and genuine mutations.
--mode all: O0+NDEBUG, O2, UBSan. --self-test: synthetic POSIX CLI fixtures.
Always use a fresh external output directory. No other models or benchmarks run.
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
HELPER = Path(__file__).with_name('run_inventory_contract.py')
spec = importlib.util.spec_from_file_location('skill_capture_helpers', HELPER)
if spec is None or spec.loader is None:
    raise RuntimeError('cannot load read-only capture helper')
helpers = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helpers)
sha, write_new, fresh_output = helpers.sha, helpers.write_new, helpers.fresh_output
run, okay = helpers.run, helpers.okay
ROOT = Path(__file__).resolve().parents[4]
HERE = Path('evidence/tests/gameplay-core')
SOURCE_PATHS = [HERE/'src/skill_pipeline.cpp', HERE/'tests/skill_contract.cpp',
                HERE/'scripts/run_skill_contract.py', HERE/'scripts/run_inventory_contract.py']
MODES = {'o0-ndebug':['-O0','-DNDEBUG'], 'o2':['-O2'],
         'ubsan':['-O1','-g','-fsanitize=undefined','-fno-sanitize-recover=undefined']}
FLAGS = ['-std=c++17','-Wall','-Wextra','-Werror','-pedantic','-fno-fast-math','-fno-finite-math-only']
# Frozen versioned counts: (checks, ledger entries). Not learned from the tested binary.
MANIFEST = {'literal_progression':(11,6)}
MANIFEST.update(dict.fromkeys(('reject_'+s for s in
    'duplicate unknown dead busy gcd cooldown mana target nan posinf neginf near far'.split()),(6,3)))
MANIFEST.update(accepted_id_scope=(8,4), rejection_retry=(4,2), lower_id_arrives_later=(6,3))
MANIFEST.update(dict.fromkeys(('time_'+field+suffix for field in ('cooldown','cast','gcd')
    for suffix in ('_exact_max','_overflow')),(6,3)))
MANIFEST.update(unused_long_definition=(2,1),max_duration_cooldown=(2,1),max_duration_cast=(2,1))
MANIFEST.update(dict.fromkeys(('early_gate_before_time_'+s for s in 'duplicate unknown dead mana target distance range'.split()),(6,3)))
MANIFEST.update(dict.fromkeys(('timed_gate_before_time_'+s for s in ('busy','gcd','cooldown')),(6,3)))
MANIFEST.update(checked_add_edges=(6,0),full_state_and_intent=(24,3),bounded_stream_literals=(15,6),
                tutorial_prediction_copy=(6,2),tutorial_pending_counterexample=(3,0),tutorial_lastseq_counterexample=(4,0))
NOTES = {
 'tutorial_prediction_copy':'value-copy exercise only; S9 owns one independent int',
 'tutorial_pending_counterexample':'blind assignment 100 drops pending B; expected display 60; no replay implementation',
 'tutorial_lastseq_counterexample':'first-late and earlier-retry both get another request result; not DUT behavior'}
FIELDS = {'alive','mana','maxMana','gcdUntilMs','castingUntilMs','castingSkill','castingInterruptible',
          'cooldownEnd','applied','acceptedHash','accepted','rejected'}


def utc() -> str:
    return datetime.now(timezone.utc).isoformat()


def semantic_errors(record: dict, kind='contract', target=None) -> list[str]:
    errors=[]; lines=record['stdout'].splitlines(); mutant=kind.endswith('-mutant')
    if kind.startswith('retained'):
        statuses={}; results=[]
        for line in lines:
            if line.startswith(('PASS','FAIL')):
                m=re.fullmatch(r'(PASS|FAIL)  (S\d+) .*',line)
                if not m: errors.append('malformed retained check'); continue
                status,name=m.groups()
                if name in statuses: errors.append('duplicate retained check')
                statuses[name]=status
            if line.startswith('RESULT'):
                m=re.fullmatch(r'RESULT pass=(\d+) fail=(\d+)',line)
                if not m: errors.append('malformed retained result')
                else: results.append(tuple(map(int,m.groups())))
        if set(statuses)!={'S'+str(n) for n in range(1,15)}: errors.append('exact S1-S14 required')
        failed=sum(v=='FAIL' for v in statuses.values())
        if results!=[(14-failed,failed)]: errors.append('retained result/count mismatch')
        if mutant:
            targets=target if isinstance(target,list) else [target]
            if not failed or any(statuses.get(t)!='FAIL' for t in targets): errors.append('intended retained failures required')
        elif failed: errors.append('retained semantic failure')
    else:
        if lines.count('SKILL_CONTRACT version=1')!=1: errors.append('exactly one supported header required')
        checks={}; completions={}; ledgers={}; notes={}; results=[]
        for line in lines:
            if line=='SKILL_CONTRACT version=1': continue
            m=re.fullmatch(r'CHECK (\S+) ([1-9]\d*) (PASS|FAIL)',line)
            if m:
                name,n,status=m.groups();key=(name,int(n))
                if key in checks: errors.append('duplicate check')
                checks[key]=status;continue
            m=re.fullmatch(r'CASE (\S+) checks=(\d+) fail=(\d+) ledger=(\d+)',line)
            if m:
                name,n,f,l=m.groups()
                if name in completions:errors.append('duplicate case')
                completions[name]=(int(n),int(f),int(l));continue
            m=re.fullmatch(r'LEDGER (\S+) ([1-9]\d*) (.+)',line)
            if m:
                name,n,payload=m.groups();key=(name,int(n))
                if key in ledgers: errors.append('duplicate ledger')
                try:
                    entry=json.loads(payload)
                    if entry['operation'] not in ('Handle','Interrupt'): raise ValueError('operation')
                    for state in ('before','expected','actual'):
                        if set(entry[state])!=FIELDS: raise ValueError('complete Actor required')
                    if not mutant and entry['expected']!=entry['actual']:
                        errors.append('normal ledger expected/actual state contradiction')
                    if entry['operation']=='Handle':
                        if set(entry['input'])!={'requestId','skillId','targetId','nowMs','distance_binary64_bits'}: raise ValueError('input')
                        if not isinstance(entry['expected_reason'],str) or not isinstance(entry['actual_reason'],str): raise ValueError('reason')
                        if 'selected_definition' not in entry: raise ValueError('definition')
                        if not mutant and entry['expected_reason']!=entry['actual_reason']:
                            errors.append('normal ledger expected/actual reason contradiction')
                    ledgers[key]=entry
                except (ValueError,KeyError,TypeError): errors.append('malformed/incomplete ledger')
                continue
            m=re.fullmatch(r'NOTE (\S+) (.*)',line)
            if m:
                name,note=m.groups()
                if name in notes:errors.append('duplicate note')
                notes[name]=note;continue
            m=re.fullmatch(r'RESULT version=1 cases=(\d+) checks=(\d+) fail=(\d+)',line)
            if m:results.append(tuple(map(int,m.groups())));continue
            errors.append('unknown/malformed protocol line: '+line[:80])
        expected_checks={(name,n) for name,(count,_) in MANIFEST.items() for n in range(1,count+1)}
        expected_ledger={(name,n) for name,(_,count) in MANIFEST.items() for n in range(1,count+1)}
        if set(checks)!=expected_checks:errors.append('exact check manifest mismatch')
        if set(ledgers)!=expected_ledger:errors.append('exact ledger manifest mismatch')
        if set(completions)!=set(MANIFEST):errors.append('exact case manifest mismatch')
        if notes!=NOTES:errors.append('tutorial labels missing/changed')
        failed=sum(v=='FAIL' for v in checks.values())
        for name,(count,ledger) in MANIFEST.items():
            case_fail=sum(v=='FAIL' for (n,_),v in checks.items() if n==name)
            if completions.get(name)!=(count,case_fail,ledger):errors.append('case counters inconsistent: '+name)
        if results!=[(len(MANIFEST),sum(x[0] for x in MANIFEST.values()),failed)]:errors.append('exact single result required')
        if mutant:
            if not failed or not any(n==target and v=='FAIL' for (n,_),v in checks.items()):errors.append('intended mutant case must fail')
        elif failed:errors.append('unexpected semantic FAIL')
    if record['exit_code']!=(1 if mutant else 0):errors.append('native exit mismatch')
    if record['timed_out'] or not record['capture_complete']:errors.append('timeout/incomplete capture')
    if record['stderr_bytes']:errors.append('unexpected stderr/sanitizer diagnostic')
    return errors


def mutations(model: str, test: str):
    def replace(text,old,new):
        if text.count(old)!=1:raise ValueError('mutation site not unique: '+old)
        return text.replace(old,new)
    gcd=replace(model,'if (r.nowMs < a.gcdUntilMs) { ++a.rejected; return "gcd"; }',
                'if (r.nowMs < a.gcdUntilMs) { --a.mana; ++a.rejected; return "gcd"; }')
    finite=replace(model,'if (!std::isfinite(r.targetDistance)) { ++a.rejected; return "invalid-distance"; }','/* finite guard removed */')
    scalar=replace(model,'if (r != "accepted") predictedMana = server.mana;', '(void)predictedMana;')
    alias=replace(test,'Actor prediction=authority; // MUTATION_SITE prediction_copy','Actor& prediction=authority; // MUTATION_SITE prediction_copy')
    saturation=replace(model,'if (delta > 0 && base > std::numeric_limits<int64_t>::max() - delta) return false;',
        'if (delta > 0 && base > std::numeric_limits<int64_t>::max() - delta) { candidate = std::numeric_limits<int64_t>::max(); return true; }')
    return [('gcd-spends-mana',gcd,test,'contract-mutant','reject_gcd'),
            ('finite-removed',finite,test,'retained-mutant',['S11','S12','S13']),
            ('scalar-correction-removed',scalar,test,'retained-mutant','S9'),
            ('prediction-alias',model,alias,'contract-mutant','tutorial_prediction_copy'),
            ('time-saturates',saturation,test,'contract-mutant','time_cooldown_overflow')]


class Driver:
    def __init__(self,args,output):
        self.args,self.output=args,output;self.failed=False;self.records=[];self.started=utc()
        self.env=helpers.child_environment(args.cxx)
        self.sources={str(p):sha(args.root/p) for p in SOURCE_PATHS}
        self.executing={str(p.resolve()):sha(p) for p in (Path(__file__),HELPER)}
        (output/'build').mkdir();(output/'logs').mkdir()
    def unchanged(self):
        return all(sha(self.args.root/p)==h for p,h in self.sources.items()) and all(sha(Path(p))==h for p,h in self.executing.items())
    def command(self,label,argv,expected=0):
        if not self.unchanged():raise RuntimeError('source/runner/helper changed during run')
        began=utc();r=run(argv,self.args.root,self.env,self.args.timeout_seconds,self.output)
        r.update(label=label,expected_exit=expected,utc_start=began,utc_end=utc());self.records.append(r)
        if expected is not None and (r['exit_code']!=expected or r['timed_out'] or not r['capture_complete']):self.failed=True
        return r
    def log(self,name,records,metadata=None):
        # One canonical raw record copy in commands.json; these index logs do not
        # recursively embed stdout/base64 or the complete command collection.
        try:
            write_new(self.output/'logs'/(name+'.json'),json.dumps({'labels':[r['label'] for r in records],
                'records_file':'../commands.json','metadata':metadata or {}},indent=2)+'\n')
        except OSError as e:self.failed=True;print('LOG_FAILURE '+name+': '+str(e),file=sys.stderr)
    def build_run(self,name,source,flags,kind='contract',target=None,model=None):
        binary=self.output/'build'/(name+'.exe');definitions=[]
        if model is not None and not kind.startswith('retained'):definitions=[f'-DSKILL_SOURCE="{model}"']
        records=[self.command(name+'-compile',[self.args.cxx]+FLAGS+flags+definitions+[str(source),'-o',str(binary)])]
        if okay(records[0]):
            r=self.command(name+'-run',[str(binary)],1 if kind.endswith('-mutant') else 0);records.append(r)
            r['semantic_errors']=semantic_errors(r,kind,target);r['semantic_valid']=not r['semantic_errors']
            if r['semantic_errors']:self.failed=True
        self.log(name,records,{'kind':kind,'target':target,'translation_unit':str(source),'translation_unit_sha256':sha(source),
            'included_model_sha256':sha(model or self.args.root/HERE/'src/skill_pipeline.cpp'),
            'binary_sha256':sha(binary) if binary.is_file() and okay(records[0]) else None})
    def execute(self):
        probes=[self.command('compiler-version',[self.args.cxx,'--version']),self.command('compiler-target',[self.args.cxx,'-dumpmachine'])]
        self.log('compiler',probes)
        if not all(okay(r) for r in probes):return self.finish()
        model=self.args.root/HERE/'src/skill_pipeline.cpp';test=self.args.root/HERE/'tests/skill_contract.cpp'
        for mode,flags in MODES.items():
            if self.args.mode not in ('all',mode):continue
            self.build_run('retained-'+mode,model,flags,'retained')
            self.build_run('contract-'+mode,test,flags)
        if self.args.negative_controls:
            directory=self.output/'mutations';directory.mkdir()
            for name,changed_model,changed_test,kind,target in mutations(model.read_text(),test.read_text()):
                mutant=directory/(name+'-model.cpp');write_new(mutant,changed_model)
                if changed_test==test.read_text():contract=test
                else:contract=directory/(name+'-contract.cpp');write_new(contract,changed_test)
                self.build_run(name,mutant if kind.startswith('retained') else contract,['-O2'],kind,target,mutant)
                if name=='gcd-spends-mana':self.build_run('gcd-old-suite-misses',mutant,['-O2'],'retained',model=mutant)
        return self.finish()
    def finish(self):
        if not self.unchanged():self.failed=True
        provenance={'version':1,'utc_start':self.started,'utc_end':utc(),'source_sha256':self.sources,
            'actual_executing_code_sha256':self.executing,'root':str(self.args.root),'python':sys.version,
            'python_optimize':sys.flags.optimize,'platform':sys.platform,'mode':self.args.mode,
            'negative_controls':self.args.negative_controls,'flags':FLAGS,'manifest':MANIFEST,
            'ignored_flag_variables_set':{k:k in os.environ for k in ('CXXFLAGS','CPPFLAGS','LDFLAGS')},
            'capture_boundary':'finite regular-file snapshots; escaped descendants not guaranteed captured/terminated',
            'scope':'single-writer synthetic Skill only; no allocation-fault guarantee, UE, network, authentication or benchmark'}
        try:
            write_new(self.output/'commands.json',json.dumps(self.records,indent=2,ensure_ascii=True)+'\n')
            write_new(self.output/'provenance.json',json.dumps(provenance,indent=2)+'\n')
            write_new(self.output/'summary.json',json.dumps({'failed':self.failed,'commands':len(self.records),
                'semantic_runs':[{'label':r['label'],'valid':r['semantic_valid'],'errors':r['semantic_errors']} for r in self.records if 'semantic_valid' in r]},indent=2)+'\n')
        except OSError as e:self.failed=True;print('FINAL_LOG_FAILURE '+str(e),file=sys.stderr)
        print('DRIVER_RESULT '+('FAIL' if self.failed else 'PASS'),flush=True);return int(self.failed)


def fixture_report(mode):
    # A CLI fixture report is synthetic protocol input, never model evidence.
    state={k:[] if k in ('cooldownEnd','applied') else 0 for k in FIELDS}
    entry={'operation':'Interrupt','before':state,'expected':state,'actual':state}
    lines=['SKILL_CONTRACT version=1']
    for name,(count,ledger) in MANIFEST.items():
        lines += [f'CHECK {name} {n} PASS' for n in range(1,count+1)]
        lines += [f'LEDGER {name} {n} '+json.dumps(entry) for n in range(1,ledger+1)]
        lines.append(f'CASE {name} checks={count} fail=0 ledger={ledger}')
    lines += ['NOTE '+name+' '+note for name,note in NOTES.items()]
    result=f'RESULT version=1 cases={len(MANIFEST)} checks={sum(x[0] for x in MANIFEST.values())} fail=0';lines.append(result)
    if mode in ('contradictory-state','contradictory-reason'):
        at=next(i for i,line in enumerate(lines) if line.startswith('LEDGER '))
        prefix=lines[at].split(' ',3)[:3];bad=json.loads(lines[at].split(' ',3)[3])
        if mode=='contradictory-state':bad['actual']['mana']=999
        else:
            bad.update(operation='Handle',input={'requestId':1,'skillId':1,'targetId':9,'nowMs':0,'distance_binary64_bits':0},
                selected_definition=None,expected_reason='accepted',actual_reason='duplicate')
        lines[at]=' '.join(prefix)+' '+json.dumps(bad)
    if mode=='empty':return ''
    if mode=='low-check':return 'SKILL_CONTRACT version=1\nCHECK literal_progression 1 PASS\nRESULT version=1 cases=1 checks=1 fail=0\n'
    if mode=='missing-check':lines.remove('CHECK literal_progression 1 PASS')
    if mode=='missing-case':lines.remove('CASE literal_progression checks=11 fail=0 ledger=6')
    if mode=='missing-ledger':lines=[x for x in lines if not x.startswith('LEDGER literal_progression 1 ')]
    if mode=='missing-result':lines.pop()
    if mode=='duplicate-result':lines.append(result)
    if mode=='duplicate-check':lines.insert(1,'CHECK literal_progression 1 PASS')
    if mode=='duplicate-case':lines.insert(1,'CASE literal_progression checks=11 fail=0 ledger=6')
    if mode=='duplicate-ledger':lines.insert(1,'LEDGER literal_progression 1 '+json.dumps(entry))
    if mode=='missing-note':lines=[x for x in lines if not x.startswith('NOTE tutorial_prediction_copy ')]
    if mode=='malformed':lines[-1]='RESULT version=1 cases=42 checks=bad fail=0'
    if mode=='wrong-version':lines[0]='SKILL_CONTRACT version=2'
    if mode=='failure-exit-zero':lines[1]='CHECK literal_progression 1 FAIL'
    if mode=='fail-marker':lines.append('FAIL unrelated')
    if mode=='bad-ledger':lines=[x.replace('"alive": 0','"omittedAlive": 0') if x.startswith('LEDGER') else x for x in lines]
    return '\n'.join(lines)+'\n'


FIXTURE_COMPILER=r'''#!PYTHON
import json,os,pathlib,sys,time
mode=os.environ['SKILL_FIXTURE_MODE'];events=pathlib.Path(os.environ['SKILL_FIXTURE_EVENTS'])
def event(kind,name):
    with events.open('a') as f:f.write(json.dumps([kind,name])+"\n")
if sys.argv[1:]==['--version']:print('synthetic Skill compiler');sys.exit(19 if mode=='compiler-fail' else 0)
if sys.argv[1:]==['-dumpmachine']:print('synthetic-target');sys.exit(0)
output=pathlib.Path(sys.argv[sys.argv.index('-o')+1]);name=output.stem;event('compile',name)
if mode=='compile-timeout' and name=='contract-o2':time.sleep(10)
code='#!'+sys.executable+'\nimport os,json,sys,time\n'
code+='with open('+repr(str(events))+',"a") as f:f.write(json.dumps(["run",'+repr(name)+'])+"\\n")\n'
if name.startswith('retained'):
    report='\n'.join(['PASS  S'+str(n)+' synthetic retained fixture' for n in range(1,15)]+['RESULT pass=14 fail=0'])+'\n'
else:report=pathlib.Path(os.environ['SKILL_FIXTURE_REPORT']).read_text()
code+='os.write(1,'+repr(report.encode())+')\n'
if name=='contract-o2':
    if mode=='binary-output':code+='os.write(1,b"\\xff|\\\\xff\\r\\n\\x00\\xfe");os.write(2,b"\\xfe|\\\\xfe\\r\\n\\x00\\xff")\n'
    if mode=='timeout':code+='time.sleep(10)\n'
    if mode=='runtime-fail':code+='sys.exit(31)\n'
output.write_text(code);output.chmod(0o700)
if name=='contract-o2':
    if mode=='compile-fail':sys.exit(23)
    if mode in ('log-directory','log-symlink'):
        log=output.parent.parent/'logs/contract-o2.json'
        if mode=='log-directory':log.mkdir()
        else:log.symlink_to(os.environ['SKILL_FIXTURE_SENTINEL'])
'''


def self_test(args,output):
    if os.name!='posix':raise RuntimeError('self-test fixtures cover POSIX only')
    started=utc();initial={str(p):sha(args.root/p) for p in SOURCE_PATHS}
    fixture=output/'fixture repository 空格'
    for relative in SOURCE_PATHS:
        path=fixture/relative;path.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(args.root/relative,path)
    compiler=output/'fixture compiler 空格';write_new(compiler,FIXTURE_COMPILER.replace('PYTHON',sys.executable,1));compiler.chmod(0o700)
    sentinel=output/'original sentinel';write_new(sentinel,'DO NOT OVERWRITE\n');original=sha(sentinel)
    driver=fixture/HERE/'scripts/run_skill_contract.py';checks=0;failures=[];records=[]
    def require(condition,message):
        nonlocal checks
        checks+=1
        if not condition:failures.append(message);print('SELFTEST_FAIL '+message,flush=True)
    def invoke(name,mode='ok',destination=None,extra=None,limited=False,runner=driver,optimize=True):
        destination=destination or output/(name+' output');report=output/(name+' report.txt');write_new(report,fixture_report(mode))
        events=output/(name+' events.jsonl')
        env=dict(os.environ,CXXFLAGS='SKILL_SYNTHETIC_UNUSED_VALUE',SKILL_FIXTURE_MODE=mode,SKILL_FIXTURE_EVENTS=str(events),
            SKILL_FIXTURE_REPORT=str(report),SKILL_FIXTURE_SENTINEL=str(sentinel))
        command=[sys.executable]+(['-O'] if optimize else [])+['-B',str(runner),'--root',str(fixture),'--cxx',str(compiler),'--mode','o2',
                 '--timeout-seconds','0.3','--output-dir',str(destination)]+(extra or [])
        if limited:
            limiter=output/(name+' limiter.py');write_new(limiter,'import os,resource,sys\nresource.setrlimit(resource.RLIMIT_FSIZE,(131072,131072))\nos.execv(sys.executable,[sys.executable]+sys.argv[1:])\n')
            command=[sys.executable,str(limiter)]+command[1:]
        began=utc();r=run(command,fixture,env,20,output);r.update(label=name,fixture=True,utc_start=began,utc_end=utc());records.append(r)
        rows=[json.loads(line) for line in events.read_text().splitlines()] if events.exists() else []
        return r,rows,destination
    modes=['ok','compiler-fail','compile-fail','compile-timeout','runtime-fail','timeout','log-directory','log-symlink',
        'empty','low-check','missing-check','missing-case','missing-ledger','missing-result','duplicate-result','duplicate-check',
        'duplicate-case','duplicate-ledger','missing-note','contradictory-state','contradictory-reason','malformed','wrong-version','failure-exit-zero','fail-marker','bad-ledger','binary-output']
    for mode in modes:
        r,events,destination=invoke('cli-'+mode,mode)
        require(okay(r)==(mode=='ok'),mode+' native CLI outcome')
        require(('DRIVER_RESULT PASS' in r['stdout'])==(mode=='ok'),mode+' no false PASS tail')
        require(sha(sentinel)==original,mode+' sentinel preserved')
        path=destination/'commands.json';entries=json.loads(path.read_text()) if path.exists() else [];by={x['label']:x for x in entries}
        if mode=='ok':
            require(by['contract-o2-run']['semantic_valid'] and by['retained-o2-run']['semantic_valid'],'complete protocols')
            provenance=(destination/'provenance.json').read_text();p=json.loads(provenance)
            require(p['python_optimize']==1,'Python -O actually active')
            require('SKILL_SYNTHETIC_UNUSED_VALUE' not in provenance and p['ignored_flag_variables_set']['CXXFLAGS'],'ignored flag presence only')
            require(all('--benchmark' not in x['argv'] for x in entries),'no benchmark')
        elif mode=='compiler-fail':require(not events,'failed compiler probe prevents build')
        elif mode in ('compile-fail','compile-timeout'):
            require('contract-o2-run' not in by and ['run','contract-o2'] not in events,'failed compile never runs stale trap')
            require(by['contract-o2-compile']['timed_out'] if mode=='compile-timeout' else by['contract-o2-compile']['exit_code']==23,'actual compile failure captured')
        elif mode=='runtime-fail':require(by['contract-o2-run']['exit_code']==31,'actual runtime exit captured')
        elif mode=='timeout':require(by['contract-o2-run']['timed_out'] and not by['contract-o2-run']['capture_complete'] and r['elapsed_seconds']<10,'bounded incomplete timeout')
        elif mode in ('log-directory','log-symlink'):require('LOG_FAILURE contract-o2' in r['stderr'],'actual log open failure')
        else:require(by['contract-o2-run']['exit_code']==0 and not by['contract-o2-run']['semantic_valid'],'bad protocol exit0 rejected')
        if mode=='binary-output':
            for channel,tail in [('stdout',b'\xff|\\xff\r\n\x00\xfe'),('stderr',b'\xfe|\\xfe\r\n\x00\xff')]:
                entry=by['contract-o2-run'];raw=base64.b64decode(entry[channel+'_base64'],validate=True)
                require(raw.endswith(tail) and len(raw)==entry[channel+'_bytes'],channel+' lossless binary capture')
    for mode in ('contradictory-state','contradictory-reason'):
        r,_,destination=invoke('normal-python-'+mode,mode,optimize=False)
        require(not okay(r) and 'DRIVER_RESULT PASS' not in r['stdout'],mode+' normal Python rejects contradiction')
        entries=json.loads((destination/'commands.json').read_text());child=next(x for x in entries if x['label']=='contract-o2-run')
        require(child['exit_code']==0 and not child['semantic_valid'],mode+' normal Python actual exit0 contradiction')
        require(json.loads((destination/'provenance.json').read_text())['python_optimize']==0,mode+' normal Python actually active')
    r,_,destination=invoke('selected-root-provenance',runner=Path(__file__).resolve())
    require(okay(r),'selected alternate root supported')
    require(json.loads((destination/'provenance.json').read_text())['actual_executing_code_sha256']==
        {str(p.resolve()):sha(p) for p in (Path(__file__),HELPER)},'actual runner/helper identity separate')
    r,_,_=invoke('real-log-write-failure',limited=True)
    require(not okay(r) and 'DRIVER_RESULT PASS' not in r['stdout'],'real OS log-write failure nonzero')
    require('FINAL_LOG_FAILURE' in r['stderr'] or 'LOG_FAILURE' in r['stderr'],'real write failure diagnostic')
    for kind in ('directory','file','symlink','dangling-symlink','missing-parent','repo-path','repo-alias'):
        destination=output/('refuse-'+kind)
        if kind=='directory':destination.mkdir();write_new(destination/'keep','keep')
        elif kind=='file':write_new(destination,'keep')
        elif kind=='symlink':destination.symlink_to(sentinel)
        elif kind=='dangling-symlink':destination.symlink_to(output/'absent')
        elif kind=='missing-parent':destination=destination/'missing'/'out'
        elif kind=='repo-path':destination=fixture/'forbidden-output'
        elif kind=='repo-alias':
            alias=output/'repo alias';alias.symlink_to(fixture,target_is_directory=True);destination=alias/'forbidden-output'
        r,events,_=invoke('protect-'+kind,destination=destination)
        require(not okay(r) and not events,kind+' rejected before compiler');require(sha(sentinel)==original,kind+' original intact')
        if kind=='directory':require((destination/'keep').read_text()=='keep','existing directory intact')
        if kind=='file':require(destination.read_text()=='keep','existing file intact')
    for name,extra in [('unknown-arg',['--invalid']),('nan-timeout',['--timeout-seconds','nan']),('inf-timeout',['--timeout-seconds','inf'])]:
        r,events,_=invoke(name,extra=extra);require(not okay(r) and not events,name+' rejected')
    require(not (fixture/HERE/'build').exists() and not (fixture/HERE/'results').exists(),'fixture repository remains output-free')
    require(all(sha(args.root/p)==h for p,h in initial.items()),'source/helper stable')
    # CLI command raw is stored ONCE here. Child command raw stays in each child's
    # commands.json. No nested full-log consolidation or binary archival required.
    write_new(output/'self-test.json',json.dumps({'checks':checks,'failures':failures,'records':records,'source_sha256':initial,
        'python_optimize':sys.flags.optimize,'utc_start':started,'utc_end':utc(),'scope':'synthetic POSIX CLI only'},indent=2)+'\n')
    print(f'SELFTEST_RESULT version=1 checks={checks} fail={len(failures)}');return int(bool(failures))


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output-dir',type=Path,required=True);p.add_argument('--root',type=Path,default=ROOT)
    p.add_argument('--cxx',default=os.environ.get('CXX','g++'));p.add_argument('--mode',choices=['all',*MODES],default='all')
    p.add_argument('--negative-controls',action='store_true');p.add_argument('--self-test',action='store_true')
    p.add_argument('--timeout-seconds',type=float,default=30);args=p.parse_args();args.root=args.root.resolve()
    if not math.isfinite(args.timeout_seconds) or not 0<args.timeout_seconds<=600:p.error('timeout must be finite and in (0,600]')
    try:
        output=fresh_output(args.output_dir,[ROOT,args.root])
        return self_test(args,output) if args.self_test else Driver(args,output).execute()
    except (OSError,ValueError,RuntimeError) as e:
        print('DRIVER_ERROR '+type(e).__name__+': '+str(e),file=sys.stderr,flush=True);return 1

if __name__=='__main__':sys.exit(main())

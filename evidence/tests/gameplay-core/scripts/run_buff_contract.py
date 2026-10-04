#!/usr/bin/env python3
"""Focused Buff-only contracts, retained 12 scenarios and real source mutants.
Default --mode all: O0+NDEBUG, O2, UBSan; mutants use O2 and explicit FAIL checks.
--self-test: isolated fake compilers and bounded process fixtures, no other model.
All outputs require a NEW directory outside the repository, even through aliases.
"""
from __future__ import annotations
import argparse
import base64
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import re
import shutil
import signal
import sys
import time

# Read-only import of the already-audited generic capture/path helpers. Prevent
# __pycache__ writes, and never call its Inventory Driver, suite or self_test.
sys.dont_write_bytecode = True
HELPER = Path(__file__).with_name('run_inventory_contract.py')
spec = importlib.util.spec_from_file_location('buff_capture_helpers', HELPER)
if spec is None or spec.loader is None:
    raise RuntimeError('cannot load required read-only capture helper')
helpers = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helpers)
sha, write_new, fresh_output = helpers.sha, helpers.write_new, helpers.fresh_output
run, okay, format_record, child_environment = helpers.run, helpers.okay, helpers.format_record, helpers.child_environment
ROOT = Path(__file__).resolve().parents[4]
HERE = Path('evidence/tests/gameplay-core')
MODES = {'o0-ndebug':['-O0','-DNDEBUG'], 'o2':['-O2'],
         'ubsan':['-O1','-g','-fsanitize=undefined','-fno-sanitize-recover=undefined']}
CASES = ('refresh_same_id refresh_source_rebind suppressed_stack_cap three_tier_recovery '
         'rank_order_1 rank_order_2 rank_order_3 rank_order_4 rank_order_5 rank_order_6 '
         'same_rank_replace lower_rank_replace_blocked continue_time_overshoot time_partition '
         'simultaneous_expiry suppressed_self_expiry dispel_all_matching dispel_highest_immediate '
         'directional_exclusion exclusion_removes_suppressed rejected_no_exclusion '
         'ordinary_refresh_exclusion suppressed_refresh_exclusion periodic_preserves_stacks '
         'periodic_at_cap periodic_suppressed_child periodic_nonstack_child periodic_missing_child '
         'periodic_existing_no_exclusion periodic_missing_exclusion rejected_unknown_zero '
         'periodic_metadata_only zero_tick_stable').split()
EXPECTED_CHECKS = 323
SOURCE_PATHS = [HERE/'src/buff_conflict.cpp', HERE/'tests/buff_contract.cpp',
                HERE/'scripts/run_buff_contract.py', HERE/'scripts/run_inventory_contract.py']


def semantic_errors(record: dict, kind: str, target: str | None = None) -> list[str]:
    text = record['stdout']; errors=[]
    summaries = re.findall(r'^RESULT(?:\s.*)?$', text, re.MULTILINE)
    fail_lines = re.findall(r'^FAIL(?:\s.*)?$', text, re.MULTILINE)
    if len(summaries)!=1: errors.append('exactly one RESULT line required, including malformed candidates')
    if kind == 'retained':
        if summaries != ['RESULT pass=12 fail=0 rules=R1-R9+E1-E3']: errors.append('retained 12 summary mismatch')
        names=re.findall(r'^PASS  ([RE]\d+) ',text,re.MULTILINE)
        if sorted(names)!=sorted(['R'+str(n) for n in range(1,10)]+['E1','E2','E3']): errors.append('retained cases missing/duplicate')
        if fail_lines: errors.append('retained FAIL marker')
    else:
        found=re.findall(r'^RESULT cases=(\d+) checks=(\d+) fail=(\d+) \(explicit checks remain active with NDEBUG\)$',text,re.MULTILINE)
        if len(found)!=1: errors.append('complete numeric contract summary required')
        else:
            cases, checks, fails=map(int,found[0])
            if cases!=len(CASES) or checks!=EXPECTED_CHECKS: errors.append('exact complete versioned contract counts required')
            if fails!=len(fail_lines): errors.append('FAIL marker count differs from summary')
            if (fails>0)!=(kind=='mutant'): errors.append('semantic failure count contradicts expected outcome')
        completed=re.findall(r'^(?:PASS_CASE|FAIL_CASE) (\S+)$',text,re.MULTILINE)
        if sorted(completed)!=sorted(CASES): errors.append('missing/duplicate/unknown semantic case')
        failed_cases=re.findall(r'^FAIL_CASE (\S+)$',text,re.MULTILINE)
        if kind=='mutant' and (not fail_lines or target not in failed_cases): errors.append('mutant must explicitly fail its intended case')
        if kind!='mutant' and (fail_lines or failed_cases): errors.append('unexpected FAIL markers')
    if record['exit_code'] != (1 if kind=='mutant' else 0): errors.append('native exit contradicts expected result')
    if record['timed_out'] or not record['capture_complete']: errors.append('timeout/incomplete capture is not a semantic result')
    return errors


# Each mutation modifies real model logic in an external source copy; no injected
# test failure, compile error or crash counts as a killed mutant.
MUTATIONS = {
    'refresh-unsuppresses': ('RecomputeWinners(); // REFRESH_ARBITRATION', 'same->suppressed = false; // REFRESH_ARBITRATION', 'refresh_same_id'),
    'first-lower-only': ('RecomputeWinners(); // INSERT_ARBITRATION', '''for (ActiveBuff& old : active_) {
            const BuffDef* od = Def(old.id);
            if (old.id != id && od->group == d->group) { old.suppressed = true; break; }
        } // INSERT_ARBITRATION''', 'three_tier_recovery'),
    'periodic-extra-stack': ('// PERIODIC_PRESERVE_STACKS', 'if (d->stackable && a->stacks < d->maxStacks) ++a->stacks;', 'periodic_preserves_stacks'),
    'first-survivor-restores': ('RecomputeWinners(); // EXPIRY_ARBITRATION', '''for (ActiveBuff& a : active_) {
            const BuffDef* d = Def(a.id); bool winner = false;
            for (const ActiveBuff& b : active_) {
                if (!b.suppressed && Def(b.id)->group == d->group) winner = true;
            }
            if (!winner) a.suppressed = false;
        } // EXPIRY_ARBITRATION''', 'three_tier_recovery'),
    'dispel-defers-recovery': ('RecomputeWinners(); // DISPEL_ARBITRATION: visible before return', '// DISPEL_ARBITRATION deliberately omitted', 'dispel_highest_immediate'),
    'reject-still-excludes': ('// Admission is decided using the whole group, before on-apply deletion.',
        'for (int g : d->excludesGroups) RemoveGroup(g);\n        // Admission is decided using the whole group, before on-apply deletion.', 'rejected_no_exclusion'),
    'suppressed-time-freezes': ('for (ActiveBuff& a : active_) a.remaining -= dt; // CONTINUE_TIME',
        'for (ActiveBuff& a : active_) if (!a.suppressed) a.remaining -= dt; // CONTINUE_TIME', 'continue_time_overshoot'),
    'periodic-repeats-exclusion': ('// PERIODIC_PRESERVE_STACKS',
        'for (int g : d->excludesGroups) RemoveGroup(g); // PERIODIC_PRESERVE_STACKS', 'periodic_existing_no_exclusion')
}


class Driver:
    def __init__(self,args:argparse.Namespace,output:Path):
        self.args,self.output=args,output;self.failed=False;self.records=[]
        self.env=child_environment(args.cxx)
        self.sources={str(p):sha(args.root/p) for p in SOURCE_PATHS}
        (output/'build').mkdir();(output/'logs').mkdir()
    def command(self,label:str,argv:list[str])->dict:
        r=run(argv,self.args.root,self.env,self.args.timeout_seconds,self.output)
        r['label']=label;self.records.append(r);return r
    def log(self,label:str,records:list[dict],extra:dict|None=None)->None:
        try:
            write_new(self.output/'logs'/(label+'.txt'),'SOURCE_SHA256 '+json.dumps(self.sources)+'\nMETADATA '+json.dumps(extra or {})+'\n'+''.join(format_record(r)for r in records))
            print('wrote '+str(self.output/'logs'/(label+'.txt')),flush=True)
        except OSError as e:
            self.failed=True;print('LOG_FAILURE '+label+': '+str(e),file=sys.stderr,flush=True)
    def finish(self)->int:
        try:
            unchanged=all(sha(self.args.root/p)==self.sources[str(p)] for p in SOURCE_PATHS)
            if not unchanged:self.failed=True
            provenance={'source_sha256':self.sources,'root':str(self.args.root),'python':sys.version,'platform':sys.platform,
                'mode':self.args.mode,'negative_controls':True,'failed':self.failed,'sources_unchanged':unchanged,
                'capture_helper_read_only':str(HELPER),'capture_helper_sha256':sha(HELPER),
                'capture_boundary':'finite regular-file snapshots; escaped descendants not guaranteed captured/terminated; snapshots are not atomic',
                'scope':'Buff model only; synthetic attribute projection, no new benchmark, scheduler, UE, network or allocation-failure guarantee'}
            write_new(self.output/'commands.json',json.dumps(self.records,indent=2)+'\n')
            write_new(self.output/'provenance.json',json.dumps(provenance,indent=2)+'\n')
            write_new(self.output/'report.txt','PROVENANCE '+json.dumps(provenance)+'\n'+''.join(format_record(r)for r in self.records)+'\nDRIVER_RESULT '+('FAIL'if self.failed else'PASS')+'\n')
        except OSError as e:
            self.failed=True;print('FINAL_LOG_FAILURE '+str(e),file=sys.stderr,flush=True)
        print('DRIVER_RESULT '+('FAIL'if self.failed else'PASS'),flush=True);return int(self.failed)
    def build_run(self,label:str,source:Path,flags:list[str],kind:str,target:str|None=None,model:Path|None=None)->None:
        binary=self.output/'build'/(label+'.exe')
        command=[self.args.cxx,'-std=c++17','-Wall','-Wextra','-Werror','-pedantic']+flags
        if model is not None:command += [f'-DBUFF_SOURCE="{model}"']
        c=self.command(label+'-compile',command+[str(source),'-o',str(binary)]); records=[c]
        if not okay(c): self.failed=True
        else:
            r=self.command(label+'-run',[str(binary)]);records.append(r)
            r['semantic_errors']=semantic_errors(r,kind,target);r['semantic_valid']=not r['semantic_errors']
            if r['semantic_errors']:self.failed=True
        self.log(label,records,{'kind':kind,'target_case':target,'mutant_sha256':sha(model)if model else None})
    def execute(self)->int:
        probe=[self.command('compiler-version',[self.args.cxx,'--version']),self.command('compiler-target',[self.args.cxx,'-dumpmachine'])]
        self.log('compiler',probe)
        if not all(okay(r)for r in probe):self.failed=True;return self.finish()
        for mode,flags in MODES.items():
            if self.args.mode not in ('all',mode):continue
            self.build_run('retained-'+mode,self.args.root/HERE/'src/buff_conflict.cpp',flags,'retained')
            self.build_run('contract-'+mode,self.args.root/HERE/'tests/buff_contract.cpp',flags,'contract')
        original=(self.args.root/HERE/'src/buff_conflict.cpp').read_text()
        mutations=self.output/'mutations';mutations.mkdir()
        for name,(before,after,target)in MUTATIONS.items():
            if original.count(before)!=1:
                self.failed=True;self.log(name,[],{'mutation_error':'site must occur exactly once'});continue
            mutant=mutations/(name+'.cpp');write_new(mutant,original.replace(before,after))
            self.build_run('mutant-'+name,self.args.root/HERE/'tests/buff_contract.cpp',['-O2'],'mutant',target,mutant)
        return self.finish()


FIXTURE_COMPILER = r'''#!PYTHON
import json,os,pathlib,sys,time
mode=os.environ.get('BUFF_FIXTURE_MODE','ok')
if sys.argv[1:]==['--version']: print('fake compiler');sys.exit(19 if mode=='version-fail' else 0)
if sys.argv[1:]==['-dumpmachine']: print('fake-target');sys.exit(0)
output=pathlib.Path(sys.argv[sys.argv.index('-o')+1]);name=output.stem
with open(os.environ['BUFF_FIXTURE_EVENTS'],'a')as f:f.write(json.dumps(['compile',name])+'\n')
if mode=='compile-timeout':time.sleep(10)
# Model source is still copied; fake compiler avoids running it or other models.
is_mutant=name.startswith('mutant-');is_contract=name.startswith('contract-') or is_mutant
lines=[]
if is_contract:
    cases=json.loads(os.environ['BUFF_FIXTURE_CASES']);target=json.loads(os.environ['BUFF_FIXTURE_TARGETS']).get(name[7:]) if is_mutant else None
    for case in cases:lines.append(('FAIL_CASE ' if case==target else 'PASS_CASE ')+case)
    if is_mutant:lines.append('FAIL '+target+' intentional fake semantic result')
    lines.append('RESULT cases='+str(len(cases))+' checks=323 fail='+str(int(is_mutant))+' (explicit checks remain active with NDEBUG)')
else:
    lines=['PASS  '+x+' fixture'for x in ['R'+str(n)for n in range(1,10)]+['E1','E2','E3']]
    lines+=['RESULT pass=12 fail=0 rules=R1-R9+E1-E3']
if mode=='few-checks':lines=[x.replace('checks=323','checks=33')for x in lines]
if mode=='empty':lines=[]
if mode=='missing-result':lines=[x for x in lines if not x.startswith('RESULT')]
if mode=='malformed-result':lines=[x if not x.startswith('RESULT')else 'RESULT malformed'for x in lines]
if mode=='duplicate-result':lines += [next(x for x in lines if x.startswith('RESULT'))]
if mode=='fail-marker':lines += ['FAIL fake contradiction']
code='#!'+sys.executable+'\nimport json,os,sys,time\n'
code+='with open(os.environ["BUFF_FIXTURE_EVENTS"],"a")as f:f.write(json.dumps(["run",'+repr(name)+'])+"\\n")\n'
code+='print('+repr('\n'.join(lines))+',flush=True)\n'
code+='print("fixture stderr",file=sys.stderr,flush=True)\n'
if mode=='timeout':code+='time.sleep(10)\n'
code+='sys.exit('+str(31 if mode=='run-fail' else 1 if is_mutant else 0)+')\n'
output.write_text(code);output.chmod(0o700)
if mode=='log-directory':(output.parent.parent/'logs'/(name+'.txt')).mkdir()
if mode=='log-symlink':(output.parent.parent/'logs'/(name+'.txt')).symlink_to(os.environ['BUFF_FIXTURE_SENTINEL'])
if mode=='compile-fail':sys.exit(23) # deliberately leaves stale runnable output
'''


def self_test(args:argparse.Namespace,output:Path)->int:
    if os.name!='posix':raise RuntimeError('self-test process fixtures require POSIX; native Windows untested')
    fixture=output/'fixture repo 空格'
    for directory in ('src','tests','scripts'):(fixture/HERE/directory).mkdir(parents=True)
    for p in SOURCE_PATHS:shutil.copyfile(args.root/p,fixture/p)
    driver=fixture/HERE/'scripts/run_buff_contract.py'
    compiler=output/'fixture compiler 空格';write_new(compiler,FIXTURE_COMPILER.replace('PYTHON',sys.executable,1));compiler.chmod(0o700)
    sentinel=output/'old raw sentinel';write_new(sentinel,'DO NOT OVERWRITE\n');sentinel_hash=sha(sentinel)
    records=[];failures=[];assertions=0
    def require(value:bool,label:str)->None:
        nonlocal assertions
        assertions+=1
        if not value:failures.append(label);print('SELFTEST_FAIL '+label,flush=True)
    def invoke(label:str,mode:str='ok',destination:Path|None=None,extra:list[str]|None=None,limit:bool=False)->tuple[dict,list,Path]:
        dest=destination or output/(label+' output 空格');events=output/(label+'.events.txt')
        env=dict(os.environ,BUFF_FIXTURE_MODE=mode,BUFF_FIXTURE_EVENTS=str(events),BUFF_FIXTURE_SENTINEL=str(sentinel),
                 BUFF_FIXTURE_CASES=json.dumps(CASES),BUFF_FIXTURE_TARGETS=json.dumps({k:v[2]for k,v in MUTATIONS.items()}))
        command=[sys.executable,'-B','-O',str(driver),'--output-dir',str(dest),'--cxx',str(compiler),'--mode','o0-ndebug','--timeout-seconds','0.2']+(extra or [])
        if limit:
            # Real OS write failure for child only. Parent retains full diagnostics.
            script='import os,resource,sys;resource.setrlimit(resource.RLIMIT_FSIZE,(4096,4096));os.execv(sys.argv[1],sys.argv[1:])'
            command=[sys.executable,'-B','-c',script]+command
        r=run(command,fixture,env,20,output);r['label']=label;records.append(r)
        rows=[json.loads(x)for x in events.read_text().splitlines()]if events.exists()else[]
        write_new(output/(label+'.txt'),format_record(r));return r,rows,dest
    for mode in ('ok','version-fail','compile-fail','compile-timeout','run-fail','few-checks','empty','missing-result','malformed-result','duplicate-result','fail-marker','timeout','log-directory','log-symlink','write-limit'):
        r,events,dest=invoke(mode,mode,limit=mode=='write-limit')
        require(okay(r)==(mode=='ok'),mode+' exit reflects complete result')
        require(('DRIVER_RESULT PASS'in r['stdout'])==(mode=='ok'),mode+' no false success')
        require(sha(sentinel)==sentinel_hash,mode+' sentinel preserved')
        if mode=='version-fail':require(not events,mode+' no compile after failed probe')
        if mode=='compile-fail':require(events and all(row[0]!='run'for row in events),mode+' stale executable never runs')
        if mode in ('few-checks','empty','missing-result','malformed-result','duplicate-result','fail-marker'):
            child=json.loads((dest/'commands.json').read_text());runs=[x for x in child if x['label'].endswith('-run')]
            affected=[x for x in runs if mode!='few-checks' or not x['label'].startswith('retained-')]
            require(bool(affected)and all(not x['semantic_valid']for x in affected),mode+' all deceptive reports rejected')
        if mode in ('timeout','compile-timeout'):
            child=json.loads((dest/'commands.json').read_text());timed=[x for x in child if x['timed_out']]
            require(len(timed)==2+len(MUTATIONS)and all(not x['capture_complete']and x['root_reaped']for x in timed),mode+' retained, incomplete and roots reaped')
            if mode=='compile-timeout':require(events and all(row[0]!='run'for row in events),'compile timeout never runs stale output')
        if mode=='ok':
            require([x[1]for x in events if x[0]=='compile']==['retained-o0-ndebug','contract-o0-ndebug']+['mutant-'+n for n in MUTATIONS],'only Buff-target compiles')
            require(all(x[1].startswith(('retained-','contract-','mutant-'))for x in events),'no other model run')
    for kind in ('directory','file','symlink','dangling-symlink','missing-parent','repo-path','repo-alias'):
        dest=output/(kind+' path')
        if kind=='directory':dest.mkdir();write_new(dest/'keep','keep')
        elif kind=='file':write_new(dest,'keep')
        elif kind=='symlink':dest.symlink_to(sentinel)
        elif kind=='dangling-symlink':dest.symlink_to(output/'absent')
        elif kind=='missing-parent':dest=dest/'missing'/'output'
        elif kind=='repo-path':dest=fixture/'forbidden'
        elif kind=='repo-alias':dest.symlink_to(fixture,target_is_directory=True);dest=dest/'forbidden'
        r,events,_=invoke('refuse-'+kind,destination=dest)
        require(not okay(r)and not events,kind+' rejected before compile')
        require(sha(sentinel)==sentinel_hash,kind+' no overwrite')
        if kind=='directory':require((dest/'keep').read_text()=='keep','directory content preserved')
        if kind=='file':require(dest.read_text()=='keep','file preserved')
    missing=output/'compiler absent';compiler.rename(missing)
    try:r,events,dest=invoke('missing-compiler')
    finally:missing.rename(compiler)
    require(not okay(r)and not events,'missing compiler launch failure propagates')
    child=json.loads((dest/'commands.json').read_text())
    require(all(x['capture_origin']=='synthetic_launch_diagnostic'and x['exit_code']is None for x in child),'missing compiler diagnostics distinguished from child bytes')
    # Exact non-UTF8/CRLF/NUL bytes: the display string is deliberately ambiguous.
    out=b'\xff|\\xff\r\n\x00\xfe';err=b'\xfe|\\xfe\r\n\x00\xff'
    r=run([sys.executable,'-B','-c',f'import os;os.write(1,{out!r});os.write(2,{err!r})'],fixture,dict(os.environ),2,output)
    r['label']='exact-binary-capture';records.append(r);require(okay(r),'binary child completed')
    for channel,payload in [('stdout',out),('stderr',err)]:
        decoded=base64.b64decode(r[channel+'_base64'],validate=True)
        require(decoded==payload and r[channel+'_bytes']==len(payload)and r[channel+'_sha256']==hashlib.sha256(payload).hexdigest(),'authoritative exact '+channel)
    # Same-group descendant must be killed; escaped one demonstrates the precise
    # capture boundary without an unbounded pipe wait. Fixture self-exits in 0.7s.
    for escaped in (False,True):
        label='escaped'if escaped else'group';marker=output/(label+'.done');pidfile=output/(label+'.pid')
        child=f'import pathlib,time;print("child output",flush=True);time.sleep(0.7);pathlib.Path({str(marker)!r}).write_text("done")'
        root=f'import subprocess,sys,time,pathlib;p=subprocess.Popen([sys.executable,"-B","-c",{child!r}],start_new_session={escaped});pathlib.Path({str(pidfile)!r}).write_text(str(p.pid));time.sleep(10)'
        r=run([sys.executable,'-B','-c',root],fixture,dict(os.environ),0.2,output);r['label']=label+'-timeout';records.append(r)
        require(r['timed_out']and r['root_reaped']and not r['capture_complete']and r['elapsed_seconds']<1.5,label+' bounded incomplete timeout snapshot')
        require(pidfile.is_file(),label+' controlled descendant actually launched')
        if pidfile.exists():
            pid=int(pidfile.read_text())
            def running()->bool:
                try:os.kill(pid,0)
                except ProcessLookupError:return False
                stat=Path('/proc')/str(pid)/'stat'
                try:return stat.read_text().split(') ')[1].split()[0]!='Z'
                except FileNotFoundError:return False
            deadline=time.monotonic()+3
            while running()and time.monotonic()<deadline:time.sleep(0.02)
            alive=running()
            if alive:
                try:os.kill(pid,signal.SIGKILL)
                except ProcessLookupError:pass
            require(not alive,label+' fixture descendant finished')
            require(marker.exists()==escaped,label+' actual original-group vs escape boundary')
    # Helper write_new rejects existing destinations instead of truncating them.
    try:write_new(sentinel,'bad');require(False,'exclusive log write rejected')
    except FileExistsError:require(sha(sentinel)==sentinel_hash,'exclusive log write preserved old bytes')
    require(not(fixture/HERE/'build').exists()and not(fixture/HERE/'results').exists(),'fixture repository remains output-free')
    result={'assertions':assertions,'failures':failures,'records':records,
            'source_sha256':{str(p):sha(args.root/p)for p in SOURCE_PATHS},
            'coverage':'Linux POSIX only; fake compiler semantics and real bounded capture; no other model execution'}
    write_new(output/'self-test.json',json.dumps(result,indent=2)+'\n')
    write_new(output/'report.txt','SELFTEST_METADATA '+json.dumps({k:v for k,v in result.items()if k!='records'})+'\n'+''.join(format_record(r)for r in records))
    print(f'SELFTEST_RESULT assertions={assertions} fail={len(failures)}',flush=True)
    return int(bool(failures))


def main()->int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir',required=True,type=Path)
    parser.add_argument('--root',type=Path,default=ROOT)
    parser.add_argument('--cxx',default=os.environ.get('CXX','g++'))
    parser.add_argument('--mode',choices=['all']+list(MODES),default='all')
    parser.add_argument('--timeout-seconds',type=float,default=30)
    exclusive=parser.add_mutually_exclusive_group()
    exclusive.add_argument('--negative-controls',action='store_true',help='explicitly request the real source mutants (already on by default)')
    exclusive.add_argument('--self-test',action='store_true',help='isolated driver fixtures instead of compiling real models')
    args=parser.parse_args();args.root=args.root.resolve()
    if not math.isfinite(args.timeout_seconds)or not 0<args.timeout_seconds<=600:parser.error('--timeout-seconds must be finite and in (0,600]')
    try:
        output=fresh_output(args.output_dir,[ROOT,args.root])
        return self_test(args,output)if args.self_test else Driver(args,output).execute()
    except (OSError,ValueError,RuntimeError)as e:
        print('DRIVER_ERROR '+type(e).__name__+': '+str(e),file=sys.stderr,flush=True);return 1

if __name__=='__main__':sys.exit(main())

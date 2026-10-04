#!/usr/bin/env python3
"""Build the safe C++23 suite plus isolated, explicitly checked negative controls."""
import argparse, hashlib, json, pathlib, platform, shutil, subprocess, tempfile
p=argparse.ArgumentParser()
p.add_argument('--cxx', default='g++-14')
p.add_argument('--out', type=pathlib.Path)
a=p.parse_args()
if a.out:
    if a.out.exists(): p.error('--out must not already exist')
    a.out.mkdir(parents=True)
    out=a.out.resolve()
else: out=pathlib.Path(tempfile.mkdtemp(prefix='foundation-contracts-'))
cxx=shutil.which(a.cxx)
if not cxx: p.error('compiler not found')
src=pathlib.Path(__file__).with_name('contracts.cpp').resolve()
report={'platform':platform.platform(),'source_sha256':hashlib.sha256(src.read_bytes()).hexdigest(), 'runs':[]}
report['compiler']=subprocess.run([cxx,'--version'],capture_output=True,text=True,check=True).stdout
ok=True
for mode, extra in [('O0',['-O0']),('O2',['-O2']),('UBSan',['-O1','-g','-fsanitize=undefined','-fno-sanitize-recover=all'])]:
    exe=out/(mode+'.bin')
    cmd=[cxx,'-std=c++23','-pthread','-Wall','-Wextra','-Wpedantic','-Werror',*extra,str(src),'-o',str(exe)]
    entry={'mode':mode,'compile_command':cmd}
    try:
        b=subprocess.run(cmd,capture_output=True,text=True,timeout=120)
        entry['compile_exit']=b.returncode
        (out/(mode+'.build.txt')).write_text(b.stdout+b.stderr)
        if b.returncode == 0:
            r=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30)
            entry['run_exit']=r.returncode
            (out/(mode+'.stdout.txt')).write_text(r.stdout)
            (out/(mode+'.stderr.txt')).write_text(r.stderr)
            ok=ok and r.returncode == 0 and 'TOTAL 45 PASS' in r.stdout
        else: ok=False
    except subprocess.TimeoutExpired:
        entry['timeout']=True; ok=False
    finally:
        if exe.exists(): exe.unlink()
    report['runs'].append(entry)
# Negative controls are isolated subprocesses, never part of the safe example.
negative = [
 ('variant_definition', '#include <variant>\nusing V = std::variant<struct X { int n; }, int>;\nint main() {}\n', 'compile'),
 ('zero_capacity', '#define main contract_main\n#include "' + str(src) + '"\n#undef main\nFixedPool<int, 0> invalid;\nint main() {}\n', 'compile'),
 ('old_byte_stride', '#include <cstddef>\nunion Node { Node* next; alignas(int) std::byte data[sizeof(int)]; };\nint main() { alignas(Node) std::byte bytes[sizeof(Node)*3]; auto* p = reinterpret_cast<Node*>(&bytes[1]); p->next = nullptr; }\n', 'ubsan')
]
report['negative_controls']=[]
for name, contents, kind in negative:
    fixture=out/(name+'.cpp'); fixture.write_text(contents)
    exe=out/(name+'.bin')
    cmd=[cxx,'-std=c++23','-pthread','-O1','-fsanitize=undefined','-fno-sanitize-recover=all',str(fixture),'-o',str(exe)]
    item={'name':name, 'compile_command':cmd}
    try:
        b=subprocess.run(cmd,capture_output=True,text=True,timeout=120)
        (out/(name+'.build.txt')).write_text(b.stdout+b.stderr)
        item['compile_exit']=b.returncode
        if kind=='compile':
            item['passed']=b.returncode != 0 and ('template' in b.stderr if name=='variant_definition' else 'static assertion' in b.stderr)
        elif b.returncode==0:
            r=subprocess.run([str(exe)],capture_output=True,text=True,timeout=10)
            (out/(name+'.stdout.txt')).write_text(r.stdout)
            (out/(name+'.stderr.txt')).write_text(r.stderr)
            item.update(run_exit=r.returncode, passed=r.returncode != 0 and 'misaligned' in r.stderr)
        else: item['passed']=False
    except subprocess.TimeoutExpired:
        item.update(timeout=True,passed=False)
    finally:
        if exe.exists(): exe.unlink()
    ok=ok and item['passed']
    report['negative_controls'].append(item)
report['passed']=ok
(out/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2))
print(out)
raise SystemExit(0 if ok else 1)

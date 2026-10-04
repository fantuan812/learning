#!/usr/bin/env python3
"""Real compiled fault fixtures and semantic mutants; checks survive python -O."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time

HERE = Path(__file__).resolve().parent
RUNNER = HERE / "run_damage_contract.py"
ROOT = HERE.parents[3]
SOURCE = HERE.parent / "src/damage_pipeline.cpp"
TEST = HERE.parent / "tests/damage_contract.cpp"


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--cxx", default="g++")
    parser.add_argument("--pwsh", default="")
    args = parser.parse_args()
    require(not os.path.lexists(args.out), "contract output already exists")
    out = args.out.resolve()
    require(out != ROOT and ROOT not in out.parents, "output must be outside repository")
    out.mkdir()
    records = []
    passed = 0
    wrappers = ["python", "python-O", "bash"] + (["pwsh"] if args.pwsh else [])

    def command(wrapper, dest, test, compiler=args.cxx, timeout=3):
        if wrapper == "pwsh":
            return [args.pwsh, "-NoProfile", "-File", str(HERE / "build_run.ps1"), "-Out", str(dest),
                    "-Gxx", compiler, "-Python", sys.executable, "-Test", str(test), "-Timeout", str(timeout)]
        tail = ["--out", str(dest), "--cxx", compiler, "--test", str(test), "--timeout", str(timeout)]
        if wrapper == "bash":
            return ["bash", str(HERE / "run_all.sh")] + tail
        return [sys.executable] + (["-O"] if wrapper == "python-O" else []) + ["-B", str(RUNNER)] + tail

    def run(label, cmd, success, env=None, expected_text=None, deadline=30):
        nonlocal passed
        started = time.time()
        p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=deadline, env=env, check=False)
        (out / (label + ".stdout.txt")).write_bytes(p.stdout)
        (out / (label + ".stderr.txt")).write_bytes(p.stderr)
        rec = {"case": label, "command": cmd, "returncode": p.returncode, "expected_success": success,
               "started_epoch": started, "elapsed_seconds": time.time()-started,
               "stdout_sha256": digest(out / (label + ".stdout.txt")), "stderr_sha256": digest(out / (label + ".stderr.txt"))}
        records.append(rec)
        require((p.returncode == 0) == success, f"wrong exit for {label}: {p.returncode}")
        if not success:
            require(b"COMPLETED" not in p.stdout, f"false completed marker for {label}")
        if expected_text is not None:
            require(expected_text in p.stdout+p.stderr, f"missing diagnostic for {label}")
        passed += 1
        print("PASS " + label, flush=True)
        return p

    fixtures = {
        "success": 'std::cout<<"PASS fixture\\nRESULT pass=1 fail=0\\n";',
        "runtime23": 'std::cout<<"PASS fixture\\nRESULT pass=1 fail=0\\n"; return 23;',
        "signal": 'std::raise(SIGTERM);',
        "runtime-timeout": 'std::this_thread::sleep_for(std::chrono::seconds(8));',
        "missing-result": 'std::cout<<"PASS fixture\\n";',
        "malformed-result": 'std::cout<<"PASS fixture\\nRESULT pass=x fail=0\\n";',
        "duplicate-result": 'std::cout<<"PASS fixture\\nRESULT pass=1 fail=0\\nRESULT pass=1 fail=0\\n";',
        "fail-count": 'std::cout<<"PASS fixture\\nRESULT pass=1 fail=1\\n";',
        "count-mismatch": 'std::cout<<"PASS fixture\\nRESULT pass=2 fail=0\\n";',
        "fail-text": 'std::cout<<"FAIL hidden\\nPASS fixture\\nRESULT pass=1 fail=0\\n";',
        "duplicate-pass": 'std::cout<<"PASS fixture\\nPASS fixture\\nRESULT pass=2 fail=0\\n";',
        "stderr-result": 'std::cout<<"PASS fixture\\nRESULT pass=1 fail=0\\n";std::cerr<<"RESULT pass=1 fail=0\\n";',
        "compile-error": 'this is invalid C++',
    }
    paths = {}
    for name, body in fixtures.items():
        path = out / (name + ".cpp")
        path.write_text('#include <iostream>\n#include <csignal>\n#include <thread>\n#include <chrono>\nint main(){' + body + '}\n')
        paths[name] = path
    # Controlled compiler faults still invoke the real compiler for write-failure cases.
    compilers = {}
    for name in ("compile-fail", "no-binary", "compile-timeout", "raw-write-fail", "manifest-write-fail"):
        path = out / (name + "-compiler")
        body = {"compile-fail": "sys.exit(17)", "no-binary": "sys.exit(0)",
                "compile-timeout": "time.sleep(8)",
                "raw-write-fail": "(dest.parent/'run-o0.stdout.txt').mkdir()\nsys.exit(subprocess.call([REAL]+sys.argv[1:]))",
                "manifest-write-fail": "p=dest.parent/'run-manifest.json'\np.mkdir(exist_ok=True)\nsys.exit(subprocess.call([REAL]+sys.argv[1:]))"}[name]
        path.write_text(f'#!{sys.executable}\nimport pathlib,subprocess,sys,time\nREAL={args.cxx!r}\n'
                        'if "--version" in sys.argv:\n print("controlled synthetic compiler");sys.exit(0)\n'
                        'dest=pathlib.Path(sys.argv[sys.argv.index("-o")+1])\n' + body + '\n')
        path.chmod(0o700)
        compilers[name] = str(path)
    for wrapper in wrappers:
        for name, test in paths.items():
            run(f"{wrapper}-{name}", command(wrapper, out / f"run-{wrapper}-{name}", test), name=="success")
        for name, compiler in compilers.items():
            run(f"{wrapper}-{name}", command(wrapper, out / f"run-{wrapper}-{name}", paths["success"], compiler), False)
        run(f"{wrapper}-missing-compiler", command(wrapper, out / f"run-{wrapper}-missing-compiler", paths["success"], str(out/'absent-cxx')), False)
        for existing in ("empty", "stale"):
            dest = out / f"existing-{wrapper}-{existing}";dest.mkdir()
            if existing == "stale":
                (dest/'damage-o0').write_text('#!/bin/sh\necho stale > '+str(out/'STALE_EXECUTED')+'\n')
                (dest/'damage-o0').chmod(0o700);(dest/'old-raw.txt').write_bytes(b'preserved old raw\x00\xff')
            before = {p.name: digest(p) for p in dest.iterdir()}
            run(f"{wrapper}-existing-{existing}", command(wrapper,dest,paths["success"]),False)
            require(before=={p.name:digest(p) for p in dest.iterdir()},"existing output changed")
        alias = out / f"alias-{wrapper}";alias.symlink_to(dest,target_is_directory=True)
        run(f"{wrapper}-symlink-existing",command(wrapper,alias,paths["success"]),False)
        broken = out / f"broken-{wrapper}";broken.symlink_to(out/'missing-target')
        run(f"{wrapper}-broken-symlink",command(wrapper,broken,paths["success"]),False)
        parent = out / f"parent-file-{wrapper}";parent.write_text("not a directory")
        run(f"{wrapper}-create-failure",command(wrapper,parent/'child',paths["success"]),False)
        run(f"{wrapper}-repo-output",command(wrapper,HERE.parent/f"forbidden-output-{wrapper}",paths["success"]),False)
    require(not (out/'STALE_EXECUTED').exists(),"stale binary executed")
    env=dict(os.environ,PYTHON=str(out/'absent-python'))
    run("bash-missing-python",["bash",str(HERE/'run_all.sh'),"--out",str(out/'missing-python-bash')],False,env=env)
    if args.pwsh:
        run("pwsh-missing-python",[args.pwsh,"-NoProfile","-File",str(HERE/'build_run.ps1'),"-Out",str(out/'missing-python-pwsh'),"-Python",str(out/'absent-python')],False)
    # Actual semantic mutants, compiled with the same independent contract suite.
    text = SOURCE.read_text()
    mutations = {
        "drops-phase": ("uint64_t phase = d.phaseUs;", "uint64_t phase = 0;"),
        "wrong-actual-hp": ("r.toHp = before - after;", "r.toHp = r.requestedHpDamage;"),
        "unstable-armor": ("if (armor >= k) { const double q = k / armor; return q / (1 + q); }\n    const double q = armor / k;\n    return 1 / (1 + q);", "return 1 - armor / (armor + k);"),
        "cap-before-armor": ("double value = raw;", "double value = spec.perHitCap > 0 ? std::min(raw, spec.perHitCap) : raw;"),
    }
    for name,(old,new) in mutations.items():
        require(text.count(old)==1,f"mutation anchor changed: {name}")
        mutated=out/(name+'.cpp');mutated.write_text(text.replace(old,new))
        dest=out/('mutant-'+name)
        cmd=[sys.executable,'-O','-B',str(RUNNER),'--out',str(dest),'--cxx',args.cxx,'--source',str(mutated),'--test',str(TEST)]
        run('mutant-'+name,cmd,False)
        m=json.loads((dest/'run-manifest.json').read_text())
        require(any(s['stage']=='compile-o0' and s['returncode']==0 for s in m['stages']),"mutant must compile")
        raw=(dest/'run-o0.stdout.txt').read_text()
        require('FAIL ' in raw and re_result_failed(raw),"semantic mutant must fail a real assertion")
        records[-1]['mutant_source_sha256']=digest(mutated)
    (out/'contract-manifest.json').write_text(json.dumps({'python_optimized':sys.flags.optimize,'source_sha256':digest(SOURCE),
        'test_sha256':digest(TEST),'runner_sha256':digest(RUNNER),'records':records,'pass':passed,'fail':0,
        'pwsh':args.pwsh or 'not-run','scope':'Linux synthetic fixtures; no Windows/CPU benchmark'},indent=2)+'\n')
    print(f"RESULT pass={passed} fail=0")
    return 0


def re_result_failed(text):
    import re
    matches=re.findall(r'^RESULT pass=(\d+) fail=(\d+)$',text,re.M)
    return len(matches)==1 and int(matches[0][1])>0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError,ValueError,RuntimeError,subprocess.TimeoutExpired) as error:
        print(f'FAIL runner contract: {error}',file=sys.stderr)
        sys.exit(1)

#!/usr/bin/env python3
"""Times the programs of bench/backends/ on each native back end.

For every program, back end and build mode it builds an executable with `gamma build`
(timing the build), runs it a few times (keeping the fastest run) and checks that every
back end prints the same checksum. The back ends are the C back end with gcc and with
clang, Cranelift, and the experimental LLVM back end at -O0 and -O2 (and -O1/-O3 with
--all-levels). Prints a Markdown table; --json writes the raw numbers too.

    cargo build --release
    python3 scripts/bench-backends.py                # all programs, modes checked and fast
    python3 scripts/bench-backends.py --runs=5 fib sieve
"""

import argparse
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = pathlib.Path(__file__).resolve().parent.parent


def backends(all_levels):
    """(name, gamma arguments, extra environment) of each configuration."""
    out = [
        ("c (gcc)", ["--backend=c"], {"CC": "gcc"}),
        ("c (clang)", ["--backend=c"], {"CC": "clang"}),
        ("cranelift", ["--backend=cranelift"], {}),
    ]
    for level in ([0, 1, 2, 3] if all_levels else [0, 2]):
        out.append((f"llvm -O{level}", ["--backend=llvm"], {"GAMMA_LLVM_OPT": str(level)}))
    return out


def timed(cmd, env):
    start = time.perf_counter()
    p = subprocess.run(cmd, env=env, capture_output=True, text=True)
    return time.perf_counter() - start, p


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("programs", nargs="*", help="program names in bench/backends (default: all)")
    ap.add_argument("--gamma", default=str(ROOT / "target/release/gamma"))
    ap.add_argument("--runs", type=int, default=3)
    ap.add_argument("--modes", default="checked,fast")
    ap.add_argument("--all-levels", action="store_true", help="also LLVM -O1 and -O3")
    ap.add_argument("--json", help="write the measurements to this file")
    args = ap.parse_args()

    progs = sorted((ROOT / "bench/backends").glob("*.gm"))
    if args.programs:
        progs = [p for p in progs if p.stem in args.programs]
    # A private cache, warmed by building each program twice and timing the second build:
    # the runtime object is then cached, as in everyday use. (`gamma build` never serves a
    # whole program from the cache.)
    work = pathlib.Path(tempfile.mkdtemp(prefix="gamma-bench-backends-"))
    rows = []
    failed = False
    try:
        for mode in args.modes.split(","):
            for prog in progs:
                outputs = {}
                for name, gargs, extra in backends(args.all_levels):
                    env = dict(os.environ, **extra)
                    env["GAMMA_CACHE_DIR"] = str(work / "cache")
                    exe = work / f"{prog.stem}-{len(rows)}"
                    build = [args.gamma, "build", f"--mode={mode}", *gargs, "-o", str(exe), str(prog)]
                    timed(build, env)
                    build_s, p = timed(build, env)
                    if p.returncode != 0:
                        print(f"{prog.stem} {name} {mode}: build failed\n{p.stderr}", file=sys.stderr)
                        failed = True
                        continue
                    best = None
                    for _ in range(args.runs):
                        run_s, r = timed([str(exe)], env)
                        if r.returncode != 0:
                            print(f"{prog.stem} {name} {mode}: run failed\n{r.stderr}", file=sys.stderr)
                            failed = True
                            break
                        best = run_s if best is None else min(best, run_s)
                        outputs[name] = r.stdout
                    rows.append({"program": prog.stem, "mode": mode, "backend": name, "build_s": build_s, "run_s": best})
                if len(set(outputs.values())) > 1:
                    print(f"{prog.stem} {mode}: back ends disagree: {outputs}", file=sys.stderr)
                    failed = True
    finally:
        shutil.rmtree(work, ignore_errors=True)

    names = [b[0] for b in backends(args.all_levels)]
    for mode in args.modes.split(","):
        for what, key in (("run time (s)", "run_s"), ("build time (s)", "build_s")):
            print(f"\n### {mode}: {what}\n")
            print("| program | " + " | ".join(names) + " |")
            print("|---|" + "---:|" * len(names))
            for prog in progs:
                cells = []
                for n in names:
                    r = next((r for r in rows if r["program"] == prog.stem and r["mode"] == mode and r["backend"] == n), None)
                    cells.append("—" if r is None or r[key] is None else f"{r[key]:.3f}")
                print(f"| {prog.stem} | " + " | ".join(cells) + " |")
    if args.json:
        pathlib.Path(args.json).write_text(json.dumps(rows, indent=1))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())

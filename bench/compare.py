#!/usr/bin/env python3
"""Interleaved A/B benchmark of webbed extension builds over bench/cases.

Each arm is a loadable webbed.duckdb_extension. Every case runs under every
arm inside each round, in rotating order, so machine drift lands on all arms.
Samples come from hyperfine 2.0 exports, which record wall time, CPU time,
peak RSS and instructions per run.

  compare.py run OUT --arm base=PATH --arm cand=PATH [--cases GLOB] [--rounds R] [--runs N]
  compare.py guard --arm base=PATH --arm cand=PATH [--cases GLOB]
  compare.py report OUT [--base base] [--markdown]
"""

import argparse
import glob
import json
import math
import os
import random
import statistics
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CASES_DIR = os.path.join(ROOT, "bench", "cases")
DATA_ROOT = os.environ.get("BENCH_DATA", os.path.join(ROOT, "tmp", "bench-data"))
DUCKDB = os.environ.get(
    "BENCH_DUCKDB", os.path.join(ROOT, "tmp", "climb", "duckdb")
)
HYPERFINE = os.environ.get(
    "BENCH_HYPERFINE",
    os.path.expanduser(
        "~/.local/share/mise/installs/aqua-sharkdp-hyperfine/2.0.0/hyperfine-v2.0.0-x86_64-apple-darwin/hyperfine"
    ),
)

METRICS = {
    "wall": ("time_wall_clock", "s"),
    "cpu": ("time_cpu", "s"),
    "rss": ("memory_peak_resident", "MB"),
    "instr": ("instructions", "G"),
}
ALPHA = 0.1
MIN_EFFECT = 0.03


def case_header(path):
    header = {}
    with open(path) as f:
        for line in f:
            if not line.startswith("--"):
                break
            key, _, value = line[2:].partition(":")
            header[key.strip()] = value.strip()
    return header


def find_cases(pattern, split):
    paths = sorted(glob.glob(os.path.join(CASES_DIR, pattern)))
    cases = []
    for path in paths:
        if split and case_header(path).get("split") != split:
            continue
        name = os.path.relpath(path, CASES_DIR)[: -len(".sql")]
        cases.append((name, path))
    if not cases:
        sys.exit(f"no cases match {pattern} (split={split})")
    return cases


def parse_arms(specs):
    arms = []
    for spec in specs:
        name, sep, path = spec.partition("=")
        if not sep:
            sys.exit(f"--arm expects name=path, got {spec}")
        path = os.path.abspath(path)
        if not os.path.exists(path):
            sys.exit(f"arm {name}: {path} does not exist")
        arms.append((name, path))
    return arms


def duckdb_argv(ext, case_path):
    init = os.path.join(os.path.dirname(ext), "init.sql")
    with open(init, "w") as f:
        f.write(f"LOAD '{ext}';\n")
    return [DUCKDB, "-unsigned", "-noheader", "-csv", "-init", init, "-f", case_path]


def run(args):
    arms = parse_arms(args.arm)
    cases = find_cases(args.cases, args.split)
    args.out = os.path.abspath(args.out)
    os.makedirs(args.out, exist_ok=False)
    with open(os.path.join(args.out, "meta.json"), "w") as f:
        json.dump({"arms": dict(arms), "cases": [c for c, _ in cases], "duckdb": DUCKDB}, f, indent=1)
    for rnd in range(args.rounds):
        load = os.getloadavg()[0]
        print(f"round {rnd + 1}/{args.rounds} load={load:.1f}", flush=True)
        for ci, (name, path) in enumerate(cases):
            shift = (rnd + ci) % len(arms)
            order = arms[shift:] + arms[:shift]
            for arm, ext in order:
                export = os.path.join(args.out, name.replace("/", "__") + f".{arm}.r{rnd}.json")
                cmd = " ".join(duckdb_argv(ext, path))
                proc = subprocess.run(
                    [HYPERFINE, "-N", "--runs", str(args.runs), "--warmup", str(args.warmup), "--export-json", export,
                     "--output", "null", "--style", "none", cmd],
                    cwd=DATA_ROOT, capture_output=True, text=True,
                )
                if proc.returncode != 0:
                    sys.exit(f"{name} [{arm}] failed:\n{proc.stderr[-2000:]}")
        if args.report_each:
            report_dir(args.out, arms[0][0], markdown=False, metrics=["wall", "cpu", "rss"])


def guard(args):
    arms = parse_arms(args.arm)
    cases = find_cases(args.cases, args.split)
    expected = {}
    if args.expect:
        for line in open(args.expect):
            name, _, out = line.rstrip("\n").partition(": ")
            expected[name] = (0, out)
    failed = 0
    for name, path in cases:
        outputs = {"expected": (*expected[name], "")} if name in expected else {}
        for arm, ext in arms:
            proc = subprocess.run(duckdb_argv(ext, path), cwd=DATA_ROOT, capture_output=True, text=True)
            outputs[arm] = (proc.returncode, proc.stdout.strip(), proc.stderr.strip()[-500:])
        base = outputs.get("expected", outputs[arms[0][0]])
        for arm, out in outputs.items():
            if out[:2] != base[:2]:
                failed += 1
                print(f"MISMATCH {name}: base={base} {arm}={out}")
        if base[0] != 0:
            failed += 1
            print(f"ERROR {name}: {base}")
        elif args.verbose:
            print(f"{name}: {base[1]}")
    print(f"guard: {failed} mismatches over {len(cases)} cases")
    sys.exit(1 if failed else 0)


def load_samples(out):
    samples = {}
    for path in glob.glob(os.path.join(out, "*.json")):
        base = os.path.basename(path)
        if base == "meta.json":
            continue
        case, arm, _rnd, _ = base.rsplit(".", 3)
        data = json.load(open(path))
        for result in data["results"]:
            for m in result["measurements"]:
                if m.get("exit_code", 0) != 0:
                    continue
                bucket = samples.setdefault(case.replace("__", "/"), {}).setdefault(arm, {k: [] for k in METRICS})
                for key, (field, _) in METRICS.items():
                    if field in m:
                        bucket[key].append(m[field]["value"])
    return samples


def permutation_p(a, b, iterations=5000):
    rng = random.Random(0)
    observed = abs(statistics.median(a) - statistics.median(b))
    pooled = a + b
    extreme = 0
    for _ in range(iterations):
        rng.shuffle(pooled)
        if abs(statistics.median(pooled[: len(a)]) - statistics.median(pooled[len(a):])) >= observed - 1e-12:
            extreme += 1
    return (extreme + 1) / (iterations + 1)


def fmt(value, metric):
    unit = METRICS[metric][1]
    if unit == "MB":
        return f"{value / 1e6:.1f}"
    if unit == "G":
        return f"{value / 1e9:.2f}"
    return f"{value:.3f}"


def report_dir(out, base, markdown, metrics):
    samples = load_samples(out)
    rows = []
    ratios = {}
    for case in sorted(samples):
        arms = samples[case]
        if base not in arms:
            continue
        for arm in sorted(arms):
            if arm == base:
                continue
            row = [case, arm]
            for metric in metrics:
                a, b = arms[base][metric], arms[arm][metric]
                if len(a) < 2 or len(b) < 2:
                    row.append("n/a")
                    continue
                ma, mb = statistics.median(a), statistics.median(b)
                change = (mb - ma) / ma if ma else 0.0
                p = permutation_p(a, b)
                star = "*" if p < ALPHA and abs(change) >= MIN_EFFECT and min(len(a), len(b)) >= 4 else ""
                ratios.setdefault((arm, metric), []).append(mb / ma if ma else 1.0)
                row.append(f"{fmt(ma, metric)}→{fmt(mb, metric)} {change:+.1%} p={p:.2f}{star}")
            rows.append(row)
    header = ["case", "arm"] + [f"{m} ({METRICS[m][1]})" for m in metrics]
    if markdown:
        print("| " + " | ".join(header) + " |")
        print("| " + " | ".join(["---"] * 2 + ["---:"] * len(metrics)) + " |")
        for row in rows:
            print("| " + " | ".join(row) + " |")
    else:
        widths = [max(len(str(r[i])) for r in rows + [header]) for i in range(len(header))]
        for row in [header] + rows:
            print("  ".join(str(c).ljust(w) for c, w in zip(row, widths)))
    print()
    for (arm, metric), rs in sorted(ratios.items()):
        geo = math.exp(sum(math.log(r) for r in rs) / len(rs))
        print(f"geomean {arm} {metric}: {geo - 1:+.1%} over {len(rs)} cases")
    counts = {arm: {m: len(v) for m, v in d.items()} for case in samples.values() for arm, d in case.items()}
    print(f"samples per arm per case: { {a: c['wall'] for a, c in counts.items()} }")


def report(args):
    metrics = args.metrics.split(",")
    report_dir(args.out, args.base, args.markdown, metrics)


def main():
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("run")
    p.add_argument("out")
    p.add_argument("--arm", action="append", required=True)
    p.add_argument("--cases", default="*/*.sql")
    p.add_argument("--split", default="dev")
    p.add_argument("--rounds", type=int, default=4)
    p.add_argument("--runs", type=int, default=2)
    p.add_argument("--warmup", type=int, default=1)
    p.add_argument("--report-each", action="store_true")
    p.set_defaults(func=run)

    g = sub.add_parser("guard")
    g.add_argument("--arm", action="append", required=True)
    g.add_argument("--cases", default="*/*.sql")
    g.add_argument("--split", default="")
    g.add_argument("--verbose", action="store_true")
    g.add_argument("--expect", help="file of 'case: output' lines from a guard --verbose run")
    g.set_defaults(func=guard)

    r = sub.add_parser("report")
    r.add_argument("out")
    r.add_argument("--base", default="base")
    r.add_argument("--markdown", action="store_true")
    r.add_argument("--metrics", default="wall,cpu,rss,instr")
    r.set_defaults(func=report)

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()

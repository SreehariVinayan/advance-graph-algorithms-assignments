#!/usr/bin/env python3
"""Controlled scaling experiment: Dinic vs Push-Relabel, driven by benchmark.c.

Pipeline
  1. Generate synthetic graphs (DIMACS .max) per family / size / seed, with fixed
     structure rules and uniform random capacities.
  2. For every (family, size) folder, call the benchmark binary:
        benchmark <dinic> <pushrelabel> <raw.csv> --runs R --warmup W <folder>
     (the benchmark runs BOTH algorithms on the identical file and aborts if
      their max-flow values disagree).
  3. Aggregate the *_summary.csv files into per-size statistics: mean / median /
     std of each algorithm over independent graphs, paired ratio
     R = T_pushrelabel / T_dinic per graph, geometric-mean ratio with 95% CI,
     operation counts, memory.  Optional plots (needs matplotlib).

Families
  sparse  : random digraph, ~avg_degree*V edges (+ s->t backbone path)
  dense   : random digraph, constant density (default 10%) -> edges ~ V^2
  layered : source, L layers of ~constant width, sink; edges only between
            consecutive layers (constant out-degree per node)

Example
  python run_experiment.py --build-benchmark benchmark.c \
      --dinic ./dinic --pushrelabel ./pushrelabel
"""
import argparse, csv, math, os, random, signal, statistics, subprocess, sys
from pathlib import Path

DEFAULT_SIZES = {
    "sparse":  [200, 400, 800, 1600, 3200, 6400],
    "dense":   [100, 200, 400, 800, 1600],      # edges grow ~V^2, so smaller
    "layered": [200, 400, 800, 1600, 3200, 6400],
}

# two-sided 95% Student-t critical values (conservative: use largest key <= df)
T95 = {1: 12.706, 2: 4.303, 3: 3.182, 4: 2.776, 5: 2.571, 6: 2.447, 7: 2.365,
       8: 2.306, 9: 2.262, 10: 2.228, 11: 2.201, 12: 2.179, 13: 2.160,
       14: 2.145, 15: 2.131, 16: 2.120, 17: 2.110, 18: 2.101, 19: 2.093,
       20: 2.086, 25: 2.060, 30: 2.042}


def t_crit(df):
    if df < 1:
        return float("nan")
    if df > 30:
        return 2.0
    return T95[max(k for k in T95 if k <= df)]


# ------------------------------------------------------------ graph generation
def gen_random(n, target, rng):
    """Directed graph, no self-loops/duplicates, backbone path 0->1->...->n-1."""
    edges = {(u, u + 1) for u in range(n - 1)}
    target = min(target, n * (n - 1))
    while len(edges) < target:
        u, v = rng.randrange(n), rng.randrange(n)
        if u != v:
            edges.add((u, v))
    return edges


def gen_layered(n, rng, width, deg):
    inner = n - 2
    L = max(2, round(inner / width))
    base, extra = divmod(inner, L)
    layers, nxt = [], 1
    for i in range(L):
        size = base + (1 if i < extra else 0)
        layers.append(list(range(nxt, nxt + size)))
        nxt += size
    src, snk = 0, n - 1
    edges = {(src, v) for v in layers[0]}
    edges |= {(v, snk) for v in layers[-1]}
    for a, b in zip(layers, layers[1:]):
        k = min(deg, len(b))
        for u in a:
            for v in rng.sample(b, k):
                edges.add((u, v))
    return edges


def write_graph(path, n, edges, rng, cmin, cmax):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w") as f:
        f.write(f"c synthetic graph, source=1 sink={n}\n")
        f.write(f"p max {n} {len(edges)}\n")
        f.write(f"n 1 s\nn {n} t\n")
        for u, v in sorted(edges):
            f.write(f"a {u + 1} {v + 1} {rng.randint(cmin, cmax)}\n")


def generate(args, family, n, seed, folder):
    path = folder / f"{family}_n{n}_s{seed}.max"
    if path.exists():
        return path
    rng = random.Random(f"{family}-{n}-{seed}")      # deterministic per graph
    if family == "sparse":
        edges = gen_random(n, int(args.avg_degree * n), rng)
    elif family == "dense":
        edges = gen_random(n, int(args.density * n * (n - 1)), rng)
    else:
        edges = gen_layered(n, rng, args.layer_width, args.layer_degree)
    write_graph(path, n, edges, rng, args.cap_min, args.cap_max)
    return path


# ------------------------------------------------------------- running benchmark
def run_batch(cmd, timeout):
    """Run benchmark in its own process group so a timeout kills the children too."""
    proc = subprocess.Popen(cmd, start_new_session=True)
    try:
        return proc.wait(timeout=timeout or None)
    except subprocess.TimeoutExpired:
        os.killpg(proc.pid, signal.SIGKILL)
        proc.wait()
        return "timeout"
    except KeyboardInterrupt:
        os.killpg(proc.pid, signal.SIGKILL)
        raise


# -------------------------------------------------------------------- analysis
def fnum(row, key):
    return float(row[key])


def aggregate(family, size, summary_path):
    with open(summary_path, newline="") as f:
        rows = list(csv.DictReader(f))
    per = {}
    for r in rows:
        per.setdefault(r["dataset"], {})[r["algorithm"]] = r
    pairs = [(v["Dinic"], v["Push-Relabel"]) for v in per.values()
             if "Dinic" in v and "Push-Relabel" in v]
    if not pairs:
        return None

    dt = [fnum(d, "time_mean_us") for d, _ in pairs]
    pt = [fnum(p, "time_mean_us") for _, p in pairs]
    ratios = [p / d for d, p in zip(dt, pt) if d > 0 and p > 0]
    out = {"family": family, "nodes_target": size, "graphs": len(pairs),
           "nodes_mean": statistics.mean(fnum(d, "nodes") for d, _ in pairs),
           "edges_mean": statistics.mean(fnum(d, "edges") for d, _ in pairs),
           "max_flow_mean": statistics.mean(fnum(d, "max_flow") for d, _ in pairs)}

    def desc(prefix, xs):
        out[f"{prefix}_mean_us"] = statistics.mean(xs)
        out[f"{prefix}_median_us"] = statistics.median(xs)
        out[f"{prefix}_sd_us"] = statistics.stdev(xs) if len(xs) > 1 else 0.0

    desc("dinic", dt)
    desc("pr", pt)
    if ratios:
        logs = [math.log(r) for r in ratios]
        m = statistics.mean(logs)
        h = (t_crit(len(logs) - 1) * statistics.stdev(logs) / math.sqrt(len(logs))
             if len(logs) > 1 else float("nan"))
        out.update(ratio_geomean=math.exp(m), ratio_ci_low=math.exp(m - h),
                   ratio_ci_high=math.exp(m + h), ratio_median=statistics.median(ratios),
                   pr_faster_graphs=sum(r < 1 for r in ratios),
                   ratio_graphs=len(ratios))
    mean = lambda rows_, k: statistics.mean(fnum(r, k) for r in rows_)
    ds, ps = [d for d, _ in pairs], [p for _, p in pairs]
    out.update(dinic_mem_kb=mean(ds, "memory_mean_kb"), pr_mem_kb=mean(ps, "memory_mean_kb"),
               dinic_bfs_phases=mean(ds, "bfs_phases_mean"),
               dinic_dfs_calls=mean(ds, "dfs_calls_mean"),
               dinic_augmentations=mean(ds, "augmentations_mean"),
               dinic_edge_scans=mean(ds, "edge_scans_mean"),
               pr_pushes=mean(ps, "push_operations_mean"),
               pr_relabels=mean(ps, "relabel_operations_mean"),
               pr_discharges=mean(ps, "discharge_operations_mean"),
               pr_edge_scans=mean(ps, "edge_scans_mean"),
               pr_global_relabels=mean(ps, "global_relabels_mean"),
               pr_gap_operations=mean(ps, "gap_operations_mean"))
    return out


def make_plots(results, out_dir):
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError:
        print("matplotlib not installed: skipping plots")
        return
    for fam in sorted({r["family"] for r in results}):
        rs = sorted((r for r in results if r["family"] == fam), key=lambda r: r["nodes_target"])
        x = [r["nodes_target"] for r in rs]
        fig, ax = plt.subplots(1, 2, figsize=(11, 4))
        for key, label in (("dinic", "Dinic"), ("pr", "Push-Relabel")):
            ax[0].errorbar(x, [r[f"{key}_mean_us"] for r in rs],
                           yerr=[r[f"{key}_sd_us"] for r in rs], marker="o",
                           capsize=3, label=label)
        ax[0].set(xscale="log", yscale="log", xlabel="nodes", ylabel="time (us)",
                  title=f"{fam}: runtime vs size")
        ax[0].legend()
        rr = [r for r in rs if "ratio_geomean" in r]
        if rr:
            g = [r["ratio_geomean"] for r in rr]
            lo = [max(0, a - r["ratio_ci_low"]) if r["ratio_ci_low"] == r["ratio_ci_low"] else 0
                  for a, r in zip(g, rr)]
            hi = [max(0, r["ratio_ci_high"] - a) if r["ratio_ci_high"] == r["ratio_ci_high"] else 0
                  for a, r in zip(g, rr)]
            ax[1].errorbar([r["nodes_target"] for r in rr], g, yerr=[lo, hi],
                           marker="o", capsize=3)
            ax[1].axhline(1.0, color="gray", ls="--")
        ax[1].set(xscale="log", xlabel="nodes", ylabel="T_PR / T_Dinic (geo-mean, 95% CI)",
                  title=f"{fam}: paired ratio (>1 means Dinic faster)")
        fig.tight_layout()
        fig.savefig(out_dir / f"plot_{fam}.png", dpi=150)
        plt.close(fig)


def print_table(results):
    print(f"\n{'family':8} {'nodes':>6} {'graphs':>6} {'Dinic us':>12} {'PR us':>12} "
          f"{'ratio PR/D':>11}  95% CI")
    for r in results:
        ci = (f"[{r['ratio_ci_low']:.3f}, {r['ratio_ci_high']:.3f}]"
              if "ratio_geomean" in r else "n/a")
        print(f"{r['family']:8} {r['nodes_target']:>6} {r['graphs']:>6} "
              f"{r['dinic_mean_us']:>12.1f} {r['pr_mean_us']:>12.1f} "
              f"{r.get('ratio_geomean', float('nan')):>11.3f}  {ci}")


# ------------------------------------------------------------------------ main
def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--benchmark", default="./benchmark", help="compiled benchmark binary")
    ap.add_argument("--build-benchmark", metavar="BENCHMARK_C",
                    help="compile this benchmark.c to --benchmark first (gcc -O2 ... -lm)")
    ap.add_argument("--dinic", default="./dinic", help="Dinic executable")
    ap.add_argument("--pushrelabel", default="./pushrelabel", help="Push-Relabel executable")
    ap.add_argument("--out-dir", default="experiment", help="working/output directory")
    ap.add_argument("--families", nargs="+", default=["sparse", "dense", "layered"],
                    choices=["sparse", "dense", "layered"])
    ap.add_argument("--sizes-sparse", type=int, nargs="+")
    ap.add_argument("--sizes-dense", type=int, nargs="+")
    ap.add_argument("--sizes-layered", type=int, nargs="+")
    ap.add_argument("--seeds", type=int, default=10, help="independent graphs per size")
    ap.add_argument("--runs", type=int, default=3, help="timed runs per graph per algorithm")
    ap.add_argument("--warmup", type=int, default=1, help="warm-up runs (not recorded)")
    ap.add_argument("--avg-degree", type=float, default=8.0, help="sparse: edges = avg_degree*V")
    ap.add_argument("--density", type=float, default=0.10, help="dense: edge density")
    ap.add_argument("--layer-width", type=int, default=20, help="layered: nodes per layer")
    ap.add_argument("--layer-degree", type=int, default=8, help="layered: out-degree per node")
    ap.add_argument("--cap-min", type=int, default=1)
    ap.add_argument("--cap-max", type=int, default=100)
    ap.add_argument("--timeout", type=int, default=0,
                    help="seconds allowed per (family,size) batch; 0 = no limit")
    ap.add_argument("--resume", action="store_true", help="skip batches that already have a summary")
    ap.add_argument("--generate-only", action="store_true")
    ap.add_argument("--skip-run", action="store_true", help="only aggregate existing summaries")
    ap.add_argument("--no-plots", action="store_true")
    args = ap.parse_args()

    out = Path(args.out_dir).resolve()
    graphs_dir, res_dir = out / "graphs", out / "results"
    res_dir.mkdir(parents=True, exist_ok=True)
    bench = str(Path(args.benchmark).resolve())

    if args.build_benchmark:
        cmd = ["gcc", "-O2", "-o", bench, args.build_benchmark, "-lm"]
        print("building:", " ".join(cmd))
        subprocess.run(cmd, check=True)

    if not (args.generate_only or args.skip_run):
        for exe in (bench, args.dinic, args.pushrelabel):
            if not os.access(exe, os.X_OK):
                sys.exit(f"error: '{exe}' not found or not executable")

    results = []
    for fam in args.families:
        sizes = getattr(args, f"sizes_{fam}") or DEFAULT_SIZES[fam]
        for n in sizes:
            folder = graphs_dir / fam / f"n{n}"
            raw = res_dir / f"{fam}_n{n}.csv"
            summary = res_dir / f"{fam}_n{n}_summary.csv"
            if not args.skip_run:
                for seed in range(args.seeds):
                    generate(args, fam, n, seed, folder)
                if args.generate_only:
                    print(f"[{fam} n={n}] generated {args.seeds} graphs")
                    continue
                if args.resume and summary.exists():
                    print(f"[{fam} n={n}] summary exists, skipping")
                else:
                    print(f"\n=== {fam} n={n} ===", flush=True)
                    if summary.exists():
                        summary.unlink()
                    cmd = [bench, str(Path(args.dinic).resolve()),
                           str(Path(args.pushrelabel).resolve()), str(raw),
                           "--runs", str(args.runs), "--warmup", str(args.warmup), str(folder)]
                    rc = run_batch(cmd, args.timeout)
                    if rc != 0:
                        print(f"!! {fam} n={n}: benchmark failed ({rc}); skipping this size")
                        continue
            if summary.exists():
                agg = aggregate(fam, n, summary)
                if agg:
                    results.append(agg)

    if args.generate_only or not results:
        return
    cols = []
    for r in results:
        for k in r:
            if k not in cols:
                cols.append(k)
    agg_path = res_dir / "aggregate.csv"
    with agg_path.open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=cols)
        w.writeheader()
        w.writerows(results)
    print_table(results)
    print(f"\naggregate table: {agg_path}")
    if not args.no_plots:
        make_plots(results, res_dir)


if __name__ == "__main__":
    main()

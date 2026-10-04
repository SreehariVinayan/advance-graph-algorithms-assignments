#!/usr/bin/env python3
"""Convert a topology .gml file (nodes/edges, no capacities) into DIMACS max-flow
format with synthetic, degree-aware (gravity-model) capacities.

Usage:
  python gml2dimacs.py africa.gml                  -> africa.max
  python gml2dimacs.py topo_dir/ -o out_dir/       -> batch convert a directory
  python gml2dimacs.py africa.gml --seed 7 --median-cap 30 --max-cap 200
"""
import argparse, math, os, random, re, statistics, sys
from collections import deque

TOKEN = re.compile(r'\[|\]|"[^"]*"|[^\s\[\]]+')


# ---------------------------------------------------------------- GML parsing
def parse_gml(text):
    toks = TOKEN.findall(text)
    pos = 0

    def parse_block():
        nonlocal pos
        items = []
        while pos < len(toks):
            t = toks[pos]
            if t == ']':
                pos += 1
                return items
            key = t
            pos += 1
            val = toks[pos]
            if val == '[':
                pos += 1
                val = parse_block()
            else:
                pos += 1
                val = val.strip('"')
            items.append((key, val))
        return items

    # top level: graph [ ... ]
    while toks[pos] != '[':
        pos += 1
    pos += 1
    return parse_block()


def load_graph(path):
    with open(path, encoding='utf-8', errors='ignore') as f:
        items = parse_gml(f.read())
    nodes, edges = set(), {}
    for k, v in items:
        if k == 'node':
            nodes.add(int(dict(v)['id']))
        elif k == 'edge':
            d = dict(v)
            u, w = int(d['source']), int(d['target'])
            if u == w:                       # drop self loops
                continue
            key = (min(u, w), max(u, w))
            edges[key] = edges.get(key, 0) + 1   # count parallel links
            nodes.update((u, w))
    return nodes, edges


# ------------------------------------------------------------ graph utilities
def adjacency(nodes, edges):
    adj = {n: set() for n in nodes}
    for u, v in edges:
        adj[u].add(v)
        adj[v].add(u)
    return adj


def largest_component(adj):
    seen, best = set(), []
    for s in adj:
        if s in seen:
            continue
        comp, q = [s], deque([s])
        seen.add(s)
        while q:
            x = q.popleft()
            for y in adj[x]:
                if y not in seen:
                    seen.add(y)
                    comp.append(y)
                    q.append(y)
        if len(comp) > len(best):
            best = comp
    return set(best)


def hop_dists(adj, s):
    dist, q = {s: 0}, deque([s])
    while q:
        x = q.popleft()
        for y in adj[x]:
            if y not in dist:
                dist[y] = dist[x] + 1
                q.append(y)
    return dist


def pick_pairs(adj, rng, mode, k, min_deg):
    """Return k (source, sink) pairs.

    Leaves (degree 1) are excluded: a degree-1 terminal caps the max-flow at a
    single link, which makes the instance trivial for any algorithm.
    """
    deg = {n: len(adj[n]) for n in adj}
    d = min_deg
    cand = [n for n in sorted(adj) if deg[n] >= d]
    while len(cand) < 2 and d > 1:          # tiny/sparse graphs: relax threshold
        d -= 1
        cand = [n for n in sorted(adj) if deg[n] >= d]
    cand = sorted(cand, key=lambda n: (-deg[n], n))[:500]   # cap for huge graphs

    if mode == 'random':
        pairs = set()
        tries = 0
        while len(pairs) < k and tries < 100 * k:
            pairs.add(tuple(rng.sample(cand, 2)))
            tries += 1
        return list(pairs)

    if mode == 'hubs':
        from itertools import combinations
        top = cand[:max(2, k + 1)]
        return list(combinations(top, 2))[:k]

    # 'far': among candidates, the farthest pair (in hops) comes first; further
    # pairs are drawn at random from pairs at least half the diameter apart.
    allp = []
    for a in cand:
        dist = hop_dists(adj, a)
        for b in cand:
            if a < b and b in dist:
                allp.append((dist[b], deg[a] + deg[b], a, b))
    allp.sort(reverse=True)
    best = allp[0]
    pairs = [(best[2], best[3])]
    if k > 1:
        eligible = [(a, b) for dd, _, a, b in allp[1:] if dd >= math.ceil(best[0] / 2)]
        rng.shuffle(eligible)
        pairs += eligible[:k - 1]
    return pairs


# ------------------------------------------------------- capacity generation
def synth_capacities(edges, adj, rng, alpha, sigma, median_cap, max_cap):
    """cap(u,v) = (deg_u*deg_v)^alpha * LogNormal(0,sigma) * parallel_links,
    rescaled so the median is `median_cap`, clamped to [1, max_cap]."""
    raw = {}
    for (u, v), mult in edges.items():
        base = (len(adj[u]) * len(adj[v])) ** alpha
        raw[(u, v)] = base * rng.lognormvariate(0.0, sigma) * mult
    med = statistics.median(raw.values())
    return {e: max(1, min(max_cap, int(round(r / med * median_cap))))
            for e, r in raw.items()}


# ----------------------------------------------------------------- conversion
def convert(path, out_path, args):
    rng = random.Random(args.seed)
    nodes, edges = load_graph(path)
    adj = adjacency(nodes, edges)
    comp = largest_component(adj)            # flow only meaningful if connected
    edges = {e: m for e, m in edges.items() if e[0] in comp}
    adj = adjacency(comp, edges)

    pairs = pick_pairs(adj, rng, args.st_mode, args.num_pairs, args.min_deg)
    caps = synth_capacities(edges, adj, rng, args.alpha, args.sigma,
                            args.median_cap, args.max_cap)   # same network for all pairs

    root, ext = os.path.splitext(out_path)
    for i, (s, t) in enumerate(pairs, 1):
        path_i = out_path if len(pairs) == 1 else f'{root}_p{i}{ext}'
        # DIMACS needs nodes numbered 1..N: map original ids to 1..N in sorted order
        new_id = {n: idx for idx, n in enumerate(sorted(comp), 1)}
        src, snk = new_id[s], new_id[t]
        arcs = []
        for (u, v), c in caps.items():       # undirected link -> two arcs
            arcs.append((new_id[u], new_id[v], c))
            arcs.append((new_id[v], new_id[u], c))
        arcs.sort()
        with open(path_i, 'w') as f:
            f.write(f'c Source={src} and Sink={snk}\n')
            f.write(f'c original ids: source={s} (deg {len(adj[s])}) sink={t} (deg {len(adj[t])})\n')
            f.write(f'p max {len(comp)} {len(arcs)}\n')
            f.write(f'n {src} s\n')
            f.write(f'n {snk} t\n')
            for u, v, c in arcs:
                f.write(f'a {u} {v} {c}\n')
        print(f'{os.path.basename(path)}: {len(comp)} nodes, {len(arcs)} arcs, '
              f's={s} t={t} -> {path_i}')


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('input', help='.gml file or directory of .gml files')
    ap.add_argument('-o', '--output', help='output file (single) or directory (batch)')
    ap.add_argument('--seed', type=int, default=42, help='RNG seed (reproducible)')
    ap.add_argument('--alpha', type=float, default=0.5,
                    help='degree exponent of gravity model (default 0.5)')
    ap.add_argument('--sigma', type=float, default=0.3,
                    help='lognormal noise std-dev (default 0.3)')
    ap.add_argument('--median-cap', type=int, default=50, help='median capacity')
    ap.add_argument('--max-cap', type=int, default=1000, help='capacity ceiling')
    ap.add_argument('--st-mode', choices=['far', 'hubs', 'random'], default='far',
                    help='how to choose source/sink (default: far apart)')
    ap.add_argument('--num-pairs', type=int, default=1,
                    help='number of source/sink pairs (one output file each)')
    ap.add_argument('--min-deg', type=int, default=3,
                    help='minimum degree of source/sink candidates (default 3)')
    args = ap.parse_args()

    if os.path.isdir(args.input):
        out_dir = args.output or args.input
        os.makedirs(out_dir, exist_ok=True)
        for fn in sorted(os.listdir(args.input)):
            if fn.lower().endswith('.gml'):
                convert(os.path.join(args.input, fn),
                        os.path.join(out_dir, os.path.splitext(fn)[0] + '.max'), args)
    else:
        out = args.output or os.path.splitext(args.input)[0] + '.max'
        convert(args.input, out, args)


if __name__ == '__main__':
    sys.exit(main())
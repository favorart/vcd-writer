"""Speed of the Python module `vcdwriter` against pyvcd on the features both have.

Usage: python bench/vcd_bench.py [output_dir]   (vcdwriter and pyvcd must be importable)

The scenarios follow bench/vcd_bench.cpp: VARS vectors and VARS scalars change on every one
of STEPS timestamps; the values are precomputed, so the benchmark measures the writers.
"""
import os
import sys
import time

from importlib.metadata import version

import vcd
import vcdwriter

VARS = 1000  # per kind
STEPS = 200
WIDTH = 32
RUNS = 5
DATE = "2026-01-01 00:00:00"


def write(writer, vec_values, sc_values):
    """Register the variables, change them on every step, close the writer."""
    vec, sc = [], []
    for i in range(VARS):
        scope = "top.m" + str(i % 50)
        vec.append(writer.register_var(scope, "v" + str(i), "wire", WIDTH))
        sc.append(writer.register_var(scope, "s" + str(i), "integer", 1))  # a scalar in both
    change = writer.change
    pairs = list(zip(vec, sc))
    for t in range(STEPS):
        vec_row, sc_row = vec_values[t], sc_values[t]
        for i, (v, s) in enumerate(pairs):
            change(v, t, vec_row[i])
            change(s, t, sc_row[i])
    writer.close()


def best_of(make_writer, vec_values, sc_values):
    best = float("inf")
    for _ in range(RUNS):
        t0 = time.perf_counter()
        write(make_writer(), vec_values, sc_values)
        best = min(best, time.perf_counter() - t0)
    return best


def main():
    out_dir = sys.argv[1] if len(sys.argv) > 1 else "."
    ours_file = os.path.join(out_dir, "bench_vcdwriter.vcd")
    pyvcd_file = os.path.join(out_dir, "bench_pyvcd.vcd")
    streams = []

    def ours():
        return vcdwriter.Writer(ours_file, timescale="1 ns", date=DATE)

    def pyvcd():
        if streams:
            streams.pop().close()  # pyvcd does not close the file
        streams.append(open(pyvcd_file, "w", newline="\n"))
        return vcd.VCDWriter(streams[-1], timescale="1 ns", date=DATE)

    ints = [(t * 2654435761) & 0xFFFFFFFF for t in range(STEPS)]
    bits = [format(v, "032b") for v in ints]
    changed = [[(t + i) % STEPS for i in range(VARS)] for t in range(STEPS)]
    scenarios = [
        ("int values, all changed",
         [[ints[k] for k in row] for row in changed], [[k & 1 for k in row] for row in changed]),
        ("str values, all changed",
         [[bits[k] for k in row] for row in changed], [["01"[k & 1] for k in row] for row in changed]),
        ("int values, unchanged",
         [[ints[i % STEPS] for i in range(VARS)]] * STEPS, [[i & 1 for i in range(VARS)]] * STEPS),
    ]

    ops = 2 * VARS * STEPS
    print(f"Python {sys.version.split()[0]}, pyvcd {version('pyvcd')}: "
          f"{VARS} vectors ({WIDTH} bits) + {VARS} scalars, {STEPS} timestamps, {ops} changes, best of {RUNS}")
    print(f"| {'scenario':26} | {'pyvcd':>11} | {'vcdwriter':>11} | {'speedup':>7} |")
    print(f"|{'-' * 28}|{'-' * 12}:|{'-' * 12}:|{'-' * 8}:|")
    for name, vec_values, sc_values in scenarios:
        t_pyvcd = best_of(pyvcd, vec_values, sc_values)
        t_ours = best_of(ours, vec_values, sc_values)
        print(f"| {name:26} | {t_pyvcd * 1e9 / ops:8.0f} ns | {t_ours * 1e9 / ops:8.0f} ns | {t_pyvcd / t_ours:6.1f}x |")
    for f in streams:
        f.close()


if __name__ == "__main__":
    main()

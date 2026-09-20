#!/usr/bin/env python3
"""Times the Red benchmarks against the matching Python programs.

Each benchmark exists twice, once as name.red and once as name.py, doing
the same work. Both are run as separate processes and the fastest of
several runs is reported, which removes most of the noise from other
activity on the machine.

    python3 bench/compare.py --red build/red
"""

import argparse
import os
import platform
import subprocess
import sys
import time

BENCHMARKS = [
    ("fib", "recursive calls, no allocation"),
    ("loop", "tight arithmetic loop"),
    ("string", "building and inspecting short strings"),
    ("alloc", "allocation churn, collector bound"),
    ("method", "method dispatch through inheritance"),
]


def time_command(command, runs):
    best = None
    output = ""
    for _ in range(runs):
        start = time.perf_counter()
        result = subprocess.run(command, capture_output=True, text=True)
        elapsed = time.perf_counter() - start
        if result.returncode != 0:
            raise SystemExit(
                f"{' '.join(command)} failed with {result.returncode}:\n"
                f"{result.stderr}"
            )
        output = result.stdout.strip()
        best = elapsed if best is None else min(best, elapsed)
    return best, output


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--red", default="build/red", help="path to the red binary")
    parser.add_argument("--python", default=sys.executable, help="reference interpreter")
    parser.add_argument("--runs", type=int, default=3, help="runs per benchmark")
    parser.add_argument("--markdown", action="store_true", help="print a markdown table")
    args = parser.parse_args()

    here = os.path.dirname(os.path.abspath(__file__))
    red = os.path.abspath(args.red)

    version = subprocess.run(
        [args.python, "--version"], capture_output=True, text=True
    ).stdout.strip()

    rows = []
    for name, description in BENCHMARKS:
        red_path = os.path.join(here, f"{name}.red")
        py_path = os.path.join(here, f"{name}.py")
        if not os.path.exists(red_path) or not os.path.exists(py_path):
            continue

        red_time, red_out = time_command([red, red_path], args.runs)
        py_time, py_out = time_command([args.python, py_path], args.runs)
        # Both programs must agree, or the comparison is meaningless.
        match = "yes" if red_out == py_out else "NO"
        rows.append((name, description, red_time, py_time, match))

    print(f"machine: {platform.platform()} {platform.machine()}")
    print(f"reference: {version}")
    print(f"runs per benchmark: {args.runs}, fastest reported\n")

    if args.markdown:
        print("| benchmark | what it measures | red | python | ratio |")
        print("|---|---|--:|--:|--:|")
        for name, description, red_time, py_time, match in rows:
            note = "" if match == "yes" else " (output differs)"
            print(
                f"| {name} | {description}{note} | {red_time:.2f}s | "
                f"{py_time:.2f}s | {red_time / py_time:.2f}x |"
            )
    else:
        print(f"{'benchmark':10} {'red':>8} {'python':>8} {'ratio':>7}  same output")
        print(f"{'-' * 10} {'-' * 8:>8} {'-' * 8:>8} {'-' * 7:>7}  -----------")
        for name, _, red_time, py_time, match in rows:
            print(
                f"{name:10} {red_time:7.2f}s {py_time:7.2f}s "
                f"{red_time / py_time:6.2f}x  {match}"
            )
    print("\nA ratio below 1.00 means Red was faster.")
    return 0


if __name__ == "__main__":
    sys.exit(main())

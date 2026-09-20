#!/usr/bin/env python3
"""Runs the Red conformance tests.

Each test is a .red file. Expected output is written in the file itself as
comments, so a test is readable on its own:

    print(1 + 1);            // expect: 2
    // expect runtime error: Division by zero.
    // expect compile error: Expect ';'

Lines marked `expect` must appear on stdout in the same order. An expected
error is matched as a substring of stderr.
"""

import argparse
import os
import re
import subprocess
import sys

EXPECT = re.compile(r"//\s*expect:\s?(.*)$")
EXPECT_RUNTIME_ERROR = re.compile(r"//\s*expect runtime error:\s?(.*)$")
EXPECT_COMPILE_ERROR = re.compile(r"//\s*expect compile error:\s?(.*)$")

GREEN = "\033[32m"
RED = "\033[31m"
DIM = "\033[2m"
OFF = "\033[0m"


def parse_expectations(path):
    output = []
    runtime_errors = []
    compile_errors = []
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            line = line.rstrip("\n")
            match = EXPECT_RUNTIME_ERROR.search(line)
            if match:
                runtime_errors.append(match.group(1))
                continue
            match = EXPECT_COMPILE_ERROR.search(line)
            if match:
                compile_errors.append(match.group(1))
                continue
            match = EXPECT.search(line)
            if match:
                output.append(match.group(1))
    return output, runtime_errors, compile_errors


def run_one(red, path, extra_args):
    expected, runtime_errors, compile_errors = parse_expectations(path)
    path = os.path.abspath(path)
    command = [red] + extra_args + [path]
    try:
        result = subprocess.run(
            command,
            capture_output=True,
            text=True,
            timeout=120,
            cwd=os.path.dirname(os.path.abspath(path)),
        )
    except subprocess.TimeoutExpired:
        return ["timed out after 120 seconds"]

    failures = []
    actual = [line for line in result.stdout.split("\n")]
    if actual and actual[-1] == "":
        actual.pop()

    for index, want in enumerate(expected):
        if index >= len(actual):
            failures.append(f"line {index + 1}: expected {want!r}, got nothing")
        elif actual[index] != want:
            failures.append(
                f"line {index + 1}: expected {want!r}, got {actual[index]!r}"
            )
    if len(actual) > len(expected):
        for extra in actual[len(expected):]:
            failures.append(f"unexpected output {extra!r}")

    for want in runtime_errors:
        if want not in result.stderr:
            failures.append(f"expected runtime error containing {want!r}")
    for want in compile_errors:
        if want not in result.stderr:
            failures.append(f"expected compile error containing {want!r}")

    if runtime_errors and result.returncode != 70:
        failures.append(f"expected exit code 70, got {result.returncode}")
    elif compile_errors and result.returncode != 65:
        failures.append(f"expected exit code 65, got {result.returncode}")
    elif not runtime_errors and not compile_errors and result.returncode != 0:
        failures.append(
            f"expected exit code 0, got {result.returncode}\n{result.stderr.strip()}"
        )
    return failures


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--red", required=True, help="path to the red binary")
    parser.add_argument("--tests", required=True, help="directory of .red tests")
    parser.add_argument(
        "--gc-stress",
        action="store_true",
        help="run every test with a collection before each allocation",
    )
    parser.add_argument("--filter", default="", help="only run matching names")
    args = parser.parse_args()

    # Tests run with their own directory as the working directory so that
    # imports and temporary files stay next to them, which means the
    # interpreter has to be named absolutely.
    red = os.path.abspath(args.red)
    extra = ["--gc-stress"] if args.gc_stress else []

    paths = []
    for root, _, names in os.walk(args.tests):
        for name in sorted(names):
            if not name.endswith(".red"):
                continue
            # Files under modules/ are imported by other tests, not run on
            # their own.
            if os.path.basename(root) == "modules":
                continue
            if args.filter and args.filter not in name:
                continue
            paths.append(os.path.join(root, name))
    paths.sort()

    passed = 0
    failed = []
    for path in paths:
        name = os.path.relpath(path, args.tests)
        failures = run_one(red, path, extra)
        if failures:
            failed.append((name, failures))
            print(f"{RED}FAIL{OFF} {name}")
            for failure in failures:
                print(f"     {DIM}{failure}{OFF}")
        else:
            passed += 1
            print(f"{GREEN}ok{OFF}   {name}")

    total = passed + len(failed)
    mode = " (gc stress)" if args.gc_stress else ""
    print(f"\n{passed}/{total} tests passed{mode}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())

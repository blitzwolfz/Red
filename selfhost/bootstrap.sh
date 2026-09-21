#!/usr/bin/env bash
#
# Bootstraps the Red compiler that is written in Red, and checks that it
# reproduces itself.
#
#   selfhost/bootstrap.sh [path-to-red] [output-directory]
#
# Defaults: build/red and build/selfhost.
#
# The test is the usual one for a self-hosting compiler:
#
#   A   the Red compiler, compiled by the C++ compiler
#   B   the Red compiler, compiled by A
#   C   the Red compiler, compiled by B
#
# B and C must be byte for byte identical. If they are, the compiler
# reproduces itself and nothing about the C++ compiler is baked into the
# result. A is also compared against B, which is a stronger claim: the two
# compilers agree on every byte for this input.
#
# Afterwards the whole test suite is compiled with B and run, and every
# other Red program in the repository is compiled with both compilers and
# the outputs are compared.

set -euo pipefail

RED=${1:-build/red}
OUT=${2:-build/selfhost}
SOURCE=selfhost/redc.red

if [ ! -x "$RED" ]; then
  echo "No red binary at '$RED'. Build one with ./setup.sh first." >&2
  exit 1
fi

RED=$(cd "$(dirname "$RED")" && pwd)/$(basename "$RED")
mkdir -p "$OUT"

bar() { printf '\n== %s ==\n' "$1"; }

bar "stage 1: the C++ compiler compiles the Red compiler"
"$RED" compile "$SOURCE" -o "$OUT/redc-a.redc"

bar "stage 2: A compiles the Red compiler"
"$RED" "$OUT/redc-a.redc" compile "$SOURCE" -o "$OUT/redc-b.redc"

bar "stage 3: B compiles the Red compiler"
"$RED" "$OUT/redc-b.redc" compile "$SOURCE" -o "$OUT/redc-c.redc"

bar "fixpoint"
if cmp -s "$OUT/redc-b.redc" "$OUT/redc-c.redc"; then
  echo "B and C are identical. The compiler reproduces itself."
else
  echo "B and C differ. The compiler does not reproduce itself." >&2
  cmp "$OUT/redc-b.redc" "$OUT/redc-c.redc" >&2 || true
  exit 1
fi

if cmp -s "$OUT/redc-a.redc" "$OUT/redc-b.redc"; then
  echo "A and B are identical too: the two compilers agree byte for byte."
else
  echo "A and B differ. That is allowed, but worth knowing about." >&2
fi

bar "every Red program in the repository, both compilers"
same=0
differ=0
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT
while IFS= read -r file; do
  name=${file//\//_}
  # Programs that are meant not to compile are skipped: what matters
  # there is the message, which is checked by the test suite.
  "$RED" compile "$file" -o "$scratch/$name.cxx" >/dev/null 2>&1 || continue
  "$RED" "$OUT/redc-b.redc" compile "$file" -o "$scratch/$name.red" >/dev/null
  if cmp -s "$scratch/$name.cxx" "$scratch/$name.red"; then
    same=$((same + 1))
  else
    differ=$((differ + 1))
    echo "differs: $file" >&2
  fi
done < <(find tests examples bench selfhost legacy lib -name '*.red' | sort)
echo "$same identical, $differ different"
[ "$differ" -eq 0 ] || exit 1

bar "the test suite, compiled by the self-hosted compiler"
"$RED" test tests --compiler "$OUT/redc-b.redc"

bar "done"
echo "Stage 3 of docs/bootstrapping.md holds."

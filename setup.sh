#!/usr/bin/env bash
#
# Builds Red and checks that it works.
#
#   ./setup.sh                    build into build/ and run the tests
#   ./setup.sh --quick            build only, skip the tests
#   ./setup.sh --debug            build with the sanitizers turned on
#   ./setup.sh --install PREFIX   also install into PREFIX
#   ./setup.sh --help
#
# Needs CMake 3.16 or newer and a compiler with C++20. Python 3 is optional
# and only used by the benchmark comparison. A JDK is optional too: without
# one the v1 interpreter is skipped and everything else still works.

set -euo pipefail

cd "$(dirname "$0")"

BUILD_DIR=build
BUILD_TYPE=Release
SANITIZE=OFF
RUN_TESTS=1
INSTALL_PREFIX=""

while [ $# -gt 0 ]; do
  case "$1" in
    --quick) RUN_TESTS=0; shift ;;
    --debug)
      BUILD_DIR=build-asan
      BUILD_TYPE=Debug
      SANITIZE=ON
      shift
      ;;
    --install)
      if [ $# -lt 2 ]; then echo "--install needs a prefix" >&2; exit 64; fi
      INSTALL_PREFIX=$2
      shift 2
      ;;
    --help|-h)
      # The comment block at the top of this file is the help text.
      awk 'NR > 1 && /^#/ { sub(/^# ?/, ""); print; next } NR > 1 { exit }' "$0"
      exit 0
      ;;
    *) echo "Unknown option '$1'. Try --help." >&2; exit 64 ;;
  esac
done

say() { printf '\n== %s ==\n' "$1"; }
have() { command -v "$1" >/dev/null 2>&1; }

# ---------------------------------------------------------------------
say "checking what is here"

missing=0
for tool in cmake; do
  if have "$tool"; then
    echo "  $tool      $(command -v "$tool")"
  else
    echo "  $tool      MISSING" >&2
    missing=1
  fi
done

if have c++; then
  echo "  c++        $(c++ --version | head -1)"
elif have g++; then
  echo "  g++        $(g++ --version | head -1)"
else
  echo "  c++        MISSING" >&2
  missing=1
fi

if have python3; then
  echo "  python3    $(python3 --version)"
else
  echo "  python3    not found. Only the benchmark comparison needs it"
fi

if have javac; then
  echo "  javac      found, so the v1 interpreter will be built too"
else
  echo "  javac      not found, so 'red legacy' will report that it is missing"
fi

if [ "$missing" -ne 0 ]; then
  cat >&2 <<'HELP'

Install what is missing and run this again.

  Debian, Ubuntu   sudo apt install cmake g++ python3
  Fedora           sudo dnf install cmake gcc-c++ python3
  macOS            xcode-select --install && brew install cmake
  Arch             sudo pacman -S cmake gcc python
HELP
  exit 1
fi

# ---------------------------------------------------------------------
say "building"
cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE" -DRED_SANITIZE="$SANITIZE"
cmake --build "$BUILD_DIR" -j

RED=$BUILD_DIR/red
"$RED" version

# ---------------------------------------------------------------------
if [ "$RUN_TESTS" -eq 1 ]; then
  say "tests"
  "$RED" test tests
fi

# ---------------------------------------------------------------------
if [ -n "$INSTALL_PREFIX" ]; then
  say "installing into $INSTALL_PREFIX"
  mkdir -p "$INSTALL_PREFIX/bin" "$INSTALL_PREFIX/lib/red"
  install -m 755 "$RED" "$INSTALL_PREFIX/bin/red"

  # Libraries and extensions go where the interpreter looks for them:
  # lib/red, one directory up from the binary. docs/libraries.md has the
  # whole search order.
  for file in lib/*.red; do
    [ -e "$file" ] && install -m 644 "$file" "$INSTALL_PREFIX/lib/red/"
  done
  for file in "$BUILD_DIR"/*.so; do
    [ -e "$file" ] && install -m 755 "$file" "$INSTALL_PREFIX/lib/red/"
  done
  if [ -e "$BUILD_DIR/red-legacy.jar" ]; then
    mkdir -p "$INSTALL_PREFIX/share/red"
    install -m 644 "$BUILD_DIR/red-legacy.jar" "$INSTALL_PREFIX/share/red/"
  fi
  echo "installed: $INSTALL_PREFIX/bin/red"
fi

# ---------------------------------------------------------------------
say "ready"
cat <<NEXT
  $RED examples/tour.red        every part of the language
  $RED repl                     an interactive prompt
  $RED bench                    the benchmark programs

  docs/guide.md                  writing a whole program, start to finish
  docs/language.md               the language reference
  docs/libraries.md              writing a library, in Red or in C++
NEXT

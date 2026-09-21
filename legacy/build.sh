#!/bin/sh
# Builds the v1 Java interpreter into build/red-legacy.jar.
#
# CMake does this as part of the normal build when a JDK is present. This
# script is for building it on its own.
set -e

here=$(cd "$(dirname "$0")" && pwd)
root=$(dirname "$here")
out="$root/build"
classes="$out/legacy-classes"

if ! command -v javac > /dev/null 2>&1; then
  echo "javac not found. Install a JDK, or skip the legacy interpreter." >&2
  exit 1
fi

mkdir -p "$classes"
javac -d "$classes" "$here"/redlang/*.java
jar cf "$out/red-legacy.jar" -C "$classes" .

echo "built $out/red-legacy.jar"
echo "run a v1 script with: red legacy legacy/main.red"

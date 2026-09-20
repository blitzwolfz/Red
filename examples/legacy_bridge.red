// Running v1 programs from v2.
//
//   red examples/legacy_bridge.red
//
// The original Red was a tree-walking interpreter written in Java. It is
// still in legacy/, still builds, and still runs. v2 does not reimplement
// its behaviour. It starts the old interpreter as a child process
// instead, which keeps the two versions honestly separate.
//
// Three ways in:
//   red legacy script.red   from the command line
//   legacy(path)            run it, output goes straight to the terminal
//   legacy_output(path)     run it and capture its output as a string

if (!legacy_available()) {
  print("The v1 interpreter is not built.");
  print("Build it with legacy/build.sh, or set RED_LEGACY_JAR.");
  exit(1);
}

const script = "legacy/main.red";
if (!exists(script)) {
  print("Run this from the top of the repository.");
  exit(1);
}

print("--- captured from v1 ---");
const output = legacy_output(script);
const lines = output.split("\n");
print("v1 printed ${lines.len() - 1} lines");
for (let i = 0; i < min(4, lines.len()); i = i + 1) {
  print("  ${lines[i]}");
}
print("  ...");

// The captured output is an ordinary v2 string, so v2 can work with it.
let roars = 0;
for (let i = 0; i < lines.len(); i = i + 1) {
  if (lines[i].contains("ROOOOOOAR")) { roars = roars + 1; }
}
print("v1 roared ${roars} times");

print("");
print("--- v1 writing straight to the terminal ---");
const code = legacy(script);
print("v1 exited with ${code}");

print("");
print("--- the same ideas in v2 ---");
// v1 had no arrays, no maps, no string interpolation and no way to catch
// an error. The rewrite of this program is examples/tour.red.
class Animal {
  init(kind) { this.kind = kind; }
  me() { return "I am a ${this.kind}"; }
  roar() { return "ROOOOOOAR"; }
}
const tiger = Animal("Tiger");
print(tiger.me());
print(tiger.roar());

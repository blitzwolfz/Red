// Running another program.
//
// Every command here is one that any Unix has, so the test does not
// depend on what else is installed.

// run() takes the program and its arguments as an array, and gives back
// what it produced.
const hello = run(["echo", "hello", "world"]);
print(hello["code"]);       // expect: 0
print(hello["out"].trim()); // expect: hello world
print(hello["err"]);        // expect:

// Standard input, when there is any to give.
print(run(["tr", "a-z", "A-Z"], "shout")["out"]); // expect: SHOUT

// The two streams stay apart, and the exit code comes back as it was.
const mixed = run(["sh", "-c", "echo out; echo err 1>&2; exit 3"]);
print(mixed["code"]);       // expect: 3
print(mixed["out"].trim()); // expect: out
print(mixed["err"].trim()); // expect: err

// Nothing in the array is expanded, split or handed to a shell, so a
// value from outside the program cannot turn into another command.
print(run(["echo", "; rm -rf /"])["out"].trim()); // expect: ; rm -rf /
print(run(["echo", "$HOME"])["out"].trim());      // expect: $HOME
print(run(["echo", "*"])["out"].trim());          // expect: *

// shell() is the one that does hand the text to /bin/sh, for when a
// pipeline is what was wanted.
print(shell("printf 'b\\na\\nc\\n' | sort | tr -d '\\n'")["out"]); // expect: abc
print(shell("exit 7")["code"]);                                    // expect: 7

// A program that is not there is a failure to start, not an exit code.
try {
  run(["no-such-program-anywhere-at-all"]);
} catch (e: "process") {
  print(e.message.starts_with("run() could not start")); // expect: true
}

// which() finds a program the same way the shell does.
print(which("sh") != nil);                              // expect: true
print(which("sh").ends_with("/sh"));                    // expect: true
print(which("no-such-program-anywhere-at-all") == nil); // expect: true
print(which("/bin/sh"));                                // expect: /bin/sh
print(which("/no/such/path") == nil);                   // expect: true

// A program that is killed reports the signal the way a shell does.
print(run(["sh", "-c", "kill -TERM $$"])["code"]); // expect: 143

// Output larger than one pipe buffer still all arrives, on both streams
// at once, which is what a single read loop over the two is for.
const big = run(["sh", "-c", "yes hello 2>/dev/null | head -20000; yes oops 2>/dev/null | head -20000 1>&2"]);
print(big["out"].split("\n").len()); // expect: 20001
print(big["err"].split("\n").len()); // expect: 20001

// An empty command is refused rather than guessed at.
try {
  run([]);
} catch (e: "process") {
  print(e.message); // expect: run() needs a program to run.
}

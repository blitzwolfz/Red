# Bootstrapping

A goal for Red is to stop depending on C++. This file describes what that
would mean, what is already in place, and what is still missing.

Nothing here is built yet. This is a plan, not a status report.

## What "without C++" means

Red has two halves:

- the **compiler**, which turns source into bytecode
- the **virtual machine**, which runs that bytecode

Both are C++ today. They can be removed in that order, and they need very
different amounts of work.

Replacing the compiler is the normal meaning of self-hosting. A compiler
written in Red, run on the current Red, producing the same bytecode. After
that the C++ compiler can be deleted and the C++ that remains is only the
virtual machine.

Replacing the virtual machine is a much larger job, because something has
to execute instructions. That something is either machine code that Red
generates, or a runtime written in a smaller language. Stage 4 below says
more.

## What already helps

Three decisions in v2 were made with this in mind.

**The bytecode is a documented format.** [bytecode.md](bytecode.md)
describes every instruction and every operand. It carries a version
number in `src/common.h`. A second compiler can target it without reading
the C++.

**Constant indexes are one size.** Two bytes everywhere, with no short
form. A compiler that emits this does not have to decide between forms or
patch sizes afterwards. That removes a whole class of work from a
self-hosted compiler.

**The compiler is a single pass with no tree.** It is about 1200 lines and
it holds very little state: a token, the previous token, a stack of
function states and a stack of loop states. That is a realistic amount of
code to rewrite in a language as small as Red.

## What Red is missing

The compiler produces bytes. Red cannot work with bytes properly yet.
These gaps have to close first.

Speed is not one of them. That was the early assumption, and measuring it
showed the opposite. Interning is slow for many *distinct* strings, which
is what the `string` benchmark creates, and a win for a small vocabulary
used over and over, which is what a compiler has. On compiler shaped work
Red matches CPython:

| Workload | Red |
|---|--:|
| Scan 116K characters one at a time and classify each | 10.9ms |
| The same in CPython 3.14 | 11.5ms |
| Accumulate 20,000 short strings a character at a time | 16ms |
| Push 500,000 bytes into an array | 25ms |
| 300,000 keyword lookups in a map | 28ms |

Single character indexing is nearly free for the same reason: there are
only so many distinct characters, so `source[i]` finds one already in the
intern table instead of allocating.

| Gap | Why the compiler needs it |
|---|---|
| Bitwise operators | Splitting a two byte operand into two bytes needs `>>` and `&`. |
| Integer semantics | Red has one number type, a float. Byte values need defined truncation and wrapping. |
| A byte array type | Building `code` needs a growable array of values in 0 to 255, not a string. |
| Binary safe file output | `write_file` writes text. Writing a compiled file needs raw bytes. |
| A compiled file format | There is no way to save a chunk and load it back. |
| A way to build a byte | `chr(n)` and `code_at(i)`. Strings already carry arbitrary bytes and `write_file` is binary safe, but there is no way to turn the number 200 into a character. This is the one gap that blocks writing a compiled file at all. |

## Stages

### Stage 1: a compiled file format

Define `.redc`: a header with the format version, then the chunk tree.
Add two things to the C++ side:

- `red compile file.red -o file.redc`
- `red run file.redc`, which loads a chunk instead of compiling one

This is useful on its own, because it makes start-up faster. It is
required for every later stage, because it is the only way a compiler
written in Red can hand its output to the virtual machine.

**Done when** a `.redc` file produced from any test in `tests/` runs and
gives the same output as the source did.

### Stage 2: the missing language pieces

Add bitwise operators, a `bytes` type, and binary file input and output.
Keep them small and boring. They are not interesting features, they are
the tools the compiler needs.

**Done when** a Red program can build a `.redc` file by hand, byte by
byte, and the virtual machine runs it.

### Stage 3: the self-hosted compiler

Write the scanner, the compiler and the `.redc` writer in Red. Port them
from the C++ rather than redesigning them. The shape of the current
compiler is deliberately plain so that this port is mechanical.

The test is the usual one for a self-hosting compiler:

1. Compile the Red compiler with the C++ compiler. Call the result **A**.
2. Compile the Red compiler again, using **A**. Call the result **B**.
3. Compile the Red compiler a third time, using **B**. Call it **C**.
4. **B** and **C** must be byte for byte identical.

If they are, the compiler reproduces itself, and the C++ compiler is no
longer needed. It stays in the repository as the thing that started the
chain, the same way `legacy/` stays.

**Done when** step 4 passes and the full test suite passes using **B**.

### Stage 4: the virtual machine

After stage 3, the C++ that remains is the virtual machine, the collector
and the standard library. Removing it is a different kind of problem,
because something still has to run instructions. Three ways to go, in
order of how much they actually deliver:

**Port the virtual machine to C.** This does not reach the goal, but it
shrinks the substrate a long way, and C is available in more places than
C++ is. The collector and the value layout are already close to C, because
they use raw pointers and manual lifetimes on purpose. The parts that
would need work are the object types that hold `std::vector`,
`std::string` and `std::deque`.

**Write a native code backend.** Add a compiler pass, in Red, that turns
bytecode into machine code for one architecture. Red would then generate
its own executables. The C++ virtual machine would still be needed to run
the compiler the first time, which is the same chicken and egg problem
that stage 3 solves, one level down. This is the honest route to "no C++",
and it is a much bigger project than everything above it put together.

**Keep the virtual machine and call the goal met at stage 3.** Most
self-hosted languages stop here. The compiler is written in itself, the
runtime is not. Saying so plainly is better than claiming more.

## Order of work

Stages 1 and 2 are small and useful on their own. Stage 3 is the real
milestone, and the one worth aiming at. Stage 4 should only start once
stage 3 is finished and stable.

The thing to protect along the way is the bytecode format. Every change to
it makes the self-hosted compiler and the C++ compiler drift apart. Once
stage 3 starts, the format version in `src/common.h` should change rarely,
and every change should be written down.

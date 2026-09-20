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

One thing.

| Gap | State |
|---|---|
| Bitwise operators | **Done.** `&` `\|` `^` `~` `<<` `>>` on 32 bit integers. |
| Turning a number into a byte | **Done.** `chr(n)`, `text.code_at(i)`, `text.bytes()`. |
| Binary safe file output | **Done.** All 256 byte values round trip through a file. |
| Named constants | **Done.** `enum`, with members that index arrays, key maps and print by name. |
| Selective error handling | **Done.** Errors carry a kind, and a `try` can have several filtered `catch` clauses. |
| Binding several names at once | **Done.** Array, map and instance patterns, nested, with rest. |
| Enough call depth | **Done.** 1024 frames, with a stack check before each call. |
| A compiled file format | **Still missing.** Stage 1 below, and now the only thing in the way. |

### The rehearsal

[`examples/mini_compiler.red`](../examples/mini_compiler.red) is a
working compiler and virtual machine for arithmetic, written in Red. It
is small on purpose, but it is built the same way the real one will be,
and it uses every part of the language the port depends on:

- an `enum` for token kinds and another for opcodes, used as array
  indexes and as `switch` cases
- a Pratt rule table: an array indexed by a token kind's value, holding
  bound methods as values
- `value >> 8 & 255` and `value & 255` to pack a two byte operand
- `chr()` to turn the finished code into bytes, then `write_file` and
  `bytes()` to read it back unchanged
- destructuring to unpack a rule and a compiler's result
- error kinds to tell a bad input program from a bug in the compiler

It runs. So the language is not what is in the way.

### Not blocking, but worth having first

- `finally`, for cleanup that has to happen on both paths.
- String padding, so a disassembler can line its columns up without
  building spaces by hand.
- A set type. A map with ignored values works, and that is what the C++
  compiler's own `constGlobals_` amounts to.

### Speed is not a gap

That was the early assumption, and measuring it showed the opposite.
Interning is slow for many *distinct* strings, which is what the `string`
benchmark creates, and a win for a small vocabulary used over and over,
which is what a compiler has. On compiler shaped work Red matches
CPython:

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

## Stages

### Stage 1: a compiled file format

This is now the only thing blocking stage 3.

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

**Finished.** Two rounds of it.

The first round added the tools: bitwise operators, `chr`, `code_at` and
`bytes`, and binary file input and output. Alongside them went `for ...
in`, `switch`, compound assignment and default and rest parameters,
because the port in stage 3 is several thousand lines of Red and those
decide whether it is bearable to write.

The second round added the three that shape how the port is organised:
`enum`, so a token kind prints as `TokenType.Fun` rather than `38`;
filtered `catch` clauses, so the compiler can separate a bad input
program from a bug in itself; and destructuring, so a rule table entry
or a multi-part result unpacks in one line.

What is left of this stage is the acceptance test itself: a Red program
that builds a `.redc` file byte by byte and has the virtual machine run
it. That needs stage 1 first.
[`examples/mini_compiler.red`](../examples/mini_compiler.red) is the
rehearsal for it.

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

Stage 2 is finished. Stage 1 is small and useful on its own, and is now
the only thing in the way. Stage 3 is the real milestone, and the one
worth aiming at. Stage 4 should only start once stage 3 is finished and
stable.

Stage 1 was deliberately left until after the language changes. Writing
the serialiser first would have frozen the opcode list exactly when it
was about to gain fifteen instructions across two rounds.

That is also the thing to watch from here. The format is at version 3,
having moved three times while the language was being filled in. Once the
port starts, it should stop moving.

The thing to protect along the way is the bytecode format. Every change to
it makes the self-hosted compiler and the C++ compiler drift apart. Once
stage 3 starts, the format version in `src/common.h` should change rarely,
and every change should be written down.

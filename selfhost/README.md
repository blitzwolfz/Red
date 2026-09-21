# The Red compiler, written in Red

[`redc.red`](redc.red) is a compiler for Red, written in Red. It reads the
same language, emits the same bytecode, and writes the same `.redc` files
as the C++ compiler in [`src/`](../src). It reproduces itself, which is
what stage 3 of [docs/bootstrapping.md](../docs/bootstrapping.md) asked
for.

```bash
red selfhost/redc.red compile examples/tour.red -o tour.redc
red tour.redc
```

## Bootstrapping it

```bash
selfhost/bootstrap.sh
```

That script does the whole thing and takes about a second:

| Stage | What it does |
|---|---|
| A | the C++ compiler compiles `redc.red` |
| B | A compiles `redc.red` |
| C | B compiles `redc.red` |

**B and C must be identical.** If they are, the compiler reproduces
itself: the output no longer depends on which compiler started the chain.

The script then checks two more things that the classic test does not.
Every Red program in the repository is compiled by both compilers and the
results compared, and the whole conformance suite is compiled by B and
run. Today all three pass, and so does the stronger claim that A and B are
identical too: the C++ compiler and the Red compiler produce the same
bytes for every input in the repository.

It compiles itself, 2,800 lines, in about 80ms; the C++ compiler does the
same file in 3.6ms.

To run just the suite against the self-hosted compiler:

```bash
red test tests --compiler selfhost/redc.red
```

## Why one file

`import` is resolved when a program runs, not when it is compiled. A
compiler split across modules would still pull its own parts through the
C++ compiler at start-up, and the bootstrap would prove nothing about
them. One file compiles to one chunk that depends on nothing, so B really
is the whole compiler.

## How it maps onto the C++

It is a port, not a redesign. Where the C++ does something in a surprising
order, this does it in the same surprising order, because the two are
compared byte for byte.

| In `redc.red` | Ported from |
|---|---|
| `Scanner` | [`src/scanner.cpp`](../src/scanner.cpp) |
| `Compiler` | [`src/compiler.cpp`](../src/compiler.cpp) |
| `Proto`, `Constant`, `EnumDef` | `Chunk` and `ObjFunction` in [`src/chunk.h`](../src/chunk.h), [`src/object.h`](../src/object.h) |
| `Writer`, `writeCompiled` | the writing half of [`src/serialize.cpp`](../src/serialize.cpp) |

Three details are worth a moment.

**Numbers agree because both sides call the same conversion.** The scanner
turns a literal into a double with `num()`, which is `strtod`, which is
what the C++ scanner uses. So the two compilers never disagree about what
`0.1` means.

**Constant folding agrees because Red's operators are the ones being
ported.** `a & b` in Red already truncates to 32 bits the way
`toInt32()` does, and `%` is already `fmod`, so the folder in `redc.red`
is the C++ folder with the casts removed, not a reimplementation of it.

**Writing a double is the one piece with no counterpart.** The C++ writer
copies the eight bytes straight out of the value. Red cannot look at a
number's bits, so `doubleBytes()` recovers them with arithmetic: it scales
the value into `[1, 2)` by halving or doubling, which is exact, then
multiplies out the fraction. Subnormals are handled by shifting the
fraction down to the fixed `2^-1022` scale instead. `tests/numbers.red`
covers the corners.

## What is still C++

The virtual machine, the collector and the standard library. That is
stage 4, and [docs/bootstrapping.md](../docs/bootstrapping.md#stage-4-the-virtual-machine)
sets out the three ways to approach it.

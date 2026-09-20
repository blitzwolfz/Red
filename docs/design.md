# Design notes

This file explains why Red v2 is built the way it is. It covers the
choices that were not obvious, and the ones that could have gone the
other way.

## Why a rewrite

v1 was a tree-walking interpreter in Java. It worked. It had closures,
classes and inheritance. But it did not touch the parts of language
implementation that are hard: memory layout, allocation, collection and
concurrency. The Java runtime did all of that.

v2 moves that work into the project. Red now owns its heap, its value
layout, its collector and its threading model. The old interpreter is
still in `legacy/`, and v2 can still run it. See
[the legacy bridge](#the-legacy-bridge).

## Pipeline

```
source  ->  Scanner  ->  Compiler  ->  Chunk  ->  VM
            tokens       bytecode     code +     stack
                                      constants  machine
```

There is no syntax tree. The compiler reads one token at a time and emits
bytecode as it goes. v1 built a tree, resolved it in a second pass, then
walked it. v2 does all of that in one pass.

The cost of a single pass is that the compiler cannot look ahead or look
back. Two things had to be designed around this:

- A function is marked as defined before its body is compiled. Without
  that, a function could not call itself.
- A `for` loop compiles its increment clause before its body, then jumps
  over it. The increment has to run after the body, so the code jumps
  forward into the body and the body jumps back to the increment.
- `for (let x in items)` and `for (let i = 0; ...)` cannot be told apart
  until after the name, because `in` only appears once the name has been
  read. The compiler reads the name first and then decides, rather than
  looking ahead.

The gain is that compiling is fast and there is no tree to allocate, walk
or free.

## Value layout

A `Value` is a tagged struct: a one byte type tag and an eight byte
payload. The whole thing is sixteen bytes.

```c++
struct Value {
  ValueType type;            // Nil, Bool, Number or Obj
  union {
    bool boolean;
    double number;
    Obj* obj;
  } as;
};
```

The alternative was NaN boxing. A `double` has about 2^51 spare bit
patterns that all mean "not a number". Pointers and small values fit in
those patterns, so every value can be eight bytes instead of sixteen.

Red uses the tagged struct. The reasons:

- It is correct by construction. NaN boxing needs care around signalling
  NaN values, pointer width, and platforms where a pointer does not fit
  in 48 bits.
- Half the value size is not the bottleneck here. The benchmarks show the
  interpreter is limited by dispatch and by allocation, not by the size of
  a stack slot.
- It is readable. A reviewer can see what a value is without decoding bit
  patterns.

NaN boxing is a reasonable next change. `Value` is used through small
inline helpers (`isNumber`, `asNumber`, `numberValue` and so on), so the
representation can change without touching the rest of the code. That was
the point of writing it that way.

## Objects and the heap

Everything larger than a value lives on the heap behind an `Obj` header:

```c++
struct Obj {
  ObjType type;
  bool isMarked;
  Obj* next;
};
```

`next` threads every live object onto one list. That list is what the
sweep phase walks. An intrusive list was chosen over a separate registry
because it costs one pointer per object and needs no allocation of its
own.

Objects are created with `new` and released with `delete`, one type at a
time, rather than from a raw byte arena with placement `new`. Several
object types hold `std::vector`, `std::string` or `std::deque` members,
and those need real constructors and destructors. Running them by hand
over an arena would have added a second lifetime system next to the
collector, for no gain at this size. `Runtime::bytesAllocated` tracks the
totals, so the collector still sees the heap it is managing.

Strings are interned. Two equal strings are always the same pointer, so
string equality and table lookup are pointer compares. The cost is a hash
of every new string and a lookup in the intern table. The `string`
benchmark shows that cost clearly: Red is about five times slower than
CPython there, and much closer everywhere else.

## Garbage collector

Mark and sweep, stop the world, precise.

**Precise, not conservative.** The collector knows exactly where every
root is. It does not scan the C++ stack looking for things that might be
pointers. Roots come from four places:

1. Every task's value stack, call frames and open upvalues.
2. Module globals and the table of built-in functions.
3. Temporary roots, for objects that exist only in a C++ local.
4. Running tasks, which are live even when no value refers to them.

**Temporary roots are the part that bites.** A native function that
builds an object and then allocates again can lose the first object,
because nothing the collector can see refers to it yet. `GCRoot` is a
small guard that pins an object for the length of a C++ scope:

```c++
ObjArray* array = runtime.newArray();
GCRoot arrayRoot(runtime, (Obj*)array);   // safe to allocate from here
```

Two real bugs of this shape were found while writing the runtime, both by
running the test suite with `--gc-stress`. That flag collects before every
single allocation, which turns a rare crash into one that happens on the
first try. It is slow and it is worth it.

**The intern table is weak.** It holds every live string, but it must not
keep any string alive by itself. After marking, entries whose key was not
marked are dropped, and then the sweep runs.

**When it runs.** Collection is triggered by allocation, when the heap
passes a threshold. After each collection the threshold is set to twice
the surviving bytes, with a floor of one megabyte. A larger factor means
fewer pauses and more memory.

**What was not built.** A generational collector would help, because most
Red objects die young. It needs a write barrier on every field store,
which is a real cost on the hot path and a real source of bugs. A
compacting collector would need every pointer to be updatable, which the
current design does not allow, because raw `Value*` slots point into task
stacks. Both are genuine next steps, not oversights.

**Why not reference counting.** It is simpler and the pauses are smaller.
But it cannot collect cycles, and cycles are easy to make here: a closure
that captures a variable holding that closure, or two instances pointing
at each other. Handling that needs a cycle collector, which is a tracing
collector again, on top of the counting. Tracing alone is less code and
collects everything.

## Concurrency

A task is an operating system thread. Each task has its own `VM` with its
own value stack and its own call frames. All tasks share one `Runtime`,
which owns the heap.

**One global lock.** A task holds the runtime lock while it runs
bytecode. This is the same shape as CPython's global interpreter lock, and
it has the same consequence: two tasks doing pure computation do not run
at the same time.

This was a deliberate trade, and it is the choice in this project that
most deserves an argument.

The alternative is a per-object or per-region lock with a collector that
can run while other tasks mutate the heap. That needs either a stop the
world handshake at safepoints, or a concurrent collector with barriers.
Both are large, and both fail in ways that only show up under load. With
one lock, the rule is simple enough to state in one line: *the collector
runs only while the lock is held, so any task that is not holding it has a
stack that is not moving and can be scanned safely.*

**Where it still helps.** A task releases the lock before anything that
blocks:

- waiting on a channel
- sleeping
- reading from a socket, accepting a connection, connecting
- reading a line from the terminal
- running a legacy script

So tasks overlap on input and output, and on waiting for each other. They
do not overlap on computation. The echo server example is genuinely
concurrent, because its tasks spend their time in `accept` and `read`.
The word count example splits work across tasks but will not run four
times faster on four cores.

**Channels.** A channel is a bounded queue guarded by the runtime lock,
with a condition variable over that same lock. A task that waits releases
the lock, which lets other tasks and the collector run. A channel with
capacity zero is unbuffered: the sender waits until a receiver takes the
value.

Because channel state is guarded by the runtime lock rather than a private
mutex, the collector can read a channel's buffer directly while marking.
Values parked in a channel are live, and this is how they stay live.

**What would come next.** Green threads on a scheduler, with the current
channel shape kept. That would show stack switching and scheduling, and
would remove the thread per task cost. It would not fix the lock.

## Error handling

`try`, `catch` and `throw`. Errors are values.

The VM keeps a flat stack of active handlers. Each entry records the call
frame, the stack depth and where the catch block starts. Unwinding is then
a truncation, not a walk: cut the frame stack and the value stack back to
what the handler recorded, push the error, and jump.

Faults raised by the runtime itself, such as adding a number to nil, build
the same kind of error value as `throw` does. So a program can catch a
division by zero the same way it catches its own errors. Every error
carries a message and the call stack from where it was raised.

Anything can be thrown. A value that is not already an error is wrapped in
one, so a `catch` block always receives something with `.message`,
`.kind`, `.trace` and `.payload`.

Every error carries a kind, and a `try` can have several `catch` clauses
with filters. A filter is either a string, matched against the kind, or a
class, matched against the thrown instance and its superclasses. Matching
on classes is why `ObjClass` keeps a superclass pointer even though
method lookup does not need one: methods are copied down at the point of
inheritance, so the link exists only to answer this question.

An error that no clause matches carries on outwards unchanged. Without
that, a `try` that named a few kinds would quietly swallow everything
else, which is the failure mode that makes exception handling untrusted.

A nested run loop, which is what `import` and callbacks such as
`array.map` use, records the frame it started at. Unwinding never crosses
that line. Without it, an error inside an imported module could jump into
a `try` block in the importing file and leave the stacks inconsistent.

Leaving a try block by jumping out of it, with `break` or `continue`,
has to close the handler on the way. A handler left behind points at a
frame and a stack depth that no longer exist, so a later `throw` resumes
inside dead code. The compiler counts the try blocks open at the start of
each loop and emits one `TRY_END` per handler opened since.

## finally

A `finally` block has to run on every way out of a `try`: falling off the
end, a caught error, an error that no clause matched, a `catch` clause
that threw, and a `return`, `break` or `continue` leaving the block.

Compilers usually handle this by emitting a copy of the finally block at
each exit. A single pass compiler cannot, because the exits inside the
body are compiled before the `finally` has been read.

So Red compiles one copy and routes every exit through it. Two hidden
slots carry the decision: an action slot saying why control is leaving,
and a pending slot holding the error or the return value. Each exit sets
both, unwinds to the depth the finally expects, and jumps to it. After
the block runs, a dispatch acts on the action.

The slots are allocated for every `try`, not only those with a
`finally`, for the same single pass reason: when the body is compiled it
is not yet known whether one follows. That costs two stack slots and two
instructions per `try` statement.

The dispatch runs after the try's own context has been popped, so a
`return` in it routes through an enclosing `finally` if there is one.
That is what makes nested blocks run innermost first without any extra
machinery.

## Compiling ahead of time

Red compiles to bytecode and then interprets it. It has never had a just
in time compiler, and the change here is not about that. What it did do
was compile from source on every run.

`red compile app.red` writes the bytecode to a file, and running that
file skips the compiler. On a program of four thousand functions that is
41ms of start-up down to 4.4ms, about nine times faster, and the compiled
file is smaller than the source for ordinary code.

Paying the compiler once also makes it worth doing work there. The
compiler folds arithmetic, bitwise operations and string joins on
literals into single constants. It is a peephole rather than a pass over
a tree, which is what a single pass compiler can do cheaply. Anything
deeper, such as removing dead code or reusing common subexpressions,
needs an intermediate form to work on, and that is a separate change.

The file format is versioned and checked on load. See
[bytecode.md](bytecode.md#compiled-files).

## Stack limits

Two separate bounds. Call depth is capped at 1024 frames. That alone does
not keep pushes inside the value stack, because one frame can hold up to
256 locals plus the temporaries its expressions need, and 256 of those
would run past the end.

So the compiler records an upper bound on each function's stack use, and
a call checks that bound against the room left before it pushes a frame.
Going over reports an error that a program can catch, rather than writing
past the end of the buffer. Whichever limit is reached first is the one
reported.

## Enums

An enum is built while compiling, not at run time. Every member is
created once and the whole enum is stored as a single constant, so
declaring one costs nothing when the program runs and mentioning a member
is a constant load.

Members are unique objects, so comparing them is a pointer compare and
they can be map keys. That is why the rule for map keys is written as one
function, `isHashableKey`, rather than repeated at each site: a key must
either compare by value or be an object that never changes.

Members print as `Colour.Red` rather than as a number. A compiler written
in Red will have a token kind and an opcode for every instruction, and
reading `38` in a trace instead of `TokenType.Fun` is the difference
between a debuggable program and a frustrating one.

Enums do not carry data. A tagged union would be a bigger feature, and it
would overlap with classes, which already hold fields.

## Modules

Each file is a module with its own globals table. Built-in functions live
in a separate table that every module can see, so a global lookup checks
the module first and falls back to the built-ins.

Imports resolve against the importing file's directory, not the working
directory, so a module can be moved without editing its imports. Loaded
modules are cached by absolute path. A module that is still loading is put
in the cache before its body runs, so an import cycle ends with a partly
filled module instead of looping forever.

## The legacy bridge

`legacy/` holds the original Java interpreter, unchanged. CMake builds it
into `red-legacy.jar` when a JDK is present, and skips it otherwise.

v2 runs it as a child process. `red legacy script.red` runs a v1 program
from the command line. `legacy(path)` does the same from inside a v2
program, and `legacy_output(path)` captures its output as a string.

A child process was chosen over embedding the Java virtual machine
through JNI. Embedding would avoid the process, but it would add a heavy
build dependency, make the build fragile, and tie the v2 runtime to a Java
installation. A child process keeps the two versions honestly separate:
v1 is the old implementation, not a subsystem of the new one.

## Tooling

- `red disasm file.red` prints the compiled bytecode, including nested
  functions.
- `--trace` prints every instruction with the stack before it runs.
- `--gc-log` reports each collection, with bytes freed and the next
  threshold.
- `--gc-stress` collects before every allocation.
- `red repl` reads statements. It counts brackets with the real scanner,
  so pasted multi-line input works, and braces inside strings do not
  confuse it.

The disassembler and the trace share one function. That is on purpose: the
trace can never drift from the disassembler, because there is only one
piece of code that knows how to print an instruction.

## Known limits

These are real and they are not hidden:

- Two tasks cannot compute at the same time. See
  [Concurrency](#concurrency).
- The collector stops the world and does not move objects.
- Type annotations are parsed and ignored.
- Optimisation is limited to peephole constant folding, because there is
  no intermediate form to run a real pass over.
- Code that makes many *distinct* strings is slow, because every string
  is interned. Reusing a small vocabulary is fast, which is why the
  `string` benchmark looks much worse than real programs do.
- A task that is never joined is kept alive until the program ends.
- Each task's value stack is one megabyte, allocated up front. That is
  the price of never moving it, because call frames and open upvalues
  hold raw pointers into it.
- The instruction pointer lives in the call frame rather than in a local
  variable in the dispatch loop. Caching it would be the first thing to
  try for speed.

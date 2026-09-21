# Leaving the virtual machine

Red compiles to bytecode and a stack machine runs it. This file is about
whether that should change, and it is a thinking-out-loud document rather
than a plan. Nothing here is scheduled. Some of it argues against itself.

The short version, up front, so the rest can be read as the working: **a
native backend is the wrong next thing.** The measurements below say the
dispatch loop is not where the time goes, and the changes that would
actually make Red faster are changes to the runtime that a native backend
would have to make anyway.

## Four things people mean

"No virtual machine" is four different wishes wearing the same coat, and
they have almost nothing to do with each other.

**I want a single binary to hand someone.** This is a packaging wish, not
a compilation one. It is already nearly solved: `red compile` produces a
`.redc`, and appending that to a copy of the interpreter and having the
interpreter look for it at the end of its own file would finish it. A
weekend, no compiler work, and it delivers what most people asking for
"compiled" actually want.

**I want it to start instantly.** Also already solved. A `.redc` skips
the compiler, and start-up on a four thousand function program is 4.4ms.
Native code would make that perhaps 1ms. Nobody notices.

**I want it to be fast.** This is the real question, and the rest of this
file is about it.

**I don't want a runtime at all: no collector, no tagged values.** This
is not a compilation strategy, it is a different language. Manual memory
management or ownership, static types, no `map` that can hold anything.
Red would not be Red afterwards. Saying it out loud because people
sometimes do mean this, and no backend delivers it.

## Where the time actually goes

Measured on an Apple M3, three million iterations each, the cost of the
loop itself subtracted where it says so. These are approximate and one
run each, but they are stable across runs to about 3%.

| Work | Per operation |
|---|--:|
| An empty loop: compare, increment, jump | 22ns |
| One bytecode instruction, roughly | 2 to 2.5ns |
| `x = x + i` on locals, five instructions | 10ns |
| `p.x`, a field read | 13ns |
| `arr[1]` | 16ns |
| `m["k"]` | 22ns |
| `p.get()`, a method call and return | 26ns |
| `"ab" + "cd"`, a string already interned | 27ns |
| `"x" + str(i)`, a string that is new every time | 297ns |

Read the last two rows first. Making a string that has not been seen
before costs more than a hundred arithmetic instructions, because every
string is interned: hashed, looked up, and allocated if new. The
[design notes](design.md#value-layout) call this a known cost. It is the
single largest number on the page and no backend touches it.

Then read the middle. A field read costs three times a local read, a
method call six times, a map lookup five times. Those are hash probes and
call frame setup, and they are the same work whether an interpreter or
native code is asking for it.

Now the top. **Dispatch is 2 to 2.5ns per instruction**, which on this
machine is six or seven cycles: read the opcode, jump through a switch,
decode operands, do the work. A native compiler makes the reading and the
jumping disappear. It does not make the work disappear.

So the ceiling on a naive native backend is: remove dispatch, keep
everything else. On the tightest arithmetic loop, where dispatch is
perhaps half the cost, that is a bit less than 2x. On code that touches
objects, calls methods and builds strings — which is to say on programs —
it is 10 to 20%. Not what anyone hopes for when they ask for a native
compiler, but it is the number.

## What Red makes easy

If the work were to happen, several things are already in place, and
they are not accidents.

**The bytecode is a documented, versioned format.**
[bytecode.md](bytecode.md) describes every instruction and operand. A
backend is a consumer of that format, not of the C++.

**There is already a compiler written in Red.**
[`selfhost/redc.red`](../selfhost/redc.red) turns source into bytecode.
A native backend is another pass on the far end of it, in the same
language, using the same structures. The hard part of a self-hosted
toolchain is done.

**The instruction set is small and regular.** Sixty-odd opcodes, two-byte
constant indexes everywhere, no short forms, no variable-length operands.
A code generator is a switch over sixty cases.

**There is no syntax tree to keep in step.** One representation, from the
compiler to whatever consumes it.

## What Red makes hard

And here is the other column, which is longer.

**Every value is a tagged 16-byte struct.** `Value` is an 8-bit tag plus
an 8-byte payload, padded to 16. Every arithmetic operation is: check
both tags, branch, unwrap, compute, wrap. Native code still does all of
that, because the types are not known until the program runs. A native
`ADD` is not `add rax, rbx`; it is a dozen instructions of tag checking
around one that adds. This is the reason the 2x ceiling is a ceiling.

**The collector needs to find roots.** Today it walks each task's value
stack and call frames, which are plain arrays it understands. Native code
keeps values in registers and in its own stack frames, and the collector
cannot see them. Fixing that means stack maps at every allocation site,
or a shadow stack that gives back much of what was won. This is the part
that turns a weekend prototype into a year.

**Closures capture by reference.** An upvalue points at a stack slot
until the slot dies and the value is moved to the heap. That protocol is
woven through the call and return paths.

**Tasks are operating system threads sharing one lock.** Native code has
to take and release that lock at the same points the interpreter does,
and has to be interruptible where the interpreter is.

**Extensions are already compiled.** The FFI hands out raw `RedValue`s
with a fixed layout. Changing the value representation — which is the one
change that would actually make native code pay — breaks every extension
ever built.

## The routes, and what each is worth

**Bytecode to machine code, ahead of time.** The obvious one. Walk the
chunk, emit code per opcode, keep the runtime for allocation, collection
and the standard library. Gets the 10 to 20% above. Costs a code
generator per architecture, a relocation and linking story, stack maps
for the collector, and a debugging experience that gets worse before it
gets better. **Verdict: the worst ratio of the four.** It is the route
people picture when they say "compiler", and it buys the least.

**Bytecode to C.** Emit a C function per Red function, with the runtime
called for everything interesting, and hand the result to the system C
compiler. This is how several small languages ship, and it is genuinely
attractive here: no code generator, no register allocator, every
architecture a C compiler targets, and the C compiler does the register
allocation and instruction selection that a first-attempt backend would
do badly. The collector problem is easier too, because the values can be
kept in a frame the runtime can still see. It produces a real binary,
which answers the packaging wish properly, with no files stapled
together. **Verdict: if a native backend happens, this is the one.** The
speed is similar to the previous route; the cost is a fifth.

**A just-in-time compiler with type feedback.** The only route that
breaks the 2x ceiling, because it is the only one that can know that this
`+` has seen two numbers ten thousand times and emit the version that
assumes so, with a guard. This is where the real numbers are — 5 to 10x
is not unreasonable on hot loops. It is also years of work, and it brings
deoptimisation, tiering, and a class of bug where the fast path and the
slow path disagree and the program quietly computes the wrong answer.
**Verdict: the real answer to "make it fast", and far out of proportion
to this project.**

**Static types, and a real compiler.** Type annotations exist in the
grammar and are ignored. Checking them, inferring the rest, and
specialising on them is the only way to get to C's numbers. It is a
different language with the same syntax. **Verdict: a fine thing to want.
Not this project.**

## What to do instead, and why it is not a consolation prize

Every measurement above points at the runtime, not at the loop. A native
backend would need these changes anyway, and each is worth more on its
own, sooner, and can be done alone:

**Inline caches on property and method lookup.** ~~A field read is 13ns
and a method call 26ns, mostly hash probing.~~ Also tried, in the
cheapest possible way: a single global cache entry in `invokeFromClass`,
which is the best any per-site cache could do and then some. It made no
measurable difference. The method lookup is not where the 26ns is; the
call frame setup and the field read inside the method body are. A per
site cache would have cost a parallel array the size of the bytecode and
bought nothing.

That leaves the field read itself, which would need hidden classes to
improve, and that is a much larger change than "two hundred lines".

**Stop interning every string.** Done in 0.4.0, and it worked. Strings of one or two characters are still shared; longer ones
are allocated outright and compare by hash and contents. Building a
string went from 297ns to 230ns, and the `string` benchmark from 5.6
times CPython to 3.5. Everything else stayed where it was.

**NaN boxing.** ~~Halves `Value` from sixteen bytes to eight, which halves
the memory traffic of every stack push, every array, every map.~~ Tried,
and it made things worse: calls and arithmetic lost 5 to 12%, because a
boxed double arrives in an integer register and every operation has to
move it to a floating point one and back. Method dispatch gained 5%. Net,
a loss. [design.md](design.md#value-layout) has the table. Struck out
out, not deleted: a prediction that turned out wrong earns its place on
the page. A prediction quietly removed teaches nobody anything.

**Computed-goto dispatch.** Replaces the switch with a jump table
threaded through each instruction, which removes one indirect branch and
helps the branch predictor keep separate history per opcode. Twenty lines
behind a compiler check. Still untested, and now the only one of these
four that is.

**Nothing in the dispatch loop that does not have to be there.** This one
was never on the list, and it worked. The loop began each
instruction by testing `runtime_.traceExecution`, for a tracer almost
nobody runs. Adding a second such test, for the debugger, cost 30% on a
tight loop. Compiling both out instead — the loop is a template over a
bool, instantiated once with them and once without — made every
benchmark 7 to 12% faster than before either existed.

Thirty per cent for one `if` in a loop that runs a few nanoseconds per
pass. The lesson is not about tracing; it is that at this scale the
dispatch loop has no room in it at all, and that anything measured
without looking at what is already in that loop is measuring the wrong
thing.

Four of those five have now been tried. Two worked, two did not, and
computed-goto dispatch is still untested. That is a worse hit rate than
this file first implied, and it sharpens the point: if changes this
targeted, guided by these measurements, are
mostly not paying off, a native backend guided by the same measurements
is not going to pay off either.

The summary is that the interpreter is closer to its floor than it
looked. Getting past that floor needs a different value representation or
type feedback, not a different way of reaching the same runtime.

## The one experiment left

Steps 1 and 2 of what this file first proposed have been run, and are
written up above: the inline cache bought nothing, NaN boxing lost.
What is left is the one that would settle it:

Take one benchmark — `loop.red` is the most favourable case — and write
by hand the C that a transpiler would emit for it. Compile it, link it
against the real runtime, and time it.

That is a day. If hand-written best-case C against the real runtime is
not at least 3x the interpreter, no backend is going to be either, and
the question is closed for good. If it is 5x, this file is wrong and
worth rewriting.

It has not been run.

## Where this leaves stage 4

[bootstrapping.md](bootstrapping.md#stage-4-the-virtual-machine) sets out
three ways to remove the remaining C++ and says the third is "keep the
virtual machine and call the goal met at stage 3". Having thought about
it properly, that is the right answer, with one amendment: the goal is
better stated than met.

Red is a language whose compiler is written in itself and whose runtime
is written in C++. That is exactly where Lua, CPython, Ruby and most of
the others sit, and it is not a half-finished state on the way to
somewhere. Saying "the compiler is self-hosted, the runtime is C++" is a
complete description of a real language.

The C backend stays interesting, for distribution, not for speed.
The JIT stays interesting as a thing to read about. The inline caches are
the next real work.

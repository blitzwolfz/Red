# Bytecode

Red compiles to a flat stream of bytes. This file describes that stream.

It is the contract between the compiler and the virtual machine, and
there are two compilers: the C++ one in [`src/`](../src) and the Red one
in [`selfhost/redc.red`](../selfhost/redc.red). They agree byte for byte,
and this file is why. Anything written here is the format; anything not
written here is an implementation detail neither of them may rely on.

## Versions

Format version **3**.

| Version | Added |
|---|---|
| 3 | The catch filter and destructuring instructions. |
| 2 | Duplication, bitwise, iteration and default argument instructions. |
| 1 | The original set. |

New opcodes go at the end of the list, so the numbering of the older ones
never moves. The numbering *is* the format.

Changing the version means changing three things together:
`kBytecodeVersion` in [`src/common.h`](../src/common.h),
`BYTECODE_VERSION` in [`selfhost/redc.red`](../selfhost/redc.red), and
the table above. A compiled file carries the version and is refused
rather than misread by an interpreter that expects a different one.
[bootstrapping.md](bootstrapping.md#what-to-protect-now) says why this
matters more now than it did.

## Chunks

A chunk is one compiled function. It holds three things:

| Part | Contents |
|---|---|
| `code` | The instruction bytes. |
| `constants` | Values the instructions refer to by index. |
| `lines` | Source line for each byte, stored as runs. |

Line numbers are stored as `(line, count)` runs rather than one number per
byte. Straight line code makes long runs, so this is usually much smaller
than a parallel array. Looking a line up is a scan, which only happens
when an error is already being reported.

Nested functions are stored in the constant pool of the function that
contains them. `red disasm` walks into them.

## Operand encoding

Every instruction is one opcode byte, then zero or more operand bytes.
Multi-byte operands are big endian.

| Operand | Size | Meaning |
|---|---|---|
| constant | 2 bytes | Index into the chunk's constant pool. |
| slot | 1 byte | Local slot or upvalue index, relative to the frame. |
| count | 1 or 2 bytes | Argument count, or element count. |
| jump | 2 bytes | Unsigned distance, added or subtracted from the instruction pointer. |

Constant indexes are two bytes everywhere, even where one would usually
do. A single size means the compiler never has to choose between a short
form and a long form, and the disassembler never has to track which one
was used. The cost is one extra byte per instruction that names a
constant.

Local slots are one byte, so a function has at most 256 locals and 256
upvalues. The compiler reports an error past that.

## Instruction set

### Constants and literals

| Opcode | Operands | Stack effect |
|---|---|---|
| `CONSTANT` | constant | `-> value` |
| `NIL` | | `-> nil` |
| `TRUE` | | `-> true` |
| `FALSE` | | `-> false` |
| `POP` | | `value ->` |
| `DUP` | | `a -> a a` |
| `DUP2` | | `a b -> a b a b` |

`DUP` and `DUP2` exist for compound assignment. `obj.field += 1` needs the
receiver twice, once to read the field and once to write it back, and
`items[i] += 1` needs both the target and the index twice. Duplicating is
what keeps the subject from being evaluated a second time.

### Variables

| Opcode | Operands | Stack effect |
|---|---|---|
| `GET_LOCAL` | slot | `-> value` |
| `SET_LOCAL` | slot | `value -> value` |
| `GET_GLOBAL` | constant | `-> value` |
| `SET_GLOBAL` | constant | `value -> value` |
| `DEFINE_GLOBAL` | constant | `value ->` |
| `GET_UPVALUE` | slot | `-> value` |
| `SET_UPVALUE` | slot | `value -> value` |

A global lookup checks the current module's table first, then the table of
built-in functions. `SET_GLOBAL` on a name that was never defined is an
error.

### Properties

| Opcode | Operands | Stack effect |
|---|---|---|
| `GET_PROPERTY` | constant | `object -> value` |
| `SET_PROPERTY` | constant | `object value -> value` |
| `GET_SUPER` | constant | `this superclass -> method` |

`GET_PROPERTY` handles instance fields, instance methods, module globals,
error properties, and the methods the runtime provides for built-in types.

### Arithmetic and comparison

| Opcode | Stack effect |
|---|---|
| `ADD` `SUBTRACT` `MULTIPLY` `DIVIDE` `MODULO` | `a b -> result` |
| `EQUAL` `NOT_EQUAL` | `a b -> bool` |
| `GREATER` `GREATER_EQUAL` `LESS` `LESS_EQUAL` | `a b -> bool` |
| `BIT_AND` `BIT_OR` `BIT_XOR` `SHIFT_LEFT` `SHIFT_RIGHT` | `a b -> result` |
| `NEGATE` `NOT` `BIT_NOT` | `a -> result` |

The bitwise instructions truncate each operand towards zero and wrap it
into a 32 bit signed integer before combining them. `SHIFT_RIGHT` keeps
the sign, and both shifts mask the count to 0 to 31.

`ADD` joins two strings or adds two numbers. The comparisons take two
numbers or two strings. `DIVIDE` and `MODULO` raise an error on a zero
right hand side.

### Jumps

| Opcode | Operands | Stack effect |
|---|---|---|
| `JUMP` | jump | none |
| `JUMP_IF_FALSE` | jump | none, reads the top |
| `JUMP_IF_TRUE` | jump | none, reads the top |
| `LOOP` | jump | none, jumps backwards |

The conditional jumps leave the value they tested in place. `and` and `or`
rely on that, and pop it themselves when the value is not the answer.

### Calls and functions

| Opcode | Operands | Stack effect |
|---|---|---|
| `CALL` | count | `callee arg... -> result` |
| `INVOKE` | constant, count | `receiver arg... -> result` |
| `SUPER_INVOKE` | constant, count | `this arg... superclass -> result` |
| `CLOSURE` | constant, then pairs | `-> closure` |
| `CLOSE_UPVALUE` | | `value ->` |
| `RETURN` | | `result ->` |

`INVOKE` fuses a property lookup and a call. It saves allocating a bound
method for the common `object.method(...)` shape.

`CLOSURE` is followed by two bytes per upvalue: a flag saying whether the
upvalue captures a local of the enclosing frame or one of its upvalues,
and the index.

`CLOSE_UPVALUE` moves a captured local from the stack to the heap when its
scope ends, so a closure that outlives the scope still sees it.

### Classes

| Opcode | Operands | Stack effect |
|---|---|---|
| `CLASS` | constant | `-> class` |
| `INHERIT` | | `superclass subclass -> subclass` |
| `METHOD` | constant | `class closure -> class` |

`INHERIT` copies the superclass methods into the subclass rather than
chaining a lookup. Dispatch then costs one table probe at any depth. The
cost is that changing a class after a subclass exists does not affect the
subclass.

### Collections

| Opcode | Operands | Stack effect |
|---|---|---|
| `ARRAY` | count (2 bytes) | `item... -> array` |
| `MAP` | count (2 bytes) | `key value ... -> map` |
| `GET_INDEX` | | `target index -> value` |
| `SET_INDEX` | | `target index value -> value` |

### Strings

| Opcode | Stack effect |
|---|---|
| `TO_STRING` | `value -> string` |

Emitted by string interpolation, so `"${x}"` works whatever `x` is.

### Errors

| Opcode | Operands | Stack effect |
|---|---|---|
| `TRY_BEGIN` | jump | none |
| `TRY_END` | | none |
| `THROW` | | `value ->` |

`TRY_BEGIN` records the current frame, the current stack depth, and the
address of the catch block. `TRY_END` drops that record when the try block
finishes normally. Raising an error cuts the frame stack and the value
stack back to what the record holds, pushes the error, and jumps.

### Iteration

| Opcode | Operands | Stack effect |
|---|---|---|
| `ITER_PREP` | | `subject -> sequence` |
| `ITER_NEXT` | slot, slot, jump | `-> element`, or jumps |

`ITER_PREP` turns the subject into something a loop can step through. An
array is left alone, a map becomes a snapshot of its keys, and a string
becomes its characters. Anything else is an error.

`ITER_NEXT` reads the sequence and the position from the two local slots
its operands name. It pushes the next element and advances the position,
or jumps when the sequence is spent. The length is read on every pass, so
a loop over an array that shrinks underneath it stops rather than reading
past the end.

The element lands on top of the stack, which is exactly the slot the loop
variable occupies. That is why no separate store instruction is needed.

### Calls with optional arguments

| Opcode | Operands | Stack effect |
|---|---|---|
| `JUMP_IF_ARG` | slot, jump | none |

A call pads the parameters it did not supply with nil, and records how
many were actually passed. The function's prologue then holds one
`JUMP_IF_ARG` per optional parameter, which skips that parameter's
default when the call did supply it. This is what keeps an omitted
argument different from an explicit `nil`.

Rest parameters need no instruction. The call gathers the extra
arguments into an array before the frame is pushed.

### Errors and patterns

| Opcode | Operands | Stack effect |
|---|---|---|
| `CATCH_MATCHES` | | `error filter -> bool` |
| `DESTRUCTURE_INDEX` | index | `subject -> value` |
| `DESTRUCTURE_REST` | index | `subject -> array` |
| `DESTRUCTURE_FIELD` | constant | `subject -> value` |

`CATCH_MATCHES` consumes both operands. A `try` with several `catch`
clauses keeps the caught error in a hidden slot and pushes a copy of it
for each clause to test, so each test takes its copy with it.

The three destructuring instructions exist rather than reusing
`GET_INDEX` and `GET_PROPERTY` because patterns need different rules. A
position past the end gives `nil` instead of failing, since a pattern may
be longer than what it matches, and a field read never finds a method, so
`let {len} = point` cannot pick one up by accident.

### Tasks and modules

| Opcode | Operands | Stack effect |
|---|---|---|
| `SPAWN` | count | `callee arg... -> task` |
| `IMPORT` | constant | `-> module` |

## Compiled files

`red compile app.red` writes `app.redc`, and running that file skips the
compiler. The layout is:

```
"REDC"            4 bytes
version           4 bytes, fixed width
<function>        the top level function, and everything under it
```

The version is fixed width on purpose, so that a file from another
release is refused with a clear message rather than misread. Everything
after it uses a variable length encoding: seven bits per byte, low group
first, top bit set while more follow. Counts in a chunk are almost always
small, so this is far smaller than four bytes each. Numbers stay eight
bytes, since a double has no small form.

A function record holds its name, its arity and slot counts, its
parameter and return type annotations, its instruction bytes, its line
runs, and its constants. A constant is one tagged byte followed by its
contents. Nested functions appear inside their parent's constant pool, so
one record carries the whole tree.

Enums are written once and then referred to by position, because members
compare by identity and two copies of one enum would not be equal.

Imports stay dynamic: `IMPORT` still carries a path and still resolves
when the instruction runs. So a compiled file finds its imports relative
to where the compiled file is, not where its source was.

## Constant folding

The compiler folds arithmetic, bitwise operations and string joins on
literals, so `2 + 3 * 4` becomes one `CONSTANT`. It is a peephole: the
folder only acts when the two operands each emitted exactly one constant
and nothing else came between them.

Division and remainder by a literal zero are left alone, so they still
report at run time where the line number and the call stack are
available.

## Reading the output

`red disasm file.red` prints every chunk. The columns are the byte
offset, the source line (`|` when it is the same as the line before), the
opcode, and the operands.

```
$ red disasm examples/tiny.red
== <script> examples/tiny.red ==
0000    1 CLOSURE               1 '<fun square>'
0003    | DEFINE_GLOBAL         0 '"square"'
0006    2 GET_GLOBAL            2 '"print"'
0009    | GET_GLOBAL            0 '"square"'
0012    | CONSTANT              3 '7'
0015    | CALL                  1
0017    | CALL                  1
0019    | POP
0020    3 NIL
0021    | RETURN

== square ==
0000    1 GET_LOCAL             1
0002    | GET_LOCAL             1
0004    | MULTIPLY
0005    | RETURN
```

`--trace` prints the same lines while the program runs, with the stack
above each one. Both use the same function, so the trace can never drift
from the disassembler.

```
$ red --trace examples/tiny.red
          [ <script> ][ <native print> ][ <fun square> ][ 7 ]
0000    1 GET_LOCAL             1
          [ <script> ][ <native print> ][ <fun square> ][ 7 ][ 7 ]
0002    | GET_LOCAL             1
```

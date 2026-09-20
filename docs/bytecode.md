# Bytecode

Red compiles to a flat stream of bytes. This file describes that stream.
It is the contract between the compiler and the virtual machine, and it is
what a future self-hosted compiler would target. See
[bootstrapping.md](bootstrapping.md).

Format version: **1**. The version in `src/common.h` changes whenever the
layout below changes in a way that breaks old code.

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
| `NEGATE` `NOT` | `a -> result` |

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

### Tasks and modules

| Opcode | Operands | Stack effect |
|---|---|---|
| `SPAWN` | count | `callee arg... -> task` |
| `IMPORT` | constant | `-> module` |

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

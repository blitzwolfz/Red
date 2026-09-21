# The Red language

A reference for Red v2: every form in the language, and a grammar at the
end.

If you are starting out, [guide.md](guide.md) builds a whole program
instead, and [`examples/tour.red`](../examples/tour.red) is one file that
uses most of what is below.

v2 is not source compatible with v1. The old syntax still runs, on the old
interpreter. See [the legacy section](#running-v1-code).

## Comments

```red
// A line comment.

/* A block comment.
   /* Block comments nest, so you can comment out a region that
      already contains one. */
*/
```

## Values

Red has five kinds of value plus the objects listed later.

| Type | Example | Notes |
|---|---|---|
| `nil` | `nil` | The absence of a value. |
| `bool` | `true`, `false` | |
| `number` | `1`, `-2.5`, `1e9`, `0xff` | One numeric type, a 64 bit float. |
| `string` | `"text"` | |
| `array` | `[1, 2, 3]` | |
| `map` | `{"a": 1}` | |

Only `nil` and `false` are false. Zero, the empty string and the empty
array are all true.

```red
if (0) { print("this runs"); }
```

Whole numbers print without a decimal point. `1.0` prints as `1`.

A number prints as the shortest text that reads back as the same value,
so `num(str(x))` always gives `x` again. Above 2^53, where doubles stop
counting by one, the exponent form takes over rather than printing digits
the value does not carry.

```red
print(0.1 + 0.2);            // 0.30000000000000004
print(1e15);                 // 1000000000000000
print(1e16);                 // 1e+16
```

Hex literals are written `0x` and are ordinary numbers, which is usually
how a mask or a byte value is spelled.

```red
print(0xff, 0xDEADBEEF);     // 255 3735928559
print(0xff & 0x0f);          // 15
```

## Bindings

`let` introduces a name that can change. `const` introduces one that
cannot.

```red
let count = 0;
count = count + 1;

const limit = 10;
limit = 11;        // error, caught while compiling
```

A `let` with no value starts at `nil`. A `const` must have a value.

Names are block scoped. A name declared inside `{ }` is gone after it.

```red
let x = "outer";
{
  let x = "inner";
  print(x);        // inner
}
print(x);          // outer
```

### Type annotations

Any name can carry a type. Nothing checks it. It is there for the reader
and for the disassembler.

```red
let attempts: Int = 0;
fun area(width: Float, height: Float) -> Float {
  return width * height;
}
```

## Strings

Strings use double quotes. These escapes are understood: `\n`, `\t`,
`\r`, `\0`, `\\`, `\"`, `\$`, and `\u` for a code point.

```red
print("caf\u00e9");           // café, from four hex digits
print("\u{1f600}");           // 😀, from one to six inside braces
```

Any expression can be written inside `${ }`:

```red
const name = "world";
print("Hello, ${name}!");
print("2 + 2 = ${2 + 2}");
print("nested ${"in" + "ner"}");
```

A `$` that is not followed by `{` is an ordinary character, so `"$5"`
needs no escape.

Strings compare in dictionary order, and index by byte:

```red
print("apple" < "banana");   // true
print("hello"[0]);           // h
print("hello"[-1]);          // o
```

A string holds bytes and may contain anything, text or not. `len()`,
indexing and `code_at()` work in bytes; `chars()`, `code_points()`,
`char_len()` and `for ... in` work in characters, decoded as UTF-8.

```red
const greeting = "héllo";
print(greeting.len());          // 6, because é takes two bytes
print(greeting.char_len());     // 5
for (let c in greeting) { write(c, "."); }   // h.é.l.l.o.
```

[stdlib.md](stdlib.md#text-and-bytes) has the whole pair of tables.

`upper()` and `lower()` cover all of Unicode, including the mappings that
change length: `"straße".upper()` is `"STRASSE"`.

## Operators

From loosest to tightest:

| Level | Operators |
|---|---|
| assignment | `=` `+=` `-=` `*=` `/=` `%=` |
| or | `or` |
| and | `and` |
| equality | `==` `!=` |
| comparison | `<` `<=` `>` `>=` |
| bitwise or | `\|` |
| bitwise xor | `^` |
| bitwise and | `&` |
| shift | `<<` `>>` |
| term | `+` `-` |
| factor | `*` `/` `%` |
| unary | `!` `-` `~` |
| call | `()` `.` `[]` |

Bitwise operators bind **tighter** than comparison, so `a & b == c` means
`(a & b) == c`. C binds them the other way round, which surprises people,
so Red does not copy it.

`and` and `or` stop early and give back the operand that decided the
result:

```red
print(nil or "fallback");    // fallback
print(false and boom());     // false, boom() is never called
```

`+` adds numbers or joins strings. It will not mix the two. Use `str()`
or interpolation.

`/` and `%` by zero raise an error rather than giving infinity.

Bitwise operators work on 32 bit signed integers. A number is truncated
towards zero and wrapped into that range first. `>>` keeps the sign, and
a shift count is masked to 0 to 31, so shifting by 32 is defined.

```red
print(12 & 10);        // 8
print(1 << 8);         // 256
print(-16 >> 2);       // -4
print(~0);             // -1
```

### Compound assignment

`+=`, `-=`, `*=`, `/=` and `%=` read a target, combine it, and write it
back. They work on variables, captured variables, fields and elements.
The target is evaluated once.

```red
let n = 10;
n += 5;
counter.count *= 2;
scores["ann"] += 1;
items[0] -= 3;
```

Numbers, booleans, `nil` and strings compare by value. Everything else
compares by identity:

```red
print([1] == [1]);           // false, two different arrays
```

## Control flow

```red
if (x > 0) {
  print("positive");
} else if (x < 0) {
  print("negative");
} else {
  print("zero");
}

while (running) {
  step();
}

for (let i = 0; i < 10; i = i + 1) {
  print(i);
}

for (;;) {
  if (done()) { break; }
}
```

`for ... in` walks a collection instead of counting.

```red
for (let item in [10, 20, 30]) { print(item); }
for (let key in ages) { print(key, ages[key]); }
for (let letter in "abc") { print(letter); }
```

An array is walked in order. A map yields its keys, as a snapshot taken
when the loop starts, so adding entries during the loop does not disturb
it. A string yields its characters. Anything else is an error.

The loop variable is a fresh binding on each pass, so a closure made in
the body captures that pass's value rather than sharing one.

```red
let readers = [];
for (let value in [1, 2, 3]) {
  readers.push(fun () { return value; });
}
print(readers[0](), readers[2]());     // 1 3
```

`switch` compares a value against each case. There is no fall through,
so a case does not need `break` to end. A case can list several values.
The subject is evaluated once.

```red
switch (kind) {
  case "tiger", "lion": return "ROAR";
  case "otter": return "squeak";
  default: return "...";
}
```

Case values are expressions, not only literals, and they are compared
with `==`. A `switch` with no matching case and no `default` does
nothing. `default` must come last.

Inside a loop, `break` always belongs to the loop, never to a `switch`,
because a case never falls through.

`break` leaves the loop. `continue` goes to the next step. In a `for`
loop `continue` still runs the increment. Leaving a loop from inside a
`try` block closes that block properly.

Braces are optional around a single statement, but the examples always
use them.

## Functions

```red
fun add(a, b) {
  return a + b;
}
```

A function with no `return` gives back `nil`. Functions are values, and
can be stored, passed and returned.

```red
const operations = {
  "add": add,
  "sub": fun (a, b) { return a - b; },
};
print(operations["sub"](8, 3));    // 5
```

`fun` without a name is an anonymous function.

### Default and rest parameters

A parameter can have a default. It is evaluated on each call, only when
the argument was left out, and it can refer to parameters declared before
it.

```red
fun greet(name, greeting = "Hello") {
  return "${greeting}, ${name}";
}
fun box(width, height = width) { return "${width}x${height}"; }
```

Leaving an argument out is not the same as passing `nil`. A default only
applies when the argument is absent.

A required parameter cannot follow one with a default.

The last parameter can be written with `...`, which gathers any further
arguments into an array.

```red
fun total(...numbers) {
  let sum = 0;
  for (let n in numbers) { sum += n; }
  return sum;
}
print(total(1, 2, 3));      // 6
```

### Closures

An inner function captures the variable itself, not a copy of its value.

```red
fun makeCounter() {
  let count = 0;
  return fun () {
    count = count + 1;
    return count;
  };
}

const next = makeCounter();
next();
print(next());       // 2
```

Two closures made in the same scope share the captured variable. Two
calls to `makeCounter` do not.

## Arrays

```red
const items = [1, "two", true, nil];
items.push(5);
print(items.len());        // 5
print(items[0]);           // 1
print(items[-1]);          // 5, negative counts back from the end
items[0] = "one";
```

Reading or writing outside the array raises an error.

Methods: `len` `push` `pop` `insert` `remove` `slice` `join` `contains`
`index_of` `reverse` `clear` `sort` `map` `filter` `reduce`.

```red
const numbers = [3, 1, 2];
print(numbers.sort());                                 // [1, 2, 3]
print(numbers.map(fun (n) { return n * n; }));         // [1, 4, 9]
print(numbers.filter(fun (n) { return n > 1; }));      // [2, 3]
print(numbers.reduce(fun (a, b) { return a + b; }));   // 6
```

`sort` with no argument orders numbers or strings. With a function, it
calls that function to compare two elements, and expects true when the
first should come earlier.

## Maps

```red
const ages = {"ann": 31, "bob": 25};
ages["cal"] = 40;
print(ages["ann"]);          // 31
print(ages["nobody"]);       // nil, a missing key is not an error
```

Keys can be strings, numbers, booleans or `nil`. Arrays, maps and
instances cannot be keys.

Methods: `len` `get` `set` `has` `remove` `keys` `values`.

```red
print(ages.get("nobody", 0));   // 0, a default for missing keys
print(ages.keys().sort());
```

## Enums

An enum is a set of named constants.

```red
enum Colour { Red, Green, Blue }
enum Status { Ok = 200, NotFound = 404, Teapot }
```

Values count up from zero unless a member gives one, and counting carries
on from the last value given, so `Teapot` above is 405. Negative values
are allowed.

Each member is a single object, built once when the enum is declared. So
members compare by identity, which is cheap, and they can be map keys.
They print with their enum name, which is the point: a bare number tells
a reader nothing when something goes wrong.

```red
print(Colour.Red);            // Colour.Red
print(Colour.Red.name);       // Red
print(Colour.Green.value);    // 1
print(Colour.Red.owner);      // <enum Colour>
```

The property is called `owner` rather than `enum`, because `enum` is a
keyword and could never be written after a dot.

The enum itself answers questions about its members:

| Call | Result |
|---|---|
| `Colour.values()` | Members, in declaration order. |
| `Colour.from(value)` | The member with that value, or `nil`. |
| `Colour.name()` | The enum's own name. |
| `Colour.len()`, `len(Colour)` | How many members. |

Members work in `switch`, as map keys, and their `value` indexes an
array:

```red
switch (colour) {
  case Colour.Red, Colour.Green: return "warm-ish";
  case Colour.Blue: return "cool";
}
const labels = ["red", "green", "blue"];
print(labels[Colour.Green.value]);
```

An enum binding is always `const`. Asking for a member that does not
exist is an error rather than `nil`, because it is almost always a typo.

A member whose name clashes with one of the calls above wins, so an enum
with a member called `values` hides `values()`.

Enums do not carry data. A variant with fields is a class with a `kind`
field.

## Destructuring

A pattern binds several names at once.

```red
let [a, b] = [1, 2];
let [head, ...tail] = items;
let {name, age} = person;
let {name: who} = person;
```

Array patterns read by position. A pattern may be longer than what it
matches, and the extra names are `nil`, the same way a missing map key
is. A `...` binding takes whatever is left, always as an array, and must
come last.

Map patterns read by name. They work on maps, on class instances, on
modules and on enums. On an instance they read fields only, so a pattern
can never pick up a method by accident.

Patterns nest, in both directions:

```red
let [[x, y], {z}] = [[1, 2], {"z": 3}];
let {corner: [left, top]} = box;
```

`const` patterns bind constants. A `for ... in` loop takes a pattern too,
which is what makes walking a map's entries read well:

```red
for (let [key, value] in ages.entries()) {
  print(key, value);
}
```

Destructuring something that has no elements or no fields is an error
naming the type, rather than a confusing failure further on.

## Sets

A set holds distinct values. Membership follows the same rules as map
keys, so strings, numbers, booleans, `nil` and enum members can go in
one.

```red
const seen = set();
const digits = set([1, 2, 2, 3]);
const letters = set("banana");     // a, b, n
print(digits.len());               // 3
```

| Method | Result |
|---|---|
| `add(...)` | Adds values. Gives the set. |
| `remove(value)` | `true` when it was there. |
| `has(value)` | Is it in the set? |
| `len()`, `len(s)` | How many values. |
| `items()` | The values as an array, in no particular order. |
| `clear()` | Empties it. |
| `union(other)` `intersect(other)` `difference(other)` | A new set. Neither operand changes. |
| `equals(other)` | Compares contents. |

`set(source)` builds one from an array, another set or a string.
`for ... in` walks the members. Sets print as `set(1, 2, 3)`, never in
braces, so they cannot be mistaken for maps.

## Classes

```red
class Animal {
  init(kind) {
    this.kind = kind;
  }

  describe() {
    return "I am a ${this.kind}";
  }
}

const cat = Animal("cat");
print(cat.describe());
```

`init` runs when the class is called. It always gives back the instance.
`this` is the receiver. Fields can be added at any time.

Single inheritance uses `<`. `super` reaches the method being overridden.

```red
class Dog < Animal {
  init(name) {
    super.init("dog");
    this.name = name;
  }

  describe() {
    return super.describe() + ", called ${this.name}";
  }
}
```

Methods are values. A method taken off an instance keeps that instance:

```red
const describe = Dog("Rex").describe;
print(describe());
```

A field holding a function hides a method with the same name.

### str and eq

Two method names mean something to the runtime. Neither is required.

`str()` says how an instance is written. It is used by `print`, by
`str()` and `repr()`, by `${}` interpolation, and by the printed form of
an array or map that holds one.

```red
class Point {
  init(x, y) { this.x = x; this.y = y; }
  str() { return "(${this.x}, ${this.y})"; }
}

print(Point(1, 2));            // (1, 2)
print([Point(1, 2)]);          // [(1, 2)]
print("at ${Point(1, 2)}");    // at (1, 2)
```

`eq(other)` says what `==` means for two instances. It is also what
`contains()` and `index_of()` search with. Without it, two instances are
equal only when they are the same object.

```red
class Point {
  init(x, y) { this.x = x; this.y = y; }
  eq(other) {
    return type(other) == "instance" and this.x == other.x and
           this.y == other.y;
  }
}

print(Point(1, 2) == Point(1, 2));   // true
```

A value is always equal to itself whatever `eq` does, because the
identity check comes first.

An instance may be a map key or a set member, **by identity**: two
objects with the same fields are two keys, the same way they are two
objects. `eq` does not change that, because a hash table cannot call back
into Red while it is probing. A class whose instances should key by value
provides something to key on, and the program uses that:

```red
class Point {
  init(x, y) { this.x = x; this.y = y; }
  key() { return "${this.x},${this.y}"; }
}

const seen = {};
seen.set(Point(1, 2).key(), "visited");
print(seen.get(Point(1, 2).key(), "no"));   // visited
```

## Errors

```red
try {
  risky();
} catch (e) {
  print(e.message);
}
```

`throw` raises any value. A value that is not already an error is wrapped
in one.

```red
throw "something broke";
throw error("bad input", {"field": "age"});
```

A caught error has four properties:

| Property | Meaning |
|---|---|
| `message` | The text. |
| `kind` | What sort of failure it is. See below. |
| `trace` | The call stack where it was raised. |
| `payload` | The value that was thrown, or the second argument to `error()`. |

Errors raised by the runtime are caught the same way:

```red
try {
  const x = 1 / 0;
} catch (e) {
  print(e.message);       // Division by zero.
}
```

### Kinds

Every error carries a kind, so a program can tell one failure from
another without reading the message. The runtime uses this fixed set:

| Kind | Raised by |
|---|---|
| `type` | An operation applied to the wrong type. |
| `name` | An undefined variable, property, method or member. |
| `index` | An array or string index out of range. |
| `key` | A value that cannot be a map key. |
| `arity` | The wrong number of arguments. |
| `zero-division` | Dividing or taking a remainder by zero. |
| `overflow` | Running out of call frames or stack. |
| `import` | A module that cannot be found or compiled. |
| `assert` | A failed `assert`. |
| `io` | File operations. |
| `net` | Sockets. |
| `task` | Channels and tasks. |
| `ffi` | Loading or calling an extension. |
| `legacy` | Running a v1 script. |
| `user` | `throw` of anything that is not a class instance. |
| `runtime` | Anything else. |

A thrown class instance takes that class's name as its kind, and its
`message` field as the message if it has one. `error(message, payload,
kind)` sets a kind explicitly.

### Several catch clauses

A `try` can have more than one `catch`. Each may carry a filter after a
colon, and the first clause that matches runs.

```red
try {
  loadConfig(path);
} catch (e: ConfigError) {
  report(e.message);
} catch (e: "io") {
  report("could not read ${path}");
} catch (e) {
  report("unexpected: ${e.kind}");
}
```

A filter is an ordinary expression:

- a **string** matches the error's `kind`
- a **class** matches when the thrown value was an instance of it, or of
  a subclass

Clauses are tried in order, so put the specific ones first. A clause with
no filter catches everything and must come last.

An error that no clause matches carries on outwards unchanged, with its
message, kind and original trace. That is what lets a function handle the
cases it knows about and leave the rest alone.

### finally

A `finally` block runs on every way out of a `try`, whether or not
anything went wrong.

```red
const handle = open(path, "r");
try {
  return handle.read();
} catch (e: "io") {
  return nil;
} finally {
  handle.close();
}
```

It runs after the body falls off the end, after a `catch` clause, after
an error that no clause matched and before that error carries on, and
after a `catch` clause that threw something of its own. It also runs
before a `return`, `break` or `continue` leaves the block, and the exit
then continues as written.

Nested `try` blocks run their `finally` blocks innermost first.

A `try` may have a `finally` with no `catch`, which is the shape for
cleanup that does not handle anything.

An error with no handler stops the program, prints the message and the
call stack, and exits with code 70.

## Modules

Each file is a module. `import` loads one and binds it to a name.

```red
import "util.red";             // binds util, from the file stem
import "util.red" as helpers;  // binds helpers
print(util.double(21));
```

Everything a module declares at its top level is visible to whoever
imports it, and nothing else is. A file is loaded once, however many
times it is imported, and its state is shared by every importer. Two
modules may import each other: the second one to start sees the first in
its partly built state rather than looping forever.

Reading a name a module does not define raises an error. Module names
cannot be assigned to from outside.

A path resolves against the file doing the importing, not against the
working directory, so a project can be moved or run from anywhere. A path
that is not found there is looked for on the library search path, which
is how `import "cli.red"` finds a library that ships with the
interpreter. [libraries.md](libraries.md) describes the order, and
`library_paths()` prints it.

An import may name a compiled file, which loads a chunk instead of
running the compiler:

```red
import "util.redc" as util;
```

## Tasks and channels

`spawn` runs a call on its own task. A task is an operating system thread.

```red
fun work(n) { return n * n; }
const task = spawn work(9);
print(task.join());        // 81
```

`join` waits for the task and gives back its result. If the task failed,
`join` raises the error the task raised, with the kind and payload it was
raised with, so a `catch` clause around the join is the same one that
would have worked had the call been direct.

```red
fun read(path) { throw error("cannot read '${path}'", path, "io"); }

try {
  spawn read("data.txt").join();
} catch (e: "io") {
  print(e.message, e.payload);
}
```

A channel passes values between tasks.

```red
const jobs = chan(4);      // room for four queued values
const done = chan();       // unbuffered: send waits for a receiver

jobs.send(1);
print(jobs.recv());
jobs.close();
```

`recv` on a closed and empty channel gives `nil` forever, which is the
usual way to end a worker loop:

```red
fun worker(jobs, results) {
  for (;;) {
    const job = jobs.recv();
    if (job == nil) { break; }
    results.send(job * 2);
  }
}
```

Channel methods: `send` `recv` `try_recv` `close` `len` `is_closed`.
Task methods: `join` `is_done`.

Tasks share one heap and one lock. Two tasks do not compute at the same
time, but a task that waits on a channel or on input and output releases
the lock. [docs/design.md](design.md#concurrency) explains why.

## Running v1 code

```red
legacy("old_script.red");            // runs it, output goes to the terminal
const text = legacy_output("old.red");  // runs it and captures the output
print(legacy_available());           // is the v1 interpreter built?
```

From the command line:

```
red legacy old_script.red
```

## Grammar

```
program        -> declaration* EOF

declaration    -> classDecl | enumDecl | funDecl | varDecl | importDecl
                | statement
classDecl      -> "class" IDENT ( "<" IDENT )? "{" method* "}"
enumDecl       -> "enum" IDENT "{" enumMember ( "," enumMember )* ","? "}"
enumMember     -> IDENT ( "=" "-"? NUMBER )?
method         -> IDENT "(" parameters? ")" returnType? block
funDecl        -> "fun" IDENT "(" parameters? ")" returnType? block
varDecl        -> ( "let" | "const" )
                  ( IDENT annotation? ( "=" expression )?
                  | pattern "=" expression ) ";"
pattern        -> "[" ( patternItem ( "," patternItem )* )? ","?
                      ( "..." IDENT )? "]"
                | "{" patternField ( "," patternField )* ","? "}"
patternItem    -> IDENT | pattern
patternField   -> IDENT ( ":" ( IDENT | pattern ) )?
importDecl     -> "import" STRING ( "as" IDENT )? ";"

parameters     -> parameter ( "," parameter )* ( "," restParam )?
                | restParam
parameter      -> IDENT annotation? ( "=" expression )?
restParam      -> "..." IDENT
annotation     -> ":" IDENT ( "[" "]" )*
returnType     -> "->" IDENT

statement      -> exprStmt | ifStmt | whileStmt | forStmt | forInStmt
                | switchStmt | returnStmt | breakStmt | continueStmt
                | tryStmt | throwStmt | block
exprStmt       -> expression ";"
ifStmt         -> "if" "(" expression ")" statement ( "else" statement )?
whileStmt      -> "while" "(" expression ")" statement
forStmt        -> "for" "(" ( varDecl | exprStmt | ";" )
                        expression? ";" expression? ")" statement
forInStmt      -> "for" "(" "let" ( IDENT | pattern ) "in" expression ")"
                  statement
switchStmt     -> "switch" "(" expression ")" "{" switchCase* "}"
switchCase     -> "case" expression ( "," expression )* ":" declaration*
                | "default" ":" declaration*
returnStmt     -> "return" expression? ";"
breakStmt      -> "break" ";"
continueStmt   -> "continue" ";"
tryStmt        -> "try" block catchClause* ( "finally" block )?
catchClause    -> "catch" "(" IDENT ( ":" expression )? ")" block
throwStmt      -> "throw" expression ";"
block          -> "{" declaration* "}"

expression     -> assignment
assignment     -> ( call "." )? IDENT assignOp assignment
                | call "[" expression "]" assignOp assignment
                | logicOr
assignOp       -> "=" | "+=" | "-=" | "*=" | "/=" | "%="
logicOr        -> logicAnd ( "or" logicAnd )*
logicAnd       -> equality ( "and" equality )*
equality       -> comparison ( ( "==" | "!=" ) comparison )*
comparison     -> bitOr ( ( "<" | "<=" | ">" | ">=" ) bitOr )*
bitOr          -> bitXor ( "|" bitXor )*
bitXor         -> bitAnd ( "^" bitAnd )*
bitAnd         -> shift ( "&" shift )*
shift          -> term ( ( "<<" | ">>" ) term )*
term           -> factor ( ( "+" | "-" ) factor )*
factor         -> unary ( ( "*" | "/" | "%" ) unary )*
unary          -> ( "!" | "-" | "~" ) unary | spawn
spawn          -> "spawn" call "(" arguments? ")" | call
call           -> primary ( "(" arguments? ")"
                          | "." IDENT
                          | "[" expression "]" )*
primary        -> NUMBER | STRING | interpolation
                | "true" | "false" | "nil" | "this"
                | IDENT | "(" expression ")"
                | "[" arguments? "]"
                | "{" ( expression ":" expression ( "," ... )* )? "}"
                | "fun" "(" parameters? ")" block
                | "super" "." IDENT
interpolation  -> STRING_PART ( expression STRING_PART )* 
arguments      -> expression ( "," expression )*
```

Tokens:

```
NUMBER         -> DIGIT+ ( "." DIGIT+ )? ( ( "e" | "E" ) ( "+" | "-" )? DIGIT+ )?
                | "0" ( "x" | "X" ) HEX+
STRING         -> '"' ( character | escape )* '"'
escape         -> "\\" ( "n" | "t" | "r" | "0" | "\\" | '"' | "$"
                       | "u" HEX HEX HEX HEX
                       | "u" "{" HEX HEX? HEX? HEX? HEX? HEX? "}" )
IDENT          -> ( ALPHA | "_" ) ( ALPHA | DIGIT | "_" )*
```

A `\u` escape names a code point up to `10ffff` and is written into the
string as UTF-8. Half of a surrogate pair is refused, because UTF-8 has
no form for one.

## Reserved words

```
and    as       break  case   catch  class   const  continue
default else    enum   false  finally for    fun    if
import in      let     nil    or      return spawn  super
this   throw   true    try    switch  while
```

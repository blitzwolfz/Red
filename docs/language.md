# The Red language

A reference for Red v2. For a program that uses most of this, read
[`examples/tour.red`](../examples/tour.red).

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
| `number` | `1`, `-2.5`, `1e9` | One numeric type, a 64 bit float. |
| `string` | `"text"` | |
| `array` | `[1, 2, 3]` | |
| `map` | `{"a": 1}` | |

Only `nil` and `false` are false. Zero, the empty string and the empty
array are all true.

```red
if (0) { print("this runs"); }
```

Whole numbers print without a decimal point. `1.0` prints as `1`.

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
`\r`, `\0`, `\\`, `\"` and `\$`.

Any expression can be written inside `${ }`:

```red
const name = "world";
print("Hello, ${name}!");
print("2 + 2 = ${2 + 2}");
print("nested ${"in" + "ner"}");
```

A `$` that is not followed by `{` is an ordinary character, so `"$5"`
needs no escape.

Strings compare in dictionary order, and index by character:

```red
print("apple" < "banana");   // true
print("hello"[0]);           // h
print("hello"[-1]);          // o
```

## Operators

From loosest to tightest:

| Level | Operators |
|---|---|
| assignment | `=` |
| or | `or` |
| and | `and` |
| equality | `==` `!=` |
| comparison | `<` `<=` `>` `>=` |
| term | `+` `-` |
| factor | `*` `/` `%` |
| unary | `!` `-` |
| call | `()` `.` `[]` |

`and` and `or` stop early and give back the operand that decided the
result:

```red
print(nil or "fallback");    // fallback
print(false and boom());     // false, boom() is never called
```

`+` adds numbers or joins strings. It will not mix the two. Use `str()`
or interpolation.

`/` and `%` by zero raise an error rather than giving infinity.

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

`break` leaves the loop. `continue` goes to the next step. In a `for`
loop `continue` still runs the increment.

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

A caught error has three properties:

| Property | Meaning |
|---|---|
| `message` | The text. |
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

An error with no handler stops the program, prints the message and the
call stack, and exits with code 70.

## Modules

Each file is a module. `import` loads one and binds it to a name.

```red
import "util.red";             // binds util
import "util.red" as helpers;  // binds helpers
print(util.double(21));
```

Paths resolve against the importing file, not the working directory. A
file is loaded once, however many times it is imported, and everything it
defines at the top level is shared by every importer.

Reading a name a module does not define raises an error. Module names
cannot be assigned to from outside.

## Tasks and channels

`spawn` runs a call on its own task. A task is an operating system thread.

```red
fun work(n) { return n * n; }
const task = spawn work(9);
print(task.join());        // 81
```

`join` waits for the task and gives back its result. If the task failed,
`join` raises that error in the joining task.

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

declaration    -> classDecl | funDecl | varDecl | importDecl | statement
classDecl      -> "class" IDENT ( "<" IDENT )? "{" method* "}"
method         -> IDENT "(" parameters? ")" returnType? block
funDecl        -> "fun" IDENT "(" parameters? ")" returnType? block
varDecl        -> ( "let" | "const" ) IDENT annotation? ( "=" expression )? ";"
importDecl     -> "import" STRING ( "as" IDENT )? ";"

parameters     -> IDENT annotation? ( "," IDENT annotation? )*
annotation     -> ":" IDENT ( "[" "]" )*
returnType     -> "->" IDENT

statement      -> exprStmt | ifStmt | whileStmt | forStmt | returnStmt
                | breakStmt | continueStmt | tryStmt | throwStmt | block
exprStmt       -> expression ";"
ifStmt         -> "if" "(" expression ")" statement ( "else" statement )?
whileStmt      -> "while" "(" expression ")" statement
forStmt        -> "for" "(" ( varDecl | exprStmt | ";" )
                        expression? ";" expression? ")" statement
returnStmt     -> "return" expression? ";"
breakStmt      -> "break" ";"
continueStmt   -> "continue" ";"
tryStmt        -> "try" block "catch" "(" IDENT ")" block
throwStmt      -> "throw" expression ";"
block          -> "{" declaration* "}"

expression     -> assignment
assignment     -> ( call "." )? IDENT "=" assignment
                | call "[" expression "]" "=" assignment
                | logicOr
logicOr        -> logicAnd ( "or" logicAnd )*
logicAnd       -> equality ( "and" equality )*
equality       -> comparison ( ( "==" | "!=" ) comparison )*
comparison     -> term ( ( "<" | "<=" | ">" | ">=" ) term )*
term           -> factor ( ( "+" | "-" ) factor )*
factor         -> unary ( ( "*" | "/" | "%" ) unary )*
unary          -> ( "!" | "-" ) unary | spawn
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

## Reserved words

```
and    as     break  catch  class  const  continue  else
false  for    fun    if     import let    nil       or
return spawn  super  this   throw  true   try       while
```

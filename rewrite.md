# Red rewrite notes

## why im doing this

Red is the little language I built in Java following the Crafting Interpreters book. Scanner, parser, resolver, then an interpreter that just walks the tree. It works and has closures, classes, inheritance and all that. But honestly it just looks like I followed a tutorial. I want to put this in my portfolio for systems jobs so I need it to actually show systems skills, not just "I can write a parser."

So the plan is to rewrite it in C++ with a real bytecode VM, my own memory management, and some concurrency. Something I can actually explain in an interview instead of just saying "yeah I followed a book."

## what v1 has right now

- written in Java, code is in redlang/
- pipeline is Scanner -> Parser -> Resolver -> Interpreter, its a tree walker
- has: variables, math/logic ops, if/while/for loops, functions with closures, classes with init and methods, this, single inheritance, and one built in function called clock()
- doesnt have: bytecode, any memory management of my own (its just using javas garbage collector), concurrency, a standard library really, tests, or benchmarks
- about 2800 lines across 17 files, and the readme literally just says "Red"

Might keep the old java version in a legacy folder just so people can see the before and after.

## what im building

**Language: C++.** No garbage collector to rely on this time so I have to write my own heap and my own GC. Thats kind of the whole point, thats the memory management stuff systems jobs actually care about. Gonna use modern C++ but not hide everything behind smart pointers, the GC and the value stuff is where I actually want to use raw pointers and manage memory myself so I can talk about it later.

**Compiler + VM.** Turn the source code into bytecode instructions instead of walking a tree. Stack based VM to start since its simpler to get right and easier to explain. Might try a register based one later if I have time, thats more of a performance flex.

**Memory management.** This is the main thing. Need some kind of value representation, probably a tagged struct first since its safer, maybe NaN boxing later if I want to show off more. Then a mark and sweep garbage collector that runs when memory gets allocated too much, with roots coming from the stack, globals, and call frames. If theres time maybe add arenas or a generational GC but mark and sweep is the minimum, not optional.

**Concurrency.** At least threads with channels, kind of like Go, since thats easy to explain and comes up in interviews a lot. If I have time after that, maybe try green threads or a small scheduler so I can show I actually understand how that works and not just calling std::thread.

**Standard library.** File reading/writing, env variables, args, exit codes, tcp sockets, and some way for Red to call out to C++ functions. This is what makes it actually useful and not just a toy.

**Tooling.** A repl that works with multiple lines, a way to print out the compiled bytecode, a trace mode that shows every instruction as it runs, and actual error messages with line numbers instead of just crashing.

**Language changes.** Keeping the basic syntax the same, curly braces, fun, classes, closures, that part is fine. Adding real arrays and maps as actual types, string interpolation, let vs const, some kind of try/catch instead of just crashing on errors, optional type hints on functions (not actually checked, just for readability), and being able to split code across multiple files.

## what im not doing

- not trying to make it fast, its not competing with real languages
- no JIT, thats a someday thing
- no real type checking, just hints
- no package manager or anything like that
- not worried about old red scripts still working, syntax can change if it makes things better

## why this actually helps with jobs

- GC and memory stuff = memory management skills
- benchmarks against python or lua and the trace mode = performance work
- threads and channels = concurrency knowledge
- bytecode stuff and maybe NaN boxing = low level bit manipulation
- doing all of this in C++ with raw pointers where it matters = actual C++ skills not just syntax
- repl, disassembler, trace mode = debugging and tooling
- sockets and calling out to C++ = networking and systems integration
- having tests and CI = shows I actually test my code

## order im doing things in

1. lock down the syntax changes so I dont redesign stuff halfway through
2. get the compiler and VM to do everything v1 could do, no GC yet, just let it leak memory for now and note that
3. memory management, value type plus the GC. not skipping this or rushing it
4. concurrency and standard library stuff, threads, channels, file io, sockets
5. tooling, repl, disassembler, trace mode, better errors
6. make it look like an actual portfolio project, good readme with a diagram, write up why I made the choices I made, benchmark numbers in the repo, maybe a gif, CI badge

## how ill know its done

- someone can clone it, build it, run the benchmarks and get real numbers in like 5 minutes
- I can explain the whole architecture out loud without looking anything up, and explain why not just what
- CI passes every time I push
- most of the "why this helps with jobs" list above is actually true and not just stuff I hope happens

## stuff im still not sure about

- if im running low on time, do I just do reference counting instead of a real GC. its weaker but might be more realistic. not deciding yet
- keeping the name Red obviously
- not sure if I delete the old java code or just move it to a legacy folder, probably just move it

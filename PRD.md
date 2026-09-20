# Red — PRD: Rewrite as a Systems-Grade Scripting Language

## 1. Summary
Red currently exists as a jlox-style tree-walking interpreter (Java): hand-written scanner, recursive-descent parser, a resolver pass, and a tree-walking evaluator. It supports variables, closures, control flow, functions, and single-inheritance classes. That's a solid *compilers* portfolio piece, but it reads as "I followed Crafting Interpreters," not "I can do systems work."

This PRD scopes a v2 rewrite whose explicit purpose is to be portfolio evidence for **systems engineering roles** — not just language-design roles. The deliverable is a bytecode-compiled VM with a real memory model, real concurrency, and real tooling, written in a systems language, with artifacts (benchmarks, writeup, demo) that make the systems work legible to a reviewer skimming a GitHub repo for 90 seconds.

## 2. Motivation
- **Why:** demonstrate low-level skills systems job postings actually screen for — memory management, performance work, concurrency primitives, debugging tooling, C interop — which a tree-walking Java interpreter does not exercise.
- **Why now:** v1 already proves front-end skills (lexing/parsing/scoping/OOP semantics). The unclaimed value is in the back end: bytecode, allocation, and runtime.

## 3. Goals
1. Compile Red source to a custom bytecode and execute it on a stack-based VM (no tree-walking in the hot path).
2. Implement memory management by hand: a tagged/boxed value representation and a real garbage collector (mark-sweep, minimum bar).
3. Ship at least one concurrency primitive that isn't "call into the host language's threads and hope" — green threads/channels or OS threads + explicit synchronization, implemented and explained.
4. Provide a small systems-flavored standard library (file I/O, sockets, process/env access) via FFI to the host systems language.
5. Ship developer tooling: REPL, bytecode disassembler, and at least basic runtime diagnostics (stack traces with line info, `--trace` execution mode).
6. Produce benchmark numbers against at least one reference interpreter (CPython and/or Lua) for a handful of workloads (fib, loop-heavy, string-heavy).
7. Package the whole thing as a legible portfolio artifact: README with architecture diagram, a design-decisions writeup, tests, CI.

## 4. Non-Goals
- Not competing with production language runtimes on raw performance.
- Not building a JIT (call out as explicit future work / stretch, not a v2 requirement).
- Not full static typing / type inference — optional type annotations at most (see §8.7).
- Not a package manager / ecosystem. One binary, one language, no registry.
- Not preserving perfect backward compatibility with v1 `.red` scripts — breaking syntax changes are acceptable if they improve the story (see §8.7).

## 5. Current State (v1) — baseline being replaced
- Language: Java (`redlang/` package), generated AST (`tool/GenerateAst.java` → `Expr.java`, `Stmt.java`).
- Pipeline: `Scanner` → `Parser` → `Resolver` (scope/binding analysis) → `Interpreter` (tree-walking, visitor pattern).
- Features: `var`, arithmetic/logical/comparison ops, `if/else`, `while`, `for`, functions with closures, `return`, classes with `init`, methods, `this`, single inheritance (`<`), one native function (`clock()`).
- No bytecode, no explicit memory management (relies on the JVM heap/GC entirely), no concurrency, no stdlib beyond `print` and `clock`, no tests, no benchmarks, minimal README.
- ~2,800 lines total across 17 files.

This is the thing being leveled up — not thrown away conceptually. The front-end (scanning/parsing/scope resolution) is reusable knowledge even if every line is rewritten.

## 6. Target Audience
Primary: engineers/hiring managers screening candidates for systems, infrastructure, platform, or runtime/compilers roles, evaluating this repo in minutes via README + a skim of source structure + maybe running the benchmark script.
Secondary: the author, as a forcing function to actually learn the material (this should hold up in a follow-up technical interview about it — assume they will ask "why mark-sweep and not generational?").

## 7. Success Criteria
- A reviewer can clone, build, and run `red bench` and get real numbers in under 5 minutes.
- The README's architecture section can be explained out loud, from memory, in an interview, in under 3 minutes, including *why* each major decision was made (not just what was built).
- At least 3 of the "skills-to-role" mapping items in §9 are true and demonstrable, not aspirational.
- CI is green: build + full test suite on every push.

## 8. Proposed Design

### 8.1 Implementation language
**Decision: C++ (author's choice).** No GC in the host runtime to lean on — Red's heap, GC, and value representation are all hand-rolled, which is exactly the manual-memory-management story systems roles screen for. Use it deliberately as a portfolio signal: modern C++ (C++17/20 — smart pointers where they don't hide the interesting parts, raw pointers/manual lifetime management where the whole point is to show you can do it correctly, e.g. inside the GC and the value representation). Build with CMake. Document where and why raw `new`/`delete` or placement-new is used deliberately instead of RAII, since a reviewer with a systems background will look for exactly that judgment call.

### 8.2 Architecture: single-pass or two-pass compiler → bytecode → stack VM
- Compiler emits a flat bytecode instruction stream (op + operands), constant pool per chunk, line-number table for error reporting.
- VM is a stack machine (simpler to implement correctly and to explain than register-based; register-based is a valid stretch goal for the performance-work narrative).
- Keep the existing recursive-descent parser design from v1 conceptually (Pratt parsing for expressions), targeting bytecode emission instead of an AST walk.

### 8.3 Memory management (the centerpiece)
- Values: a tagged union (`struct` with a type tag + union payload) or NaN-boxed `double`-sized representation (pick one, document the tradeoff — NaN-boxing is the more "I understand IEEE-754 and can do bit-level tricks" flex, tagged union is simpler and safer to get right first, and more idiomatic C++).
- Heap-allocated objects (strings, closures, class instances) tracked via an intrusive linked list or arena for GC traversal.
- Garbage collector: mark-and-sweep at minimum, triggered on allocation thresholds; document is it precise or conservative, and where roots come from (VM stack, globals, call frames).
- Stretch: generational GC, or a simple bump-allocator arena per "frame" to demonstrate arena-allocation knowledge even without a full generational collector.
- This section needs the most explicit "why" writeup — memory management is the #1 thing this rewrite exists to prove.

### 8.4 Concurrency model
- Minimum bar: OS-thread-backed "tasks" with channels for message passing (Go-style, easy to explain, maps directly to interview questions about concurrency primitives).
- Stretch: a small cooperative scheduler (green threads / coroutines) to show understanding of stack switching and schedulers, not just "I called `std::thread::spawn`."
- Must include one non-trivial demo program in Red itself that uses concurrency (e.g., a concurrent TCP echo server, or a parallel word-count).

### 8.5 Standard library & systems integration
- File I/O (read/write/append), environment variables, process args, exit codes.
- Sockets: at minimum TCP client/server, exposed as Red built-ins.
- FFI: a documented mechanism for Red to call into host-language functions, even if minimal (this is what makes "systems integration" a real bullet point, not just "has functions").

### 8.6 Tooling
- REPL with multi-line input support.
- `red disasm <file>` — dumps compiled bytecode in human-readable form.
- `--trace` execution mode — prints each instruction executed and stack state (useful for debugging and for demoing "I understand my own VM" in an interview).
- Structured runtime errors: stack traces with source line numbers, not raw panics.

### 8.7 Language surface (syntax/semantics) — what changes from v1
Keep what already works conceptually (C-like block syntax, `fun`, classes, closures) but modernize the parts that read as "toy":
- First-class arrays and maps/dicts as built-in types (v1 has none).
- String interpolation.
- Explicit `let`/`const` distinction (immutability signal — relevant to systems-minded reviewers).
- Structured error handling (`Result`-style return or `try`/`catch`) instead of only host-language exceptions leaking through.
- Optional type annotations on function signatures (purely advisory/documentation in v2 — not enforced — explicitly deferred to non-goals for actual type checking).
- Module/import system for splitting programs across files.

## 9. Skills-to-role mapping (why this gets you hired)
| Systems job requirement | Red v2 feature that demonstrates it |
|---|---|
| Memory management / GC internals | Hand-written mark-sweep collector, tagged/NaN-boxed values (§8.3) |
| Performance engineering | Benchmark suite vs. CPython/Lua, `--trace` profiling mode |
| Concurrency & synchronization | Threads/channels or coroutine scheduler + demo server (§8.4) |
| Low-level bit manipulation / data layout | Value representation, bytecode instruction encoding |
| C++ proficiency, manual memory-management judgment | Whole VM + GC implementation, narrated raw-pointer/RAII tradeoffs |
| Debugging & tooling instincts | REPL, disassembler, trace mode, structured stack traces |
| Networking / OS-adjacent code | Socket stdlib, FFI layer |
| Testing discipline | Conformance test suite + CI |

## 10. Roadmap (sized, not dated — fit to actual available time)
- **M0 — Spec freeze (S):** finalize language surface changes (§8.7) and lock implementation language (§8.1 open question).
- **M1 — Compiler + VM parity (L):** bytecode compiler and stack VM reach feature parity with v1 (vars, control flow, functions, closures, classes) — no GC yet, leak memory intentionally, note it as a known gap.
- **M2 — Memory management (L):** value representation + mark-sweep GC. This is the milestone that matters most for the stated goal; do not compress it.
- **M3 — Concurrency + stdlib (M):** threads/channels, file I/O, sockets, FFI hook.
- **M4 — Tooling (M):** REPL, disassembler, trace mode, error reporting polish.
- **M5 — Portfolio polish (S):** README + architecture diagram, design-decisions writeup (blog-post quality), benchmark script + published numbers, demo GIF/asciicast, CI badge.

## 11. Portfolio deliverables checklist
- [ ] README: what it is, architecture diagram, how to build/run, benchmark numbers up top.
- [ ] `docs/design.md` (or blog post): the *why* behind value representation, GC choice, concurrency model — this is what turns "did a project" into "can discuss tradeoffs."
- [ ] Benchmark script + results table, checked in.
- [ ] Test suite (interpreter conformance tests, ideally a golden-file suite) + CI running it on every push.
- [ ] At least one non-trivial demo program in Red showing off concurrency + stdlib together.
- [ ] Short demo (GIF or asciicast) of the REPL and `--trace` mode.

## 12. Risks & Open Questions
- **Scope risk:** a real GC + concurrency + FFI is a multi-month project done well; if time-boxed, M2 (memory management) should be protected over M3/M4 — it's the highest-signal piece for the stated goal.
- **Open:** keep the name "Red," or rebrand for the portfolio relaunch? Assumed: keep it, it's already the repo/brand.
- **Open:** is a from-scratch GC in scope, or is "GC-lite" (e.g., reference counting with a cycle-breaking escape hatch) an acceptable substitute if time is short? RC is easier but a weaker signal for "understands tracing GC" — flagged, not decided here.
- **Risk:** v1's Java code and v2's rewrite will diverge completely; decide whether v1 stays in the repo (e.g., under `legacy/`) as "here's where I started" narrative value, or gets removed. Recommend keeping it — the before/after is itself a portfolio point.

## 13. Assumptions Log
- Implementation language: C++ (per author, confirmed — §8.1).
- GC minimum bar: mark-and-sweep, not reference counting.
- Concurrency minimum bar: OS threads + channels, coroutines are stretch.
- Name stays "Red."
- No package manager / module registry in scope.
- No JIT in scope.

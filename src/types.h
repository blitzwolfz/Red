// Types, as values.
//
// A type annotation is not a comment. `fun area(r: Num) -> Num` compiles
// to a check on the way in and a check on the way out, so a wrong
// argument is reported where it was passed rather than three frames
// later where it was finally added to something.
//
// A type is an ordinary heap value, so `Num` is a name that evaluates to
// something, `type_of(x)` hands one back, and `x is Num` asks a question
// about one. Writing `: Num` is reading that same value at compile time.
//
// The grammar is in docs/language.md. In short:
//
//   Any Nil Bool Num Int String Array Map Set Fun Error
//   [T]              an array whose elements are all T
//   {K: V}           a map with keys K and values V
//   fun(A, B) -> C   something callable, with that shape
//   T?               T, or nil
//   Point            an instance of that class, or of one derived from it
#pragma once

#include <string>

#include "object.h"
#include "value.h"

namespace red {

class VM;

// Tests `value` against `type`. Fills `reason` with what went wrong,
// phrased to go straight into an error message.
//
// An Array or Map with element types walks what it is given, because
// otherwise `[Num]` would mean no more than `Array`. That is linear in
// the size of the container, which is worth knowing before putting an
// annotation on a hot path: `Array` costs one tag comparison, `[Num]`
// costs a pass over the elements.
// `where` is the module the check was compiled into, which is where a
// class or enum name is looked up. A name binds once, so the answer is
// kept on the type and the module only matters the first time.
bool typeMatches(Runtime& runtime, ObjModule* where, ObjTypeDesc* type,
                 Value value, std::string* reason);

// The canonical spelling, which is what the compiler stores for the
// disassembler and what an error message shows.
std::string typeName(ObjTypeDesc* type);

// The type a value has, as a value. Built fresh, so the answer for an
// array describes the elements that are in it now.
ObjTypeDesc* typeOf(Runtime& runtime, Value value);

// Can a literal be measured against this type without running the
// program? Everything but a class or enum name is settled by the
// spelling alone; those are bound while the program runs, so a value
// measured against one waits for the check at run time.
//
// The compiler uses this to decide what it may report while compiling.
// The self-hosted compiler answers the same question from the canonical
// text, so the two agree about which programs are rejected.
bool typeIsStaticallyKnown(ObjTypeDesc* type);

// Builds a type from its canonical spelling: "Num", "[String]",
// "{String: Num}", "fun(Num) -> Num", "Point?". Returns nullptr when the
// text is not a type, which a compiler should never produce.
//
// This is the only place types are built from, so the compiler's job is
// to turn tokens into canonical text and nothing more. That is also what
// goes into a compiled file, which is why the self-hosted compiler needs
// to agree about the spelling and about nothing else.
ObjTypeDesc* parseTypeText(Runtime& runtime, const std::string& text);

// Defines the simple type names as globals, so that `Num` is something a
// program can pass around, print and compare, and not only something it
// can write after a colon.
void installTypeNames(Runtime& runtime);

// Builds the simple types. Called once, when the built-ins are
// installed.
void installTypes(Runtime& runtime);

// The shared instance of a type that takes no parameters. These are
// made once and handed out, so `Num` is one object however often it is
// written.
ObjTypeDesc* simpleType(Runtime& runtime, TypeKind kind);

}  // namespace red

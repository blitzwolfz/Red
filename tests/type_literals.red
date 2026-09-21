// A literal that cannot fit its annotation is caught while compiling.
//
// One pass with no tree to walk means this reaches what is written right
// there and no further. Everything else is caught when the value arrives,
// which tests/types.red covers. docs/language.md says where the line is.

let count: Int = 1.5;
let name: String = 3;
let flag: Bool = nil;

fun wrong() -> Num { return "no"; }

let n: Num = 1;
n = "not a number";
// expect compile error: expected Int, got the number 1.5.
// expect compile error: expected String, got number.
// expect compile error: expected Bool, got nil.
// expect compile error: expected Num, got string.
// expect compile error: expected Num, got string.

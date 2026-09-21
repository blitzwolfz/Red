// print writes to standard output; eprint writes to standard error.
//
// Nothing below asserts what reached the error stream, because the
// runner only matches expected errors there. What it does assert is that
// eprint's output does not turn up on standard output: an unexpected
// line there is a failure.

eprint("this belongs on the error stream");
ewrite("so does this");
ewrite(", and this\n");

print("this is output");                  // expect: this is output
write("built ");
write("from parts");
print("");                                // expect: built from parts

// Both take several arguments, the same way print does.
eprint("one", 2, [3]);
print("one", 2, [3]);                     // expect: one 2 [3]

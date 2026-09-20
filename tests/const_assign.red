// Assigning to a const binding is caught while compiling, whether the
// assignment is plain or compound.
const answer = 42;
answer = 43;
answer += 1;
// expect compile error: Cannot assign to a const binding.

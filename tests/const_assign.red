// Assigning to a const binding is caught while compiling.
const answer = 42;
answer = 43;
// expect compile error: Cannot assign to a const binding.

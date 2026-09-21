// Switching one fiber's machine stack for another's.
//
// This is the whole of the machine-dependent part of the scheduler. A
// fiber is a stack and a stack pointer; switching between two of them
// means saving the registers the calling convention says a function must
// preserve, swapping the stack pointer, and restoring the other set.
// Everything else the fiber owns -- its VM, its value stack, its place
// in the collector's list of threads -- is ordinary C++ next door in
// sched.cpp.
//
// The libc way to do this is makecontext/swapcontext, which is
// deprecated on macOS and saves and restores the signal mask on every
// switch: a system call, on a path that should be a few dozen
// instructions. Forty lines of assembly are cheaper and do not go out
// from under us.
//
//   void red_fiber_swap(void** saveStackPointer, void* newStackPointer);
//
// The stack a fiber has never run on is built by red_fiber_prepare() in
// sched.cpp to look exactly like one this function has just saved, with
// the return address pointing at the entry trampoline.

#if defined(__APPLE__)
#define RED_SYMBOL(name) _##name
#else
#define RED_SYMBOL(name) name
#endif

#if defined(__x86_64__)

.text
.globl RED_SYMBOL(red_fiber_swap)
#if !defined(__APPLE__)
.type RED_SYMBOL(red_fiber_swap), @function
#endif
.align 16
RED_SYMBOL(red_fiber_swap):
  // System V says rbx, rbp and r12-r15 survive a call. The return
  // address is already on the stack, put there by whoever called us.
  pushq %rbp
  pushq %rbx
  pushq %r12
  pushq %r13
  pushq %r14
  pushq %r15
  movq  %rsp, (%rdi)
  movq  %rsi, %rsp
  popq  %r15
  popq  %r14
  popq  %r13
  popq  %r12
  popq  %rbx
  popq  %rbp
  // Returns onto the other fiber's stack: either back into its own call
  // to this function, or into the entry trampoline of one that has not
  // run before.
  ret

#elif defined(__aarch64__) || defined(__arm64__)

.text
.globl RED_SYMBOL(red_fiber_swap)
#if !defined(__APPLE__)
.type RED_SYMBOL(red_fiber_swap), %function
#endif
.p2align 2
RED_SYMBOL(red_fiber_swap):
  // AAPCS64: x19-x28, the frame pointer, the link register and the low
  // halves of v8-v15 survive a call. 160 bytes holds all of them.
  stp x29, x30, [sp, #-160]!
  stp x19, x20, [sp, #16]
  stp x21, x22, [sp, #32]
  stp x23, x24, [sp, #48]
  stp x25, x26, [sp, #64]
  stp x27, x28, [sp, #80]
  stp d8,  d9,  [sp, #96]
  stp d10, d11, [sp, #112]
  stp d12, d13, [sp, #128]
  stp d14, d15, [sp, #144]
  mov x2, sp
  str x2, [x0]
  mov sp, x1
  ldp x19, x20, [sp, #16]
  ldp x21, x22, [sp, #32]
  ldp x23, x24, [sp, #48]
  ldp x25, x26, [sp, #64]
  ldp x27, x28, [sp, #80]
  ldp d8,  d9,  [sp, #96]
  ldp d10, d11, [sp, #112]
  ldp d12, d13, [sp, #128]
  ldp d14, d15, [sp, #144]
  ldp x29, x30, [sp], #160
  ret

#else
#error "Red's fiber scheduler has no context switch for this architecture."
#endif

#if defined(__linux__)
// Nothing here wants an executable stack, and saying so keeps the
// linker from marking the whole program's as one.
.section .note.GNU-stack,"",@progbits
#endif

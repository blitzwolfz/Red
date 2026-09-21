// Which fiber and which worker the calling operating system thread is
// running, kept behind functions in a translation unit of their own.
//
// This is not an abstraction for its own sake. A fiber can be resumed by
// a different worker thread than the one that suspended it -- that is
// what work stealing means -- so every thread-local read after a context
// switch has to come from the thread that is running now, not from the
// one that was. A compiler cannot know that red_fiber_swap() changes
// threads, and will happily compute the address of a thread_local once
// and reuse it on both sides of the call.
//
// Putting the accessors in another file, and telling the compiler not to
// inline them, is what stops that: the address is worked out inside the
// call, on whichever thread is making it.
#pragma once

namespace red {

__attribute__((noinline)) void* schedulerCurrentFiber();
__attribute__((noinline)) void schedulerSetCurrentFiber(void* fiber);
__attribute__((noinline)) void* schedulerCurrentWorker();
__attribute__((noinline)) void schedulerSetCurrentWorker(void* worker);

}  // namespace red

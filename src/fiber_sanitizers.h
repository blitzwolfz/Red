// Telling the sanitizers about fiber switches.
//
// AddressSanitizer and ThreadSanitizer both keep per-stack bookkeeping
// and both assume a thread has one stack. Switching stacks underneath
// them without saying so produces a flood of reports that are all the
// same false positive: a stack they were watching has apparently been
// replaced by memory they have never seen.
//
// Both provide an interface for exactly this. Compiled out entirely
// unless the sanitizer in question is on, so an ordinary build has none
// of it.
#pragma once

#include <cstddef>

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define RED_ASAN_FIBERS 1
#endif
#if __has_feature(thread_sanitizer)
#define RED_TSAN_FIBERS 1
#endif
#endif
#if defined(__SANITIZE_ADDRESS__)
#define RED_ASAN_FIBERS 1
#endif
#if defined(__SANITIZE_THREAD__)
#define RED_TSAN_FIBERS 1
#endif

#if defined(RED_ASAN_FIBERS)
extern "C" {
void __sanitizer_start_switch_fiber(void** fakeStackSave, const void* bottom,
                                    size_t size);
void __sanitizer_finish_switch_fiber(void* fakeStackSave,
                                     const void** bottomOld, size_t* sizeOld);
}
#endif

#if defined(RED_TSAN_FIBERS)
extern "C" {
void* __tsan_get_current_fiber(void);
void* __tsan_create_fiber(unsigned flags);
void __tsan_destroy_fiber(void* fiber);
void __tsan_switch_to_fiber(void* fiber, unsigned flags);
}
#endif

namespace red {

// What one side of a switch has to remember between leaving and coming
// back. Every field is unused in a build with neither sanitizer on.
struct SanitizerContext {
  void* fakeStack = nullptr;
  void* tsanFiber = nullptr;
  const void* stackBottom = nullptr;
  size_t stackSize = 0;
};

// These four are marked so that the sanitizers do not instrument them.
//
// ThreadSanitizer keeps a shadow call stack per fiber, and pushes and
// pops it on every instrumented function's entry and exit. A function
// that switches fibers halfway through would push onto one and pop from
// the other, and a few tens of thousands of switches later the mismatch
// is an overflow inside TSAN itself. Leaving these uninstrumented keeps
// every push and its pop on the same shadow stack.
#define RED_SWITCH_HELPER \
  __attribute__((no_sanitize("thread"), no_sanitize("address"))) inline

// Called on a context that is about to switch away and be resumed
// later. `to` is where control is going.
RED_SWITCH_HELPER void sanitizerLeaving(SanitizerContext* from,
                             const SanitizerContext* to) {
#if defined(RED_ASAN_FIBERS)
  __sanitizer_start_switch_fiber(&from->fakeStack, to->stackBottom,
                                 to->stackSize);
#else
  (void)from;
  (void)to;
#endif
#if defined(RED_TSAN_FIBERS)
  if (to->tsanFiber != nullptr) __tsan_switch_to_fiber(to->tsanFiber, 0);
#endif
}

// Called on a context that is about to switch away for the last time.
// Nothing needs to be saved, because nothing will come back to it.
RED_SWITCH_HELPER void sanitizerLeavingForGood(const SanitizerContext* to) {
#if defined(RED_ASAN_FIBERS)
  __sanitizer_start_switch_fiber(nullptr, to->stackBottom, to->stackSize);
#else
  (void)to;
#endif
#if defined(RED_TSAN_FIBERS)
  if (to->tsanFiber != nullptr) __tsan_switch_to_fiber(to->tsanFiber, 0);
#endif
}

// Called immediately after arriving on a context, whether it is starting
// or being resumed.
RED_SWITCH_HELPER void sanitizerArrived(SanitizerContext* here) {
#if defined(RED_ASAN_FIBERS)
  __sanitizer_finish_switch_fiber(here->fakeStack, nullptr, nullptr);
  here->fakeStack = nullptr;
#else
  (void)here;
#endif
}

// Records the stack a context runs on, so the other side can name it.
inline void sanitizerSetStack(SanitizerContext* context, const void* bottom,
                              size_t size) {
  context->stackBottom = bottom;
  context->stackSize = size;
}

inline void sanitizerCreateFiber(SanitizerContext* context) {
#if defined(RED_TSAN_FIBERS)
  context->tsanFiber = __tsan_create_fiber(0);
#else
  (void)context;
#endif
}

inline void sanitizerAdoptCurrentFiber(SanitizerContext* context) {
#if defined(RED_TSAN_FIBERS)
  context->tsanFiber = __tsan_get_current_fiber();
#else
  (void)context;
#endif
}

inline void sanitizerDestroyFiber(SanitizerContext* context) {
#if defined(RED_TSAN_FIBERS)
  if (context->tsanFiber != nullptr) __tsan_destroy_fiber(context->tsanFiber);
  context->tsanFiber = nullptr;
#else
  (void)context;
#endif
}

}  // namespace red

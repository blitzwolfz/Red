// The per-object lock. See ObjLock in object.h.
#include <thread>

#include "object.h"
#include "runtime.h"

namespace red {

namespace {

// Numbered from one, because zero in Obj::owner means the object is
// free. Assigned on first use rather than at thread creation, so a
// thread that never touches an aggregate never takes a number.
std::atomic<uint32_t> g_nextThreadId{1};
thread_local uint32_t t_threadId = 0;

// How many times to try before parking. Contention on one object is
// rare, and when it happens the other thread is usually a few
// instructions from letting go, so a short spin avoids the trip through
// the scheduler. After that, parking is what keeps a thread that is
// waiting from being a thread the collector waits for.
constexpr int kSpinsBeforeParking = 64;

}  // namespace

uint32_t currentThreadId() {
  if (t_threadId == 0) {
    t_threadId = g_nextThreadId.fetch_add(1, std::memory_order_relaxed);
  }
  return t_threadId;
}

void ObjLock::take(Obj* object) {
  int slot = held_[0] == nullptr ? 0 : 1;
  uint32_t self = currentThreadId();
  if (object->owner.load(std::memory_order_relaxed) == self) {
    // Already held by this thread, which is what happens when a
    // callback run by sort() reaches the array being sorted. The depth
    // goes up and the guard takes it down again, so the object is only
    // let go of by the outermost one.
    object->lockDepth++;
    held_[slot] = object;
    return;
  }

  int spins = 0;
  uint32_t expected = 0;
  while (!object->owner.compare_exchange_weak(expected, self,
                                              std::memory_order_acquire,
                                              std::memory_order_relaxed)) {
    expected = 0;
    if (++spins >= kSpinsBeforeParking) {
      spins = 0;
      // Parked for the wait, so a collection can go ahead while this
      // thread has nothing to do but wait.
      if (runtime_ != nullptr) runtime_->park();
      std::this_thread::yield();
      if (runtime_ != nullptr) runtime_->unpark();
    }
  }
  held_[slot] = object;
}

void ObjLock::release(Obj* object) {
  if (object->lockDepth > 0) {
    object->lockDepth--;
    return;
  }
  object->owner.store(0, std::memory_order_release);
}

ObjLock::ObjLock(Obj* object) : runtime_(Runtime::current()) {
  if (!runningInParallel() || object == nullptr) return;
  take(object);
}

ObjLock::ObjLock(Runtime& runtime, Obj* object) : runtime_(&runtime) {
  if (!runningInParallel() || object == nullptr) return;
  take(object);
}

ObjLock::ObjLock(Runtime& runtime, Value value) : runtime_(&runtime) {
  if (!runningInParallel() || !isObj(value)) return;
  take(asObj(value));
}

ObjLock::ObjLock(Runtime& runtime, Obj* first, Obj* second)
    : runtime_(&runtime) {
  if (!runningInParallel()) return;
  if (first == second) second = nullptr;
  if (first != nullptr && second != nullptr && second < first) {
    Obj* swap = first;
    first = second;
    second = swap;
  }
  if (first != nullptr) take(first);
  if (second != nullptr) take(second);
}

ObjLock::~ObjLock() {
  // In the reverse order they were taken, which for the two argument
  // form means the higher address first.
  for (int i = 1; i >= 0; i--) {
    if (held_[i] != nullptr) release(held_[i]);
  }
}

}  // namespace red

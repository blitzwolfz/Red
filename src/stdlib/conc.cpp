// Tasks and channels.
//
// A task is a fiber: its own stack and its own VM, scheduled onto a
// small set of operating system threads rather than getting one each.
// Spawning one costs a mapping and a queue push, so a program can have
// as many as it has work for.
//
// Everything here that waits does so by parking the fiber and handing
// its worker the next runnable one, so waiting costs a context switch
// rather than a thread. Outside the scheduler -- in the REPL, the
// debugger and the test runner -- a task is still an operating system
// thread, and the same functions wait on a condition variable instead.
// Both kinds can wait on the same channel at once.
//
// One mutex guards every channel and every task's completion. It is held
// only for the moment an operation takes, never while Red code runs, and
// never across a switch: parking releases it from the worker's own
// stack, after the fiber is off it. That is what closes the gap between
// deciding to wait and being ready to be woken.
#include <chrono>
#include <mutex>
#include <thread>

#include "../sched.h"
#include "../vm.h"
#include "builtins.h"

namespace red {

namespace {

// Takes the mutex that guards every channel and every task's finished
// flag, parking while it waits. A task can be parked for a collection
// while holding it: raising an error from inside a channel operation
// allocates, and allocating is where a collection starts.
std::unique_lock<std::mutex> lockTasks(VM& vm) {
  std::unique_lock<std::mutex> guard(vm.runtime().lock, std::defer_lock);
  if (guard.try_lock()) return guard;
  vm.park();
  guard.lock();
  vm.unpark();
  return guard;
}

// Waits until something changes the state this task is waiting on, with
// the guard released for the duration and held again on return.
//
// A fiber puts itself on `waiters` and parks: the scheduler releases the
// lock once the fiber is off its stack, so a wakeup cannot arrive in the
// gap. A task on a thread of its own waits on the shared condition
// variable, as it did before there was a scheduler.
//
// The caller must re-check what it was waiting for. Waking is a hint
// that something changed, not a promise about what.
void waitFor(VM& vm, std::vector<Fiber*>& waiters,
             std::unique_lock<std::mutex>& guard) {
  Fiber* self = Scheduler::current();
  if (self != nullptr) {
    // Announced before the lock goes, so a waker that takes the lock
    // next either finds this fiber on the list and wakes it, or came
    // first and left the value that made waking unnecessary.
    Scheduler::prepareToPark();
    waiters.push_back(self);
    guard.unlock();
    Scheduler::park();
    // Awake again, holding nothing.
    guard = lockTasks(vm);
    return;
  }
  vm.park();
  vm.runtime().cond.wait(guard);
  vm.unpark();
}

// Wakes everything waiting on a channel or a task, of either kind. The
// caller holds the mutex, which is what stops a fiber that is on its way
// to parking from being missed.
//
// Everything is woken rather than one of each: a woken task re-checks
// the condition and waits again if it was not the one meant, and getting
// that wrong is a hang rather than a slowdown.
void wakeAll(Runtime& runtime, std::vector<Fiber*>* first,
             std::vector<Fiber*>* second) {
  if (first != nullptr) {
    for (Fiber* fiber : *first) Scheduler::ready(fiber);
    first->clear();
  }
  if (second != nullptr) {
    for (Fiber* fiber : *second) Scheduler::ready(fiber);
    second->clear();
  }
  runtime.cond.notify_all();
}

void wakeChannel(VM& vm, ObjChannel* channel) {
  wakeAll(vm.runtime(), &channel->sendWaiters, &channel->recvWaiters);
}

Value nativeChan(VM& vm, int argCount, Value* args) {
  size_t capacity = 0;
  if (argCount > 0) {
    if (!isNumber(args[0]) || asNumber(args[0]) < 0) {
      return vm.failAs("type", "chan() expects a capacity of zero or more.");
    }
    capacity = (size_t)asNumber(args[0]);
  }
  // Capacity zero means unbuffered: a send waits for a matching receive.
  return objValue((Obj*)vm.runtime().newChannel(capacity));
}

Value nativeSleep(VM& vm, int, Value* args) {
  if (!isNumber(args[0])) {
    return vm.failAs("type", "sleep() expects a number, got %s.",
                     valueTypeName(args[0]));
  }
  double seconds = asNumber(args[0]);
  if (seconds <= 0) {
    Scheduler::yieldNow();
    return nilValue();
  }

  // Parked for the wait, so the collector never has to wait on a task
  // that is doing nothing. Inside the scheduler this is a timer and a
  // context switch; outside it, a sleeping thread.
  vm.park();
  Scheduler::sleepFor(seconds);
  vm.unpark();
  return nilValue();
}

// Hands this task's turn to another without waiting for anything.
Value nativeYield(VM& vm, int, Value*) {
  vm.park();
  Scheduler::yieldNow();
  vm.unpark();
  return nilValue();
}

Value channelSend(VM& vm, int, Value* args) {
  ObjChannel* channel = asChannel(args[0]);
  Value value = args[1];
  std::unique_lock<std::mutex> guard = lockTasks(vm);

  if (channel->closed) return vm.failAs("task", "send() on a closed channel.");

  if (channel->capacity == 0) {
    // Unbuffered: hand the value over, then wait until a receiver has
    // taken it before returning.
    channel->buffer.push_back(value);
    wakeChannel(vm, channel);
    channel->waiters++;
    while (!channel->buffer.empty() && !channel->closed) {
      waitFor(vm, channel->sendWaiters, guard);
    }
    channel->waiters--;
    return nilValue();
  }

  while (channel->buffer.size() >= channel->capacity && !channel->closed) {
    channel->waiters++;
    waitFor(vm, channel->sendWaiters, guard);
    channel->waiters--;
  }
  if (channel->closed) return vm.failAs("task", "send() on a closed channel.");
  channel->buffer.push_back(value);
  wakeChannel(vm, channel);
  return nilValue();
}

// Sends without waiting. Returns true when the value went in, false when
// the channel was full. A closed channel is still an error: a send that
// can never succeed is a mistake, not a full buffer.
Value channelTrySend(VM& vm, int, Value* args) {
  ObjChannel* channel = asChannel(args[0]);
  std::unique_lock<std::mutex> guard = lockTasks(vm);
  if (channel->closed) {
    return vm.failAs("task", "try_send() on a closed channel.");
  }
  // An unbuffered channel accepts a value only when somebody is already
  // waiting for it.
  if (channel->capacity == 0) {
    if (channel->recvWaiters.empty() && channel->waiters == 0) {
      return boolValue(false);
    }
  } else if (channel->buffer.size() >= channel->capacity) {
    return boolValue(false);
  }
  channel->buffer.push_back(args[1]);
  wakeChannel(vm, channel);
  return boolValue(true);
}

Value channelRecv(VM& vm, int, Value* args) {
  ObjChannel* channel = asChannel(args[0]);
  std::unique_lock<std::mutex> guard = lockTasks(vm);

  while (channel->buffer.empty() && !channel->closed) {
    channel->waiters++;
    waitFor(vm, channel->recvWaiters, guard);
    channel->waiters--;
  }
  // A closed and drained channel yields nil forever, which is what lets a
  // worker loop end with `while ((job = ch.recv()) != nil)`.
  if (channel->buffer.empty()) return nilValue();

  Value value = channel->buffer.front();
  channel->buffer.pop_front();
  wakeChannel(vm, channel);
  return value;
}

Value channelTryRecv(VM& vm, int, Value* args) {
  ObjChannel* channel = asChannel(args[0]);
  std::unique_lock<std::mutex> guard = lockTasks(vm);
  if (channel->buffer.empty()) return nilValue();
  Value value = channel->buffer.front();
  channel->buffer.pop_front();
  wakeChannel(vm, channel);
  return value;
}

Value channelClose(VM& vm, int, Value* args) {
  ObjChannel* channel = asChannel(args[0]);
  std::unique_lock<std::mutex> guard = lockTasks(vm);
  channel->closed = true;
  wakeChannel(vm, channel);
  return nilValue();
}

Value channelLen(VM& vm, int, Value* args) {
  std::unique_lock<std::mutex> guard = lockTasks(vm);
  return numberValue((double)asChannel(args[0])->buffer.size());
}

Value channelIsClosed(VM& vm, int, Value* args) {
  std::unique_lock<std::mutex> guard = lockTasks(vm);
  return boolValue(asChannel(args[0])->closed);
}

// Takes from whichever of several channels has something first. Returns
// a two element array: the channel it came from and the value. With a
// timeout given, returns nil when nothing arrived in time.
//
// This is what a task that serves more than one source needs, and it is
// the reason recv() alone is not enough.
Value nativeSelect(VM& vm, int argCount, Value* args) {
  if (argCount < 1 || !isArray(args[0])) {
    return vm.failAs("type",
                     "select() expects an array of channels, and an optional "
                     "timeout in seconds.");
  }
  ObjArray* channels = asArray(args[0]);
  for (Value entry : channels->items) {
    if (!isChannel(entry)) {
      return vm.failAs("type", "select() expects channels, got %s.",
                       valueTypeName(entry));
    }
  }
  if (channels->items.empty()) {
    return vm.failAs("task", "select() needs at least one channel.");
  }

  bool timed = argCount > 1 && isNumber(args[1]);
  double remaining = timed ? asNumber(args[1]) : 0;

  for (;;) {
    {
      std::unique_lock<std::mutex> guard = lockTasks(vm);
      // A fixed order would starve the later channels whenever the
      // first one is always ready, so the scan starts where the last
      // one left off.
      static std::atomic<unsigned> rotation{0};
      size_t count = channels->items.size();
      size_t offset = rotation.fetch_add(1, std::memory_order_relaxed) % count;
      bool allClosed = true;
      for (size_t i = 0; i < count; i++) {
        ObjChannel* channel =
            asChannel(channels->items[(i + offset) % count]);
        if (!channel->closed) allClosed = false;
        if (channel->buffer.empty()) continue;
        Value value = channel->buffer.front();
        channel->buffer.pop_front();
        wakeChannel(vm, channel);

        guard.unlock();
        ObjArray* pair = vm.runtime().newArray();
        GCRoot pairRoot(vm.runtime(), (Obj*)pair);
        pair->items.push_back(channels->items[(i + offset) % count]);
        pair->items.push_back(value);
        return objValue((Obj*)pair);
      }
      if (allClosed) return nilValue();
    }

    // Nothing ready. Rather than parking on every channel at once,
    // which would need a waiter entry per channel and a way to take the
    // others back off, this waits a short time and looks again. A
    // select is a rare thing in a hot loop, and this keeps the channel
    // fast path free of it.
    if (timed) {
      if (remaining <= 0) return nilValue();
      double slice = remaining < 0.002 ? remaining : 0.002;
      vm.park();
      Scheduler::sleepFor(slice);
      vm.unpark();
      remaining -= slice;
    } else {
      vm.park();
      Scheduler::sleepFor(0.002);
      vm.unpark();
    }
  }
}

Value taskJoin(VM& vm, int, Value* args) {
  ObjTask* task = asTask(args[0]);
  std::unique_lock<std::mutex> guard = lockTasks(vm);

  // `joined` belongs to the same state machine as `done`: checking it
  // before taking the mutex races another task completing this join.
  if (task->joined) return task->result;

  while (!task->done) waitFor(vm, task->waiters, guard);
  // The task sets `done` as the last thing it does, so by the time this
  // runs the join itself cannot block for long. It still parks: a thread
  // on its way out is not one the collector should be kept waiting by.
  vm.park();
  vm.runtime().reapTask(task);
  vm.unpark();
  task->joined = true;
  vm.runtime().retireTask(task);

  if (task->failed) {
    // The error the task raised, raised again here, so that a catch
    // clause on this side selects on the same kind and reads the same
    // payload as one inside the task would have.
    if (isError(task->error)) return vm.reraise(task->error);
    return vm.failAs("task", "Task failed: %s",
                     valueToString(task->error).c_str());
  }
  return task->result;
}

Value taskIsDone(VM& vm, int, Value* args) {
  std::unique_lock<std::mutex> guard = lockTasks(vm);
  return boolValue(asTask(args[0])->done);
}

// What the scheduler is doing, for a program that wants to see it and
// for the tests that check the scheduler is being used at all.
Value nativeSchedInfo(VM& vm, int, Value*) {
  Scheduler::Stats stats = Scheduler::instance().stats();
  ObjMap* map = vm.runtime().newMap();
  GCRoot mapRoot(vm.runtime(), (Obj*)map);
  struct Entry {
    const char* name;
    double value;
  };
  const Entry entries[] = {
      {"workers", (double)stats.workers},   {"alive", (double)stats.alive},
      {"runnable", (double)stats.runnable}, {"spawned", (double)stats.spawned},
      {"finished", (double)stats.finished}, {"switches", (double)stats.switches},
      {"steals", (double)stats.steals},     {"parks", (double)stats.parks},
      {"waiting_io", (double)stats.waitingOnIO},
      {"timers", (double)stats.timers},
  };
  for (const Entry& entry : entries) {
    ObjString* key = vm.runtime().internString(entry.name);
    GCRoot keyRoot(vm.runtime(), (Obj*)key);
    map->entries.set(objValue((Obj*)key), numberValue(entry.value));
  }
  return objValue((Obj*)map);
}

}  // namespace

void installConcurrency(Runtime& runtime) {
  defineGlobalFn(runtime, "chan", nativeChan, -1);
  defineGlobalFn(runtime, "sleep", nativeSleep, 1);
  defineGlobalFn(runtime, "yield", nativeYield, 0);
  defineGlobalFn(runtime, "select", nativeSelect, -1);
  defineGlobalFn(runtime, "sched_info", nativeSchedInfo, 0);

  defineMethodFn(runtime, ObjType::Channel, "send", channelSend, 2);
  defineMethodFn(runtime, ObjType::Channel, "try_send", channelTrySend, 2);
  defineMethodFn(runtime, ObjType::Channel, "recv", channelRecv, 1);
  defineMethodFn(runtime, ObjType::Channel, "try_recv", channelTryRecv, 1);
  defineMethodFn(runtime, ObjType::Channel, "close", channelClose, 1);
  defineMethodFn(runtime, ObjType::Channel, "len", channelLen, 1);
  defineMethodFn(runtime, ObjType::Channel, "is_closed", channelIsClosed, 1);

  defineMethodFn(runtime, ObjType::Task, "join", taskJoin, 1);
  defineMethodFn(runtime, ObjType::Task, "is_done", taskIsDone, 1);
}

}  // namespace red

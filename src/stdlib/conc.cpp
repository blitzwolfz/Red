// Tasks and channels.
//
// A task is an operating system thread with its own VM and its own value
// stack, and tasks run at the same time as one another. Channels are the
// supported way to move values between them.
//
// One mutex guards every channel and every task's finished flag. It is
// held only for the moment an operation takes, never while Red code
// runs, so it is not what the old runtime lock was. Waiting on it parks
// the task, which lets the collector treat the waiting thread's stack as
// still. docs/design.md covers the rest.
#include <chrono>
#include <mutex>
#include <thread>

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

// Waits on the shared condition variable with this task parked, so a
// collection can run while it waits. Nothing here touches the heap: the
// values a channel holds are already in its buffer, where the collector
// finds them.
void waitParked(VM& vm, std::unique_lock<std::mutex>& guard) {
  vm.park();
  vm.runtime().cond.wait(guard);
  vm.unpark();
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
    return vm.failAs("type", "sleep() expects a number, got %s.", valueTypeName(args[0]));
  }
  double seconds = asNumber(args[0]);
  if (seconds <= 0) return nilValue();

  // Parked for the wait, so the collector never has to wait on a task
  // that is doing nothing.
  vm.park();
  std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
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
    vm.runtime().cond.notify_all();
    channel->waiters++;
    while (!channel->buffer.empty() && !channel->closed) {
      waitParked(vm, guard);
    }
    channel->waiters--;
    return nilValue();
  }

  while (channel->buffer.size() >= channel->capacity && !channel->closed) {
    channel->waiters++;
    waitParked(vm, guard);
    channel->waiters--;
  }
  if (channel->closed) return vm.failAs("task", "send() on a closed channel.");
  channel->buffer.push_back(value);
  vm.runtime().cond.notify_all();
  return nilValue();
}

Value channelRecv(VM& vm, int, Value* args) {
  ObjChannel* channel = asChannel(args[0]);
  std::unique_lock<std::mutex> guard = lockTasks(vm);

  while (channel->buffer.empty() && !channel->closed) {
    channel->waiters++;
    waitParked(vm, guard);
    channel->waiters--;
  }
  // A closed and drained channel yields nil forever, which is what lets a
  // worker loop end with `while ((job = ch.recv()) != nil)`.
  if (channel->buffer.empty()) return nilValue();

  Value value = channel->buffer.front();
  channel->buffer.pop_front();
  vm.runtime().cond.notify_all();
  return value;
}

Value channelTryRecv(VM& vm, int, Value* args) {
  ObjChannel* channel = asChannel(args[0]);
  std::unique_lock<std::mutex> guard = lockTasks(vm);
  if (channel->buffer.empty()) return nilValue();
  Value value = channel->buffer.front();
  channel->buffer.pop_front();
  vm.runtime().cond.notify_all();
  return value;
}

Value channelClose(VM& vm, int, Value* args) {
  ObjChannel* channel = asChannel(args[0]);
  std::unique_lock<std::mutex> guard = lockTasks(vm);
  channel->closed = true;
  vm.runtime().cond.notify_all();
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

Value taskJoin(VM& vm, int, Value* args) {
  ObjTask* task = asTask(args[0]);
  std::unique_lock<std::mutex> guard = lockTasks(vm);

  // `joined` belongs to the same state machine as `done`: checking it
  // before taking the mutex races another task completing this join.
  if (task->joined) return task->result;

  while (!task->done) waitParked(vm, guard);
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

}  // namespace

void installConcurrency(Runtime& runtime) {
  defineGlobalFn(runtime, "chan", nativeChan, -1);
  defineGlobalFn(runtime, "sleep", nativeSleep, 1);

  defineMethodFn(runtime, ObjType::Channel, "send", channelSend, 2);
  defineMethodFn(runtime, ObjType::Channel, "recv", channelRecv, 1);
  defineMethodFn(runtime, ObjType::Channel, "try_recv", channelTryRecv, 1);
  defineMethodFn(runtime, ObjType::Channel, "close", channelClose, 1);
  defineMethodFn(runtime, ObjType::Channel, "len", channelLen, 1);
  defineMethodFn(runtime, ObjType::Channel, "is_closed", channelIsClosed, 1);

  defineMethodFn(runtime, ObjType::Task, "join", taskJoin, 1);
  defineMethodFn(runtime, ObjType::Task, "is_done", taskIsDone, 1);
}

}  // namespace red

// Tasks and channels.
//
// A task is an operating system thread with its own VM and its own value
// stack. Channels are the only supported way to move values between
// tasks. Both are guarded by the single runtime lock, and a task that
// parks on a channel releases that lock so the rest of the program, and
// the collector, keep running. docs/design.md explains the tradeoff.
#include <chrono>
#include <thread>

#include "../vm.h"
#include "builtins.h"

namespace red {

namespace {

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

  // Holding the runtime lock while sleeping would stop every other task,
  // so it goes back before the wait and is retaken afterwards.
  vm.releaseLock();
  std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
  vm.acquireLock();
  return nilValue();
}

Value channelSend(VM& vm, int, Value* args) {
  ObjChannel* channel = asChannel(args[0]);
  Value value = args[1];

  if (channel->closed) return vm.failAs("task", "send() on a closed channel.");

  if (channel->capacity == 0) {
    // Unbuffered: hand the value over, then wait until a receiver has
    // taken it before returning.
    channel->buffer.push_back(value);
    vm.runtime().cond.notify_all();
    channel->waiters++;
    while (!channel->buffer.empty() && !channel->closed) {
      vm.runtime().cond.wait(vm.lock());
    }
    channel->waiters--;
    return nilValue();
  }

  while (channel->buffer.size() >= channel->capacity && !channel->closed) {
    channel->waiters++;
    vm.runtime().cond.wait(vm.lock());
    channel->waiters--;
  }
  if (channel->closed) return vm.failAs("task", "send() on a closed channel.");
  channel->buffer.push_back(value);
  vm.runtime().cond.notify_all();
  return nilValue();
}

Value channelRecv(VM& vm, int, Value* args) {
  ObjChannel* channel = asChannel(args[0]);

  while (channel->buffer.empty() && !channel->closed) {
    channel->waiters++;
    vm.runtime().cond.wait(vm.lock());
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
  if (channel->buffer.empty()) return nilValue();
  Value value = channel->buffer.front();
  channel->buffer.pop_front();
  vm.runtime().cond.notify_all();
  return value;
}

Value channelClose(VM& vm, int, Value* args) {
  ObjChannel* channel = asChannel(args[0]);
  channel->closed = true;
  vm.runtime().cond.notify_all();
  return nilValue();
}

Value channelLen(VM&, int, Value* args) {
  return numberValue((double)asChannel(args[0])->buffer.size());
}

Value channelIsClosed(VM&, int, Value* args) {
  return boolValue(asChannel(args[0])->closed);
}

Value taskJoin(VM& vm, int, Value* args) {
  ObjTask* task = asTask(args[0]);
  if (task->joined) return task->result;

  while (!task->done) {
    vm.runtime().cond.wait(vm.lock());
  }
  // The task thread sets `done` and then releases the runtime lock on its
  // way out, so by the time this runs the join cannot block for long.
  if (task->thread != nullptr && task->thread->joinable()) {
    task->thread->join();
  }
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

Value taskIsDone(VM&, int, Value* args) {
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

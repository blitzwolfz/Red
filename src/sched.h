// The green-thread scheduler.
//
// A task is a fiber: a stack of its own and a VM of its own, scheduled
// onto a small fixed set of operating system threads rather than getting
// one each. Spawning is then a page of memory and a queue push instead of
// a clone(2), and a program can have a hundred thousand tasks running.
//
// The shape is the one Go uses. N workers, each with a run queue it owns
// and steals from its neighbours when it runs dry; a poller thread that
// owns kqueue or epoll and wakes fibers when their sockets are ready; a
// timer heap for sleeps and deadlines. A fiber that blocks -- on a
// channel, on a socket, on another task -- switches back to its worker,
// which picks up the next runnable one. The operating system thread never
// waits.
//
// The collector sees a fiber exactly as it used to see a task's thread:
// one Thread entry, with its own allocation list and its own VM to scan.
// A fiber that is not running is parked, which is the same state the old
// runtime used for a thread blocked in a system call, so the rest of the
// collector needed no changes. docs/concurrency.md is the long version.
#pragma once

#include <cstddef>
#include <functional>
#include <mutex>
#include <string>

namespace red {

class Runtime;
class VM;
struct Thread;
struct ObjTask;

struct Fiber;

// How big a fiber's machine stack is, and how big its value stack is.
// Both are mapped rather than allocated, so a fiber that uses a page of
// each costs a page of each.
struct SchedulerOptions {
  int workers = 0;              // 0 means one per core, capped
  size_t fiberStackBytes = 0;   // 0 means the default
};

class Scheduler {
 public:
  static Scheduler& instance();

  // Runs `body` as the program's first fiber, on this thread and on the
  // worker threads it starts. Returns once that fiber and every fiber
  // still running when it finished have finished too, which is what
  // `joinAllTasks` used to wait for.
  //
  // Everything the program does happens inside this call, so an ordinary
  // `chan.recv()` in main code parks a fiber rather than an OS thread.
  void runMain(Runtime& runtime, const SchedulerOptions& options,
               const std::function<void()>& body);

  // The fiber running on the calling thread, or nullptr when this thread
  // is not running one. Everything that can block asks for this first
  // and falls back to blocking the thread when there is no fiber: the
  // REPL, the test runner and the debugger run VMs outside the
  // scheduler.
  static Fiber* current();
  // Is a scheduler running at all?
  static bool active();

  // Starts a task on a new fiber. The task's callee and arguments are
  // already on it.
  bool spawn(Runtime& runtime, ObjTask* task);

  // ---- what a running fiber can ask for ---------------------------
  //
  // Waiting is two calls, because it spans letting a lock go.
  //
  // prepareToPark() is called while the caller still holds the lock that
  // protects whatever it is about to wait on, after it has put itself
  // somewhere a waker will find it. park() is called once that lock is
  // released, and takes the fiber off its worker.
  //
  // A wakeup that arrives in between is not lost: it finds the note
  // prepareToPark() left and leaves the queueing to the switch that is
  // still finishing. The fiber runs again when somebody calls ready().
  static void prepareToPark();
  static void park();
  // Makes a parked fiber runnable. Safe from any thread, and from the
  // poller. The caller holds whatever lock the fiber announced itself
  // under.
  static void ready(Fiber* fiber);
  // Gives up the rest of this fiber's turn without blocking.
  static void yieldNow();

  // Sleeps this fiber. Falls back to sleeping the thread outside the
  // scheduler.
  static void sleepFor(double seconds);

  // Waits until `fd` can be read or written, or until `timeout` seconds
  // pass. A timeout of zero or less waits forever. Returns true when the
  // descriptor is ready, false on a timeout or an error.
  //
  // Outside the scheduler this is a poll(2) on the calling thread, so a
  // native can call it either way and get the same answer.
  static bool waitReady(int fd, bool forWrite, double timeout);

  // Wakes everything waiting on a descriptor, reporting it as not ready.
  // Called just before the descriptor is closed: closing one takes it
  // out of kqueue and epoll silently, so a task parked on it would
  // otherwise wait for a readiness that can never be reported. An accept
  // loop ended by closing its listening socket is the usual case.
  static void wakeOnClose(int fd);

  // ---- for something that really does block the thread ------------
  //
  // Reading a file, running a subprocess and calling into a C extension
  // all block the operating system thread no matter what the scheduler
  // wants. Wrapping one of those in a BlockingRegion tells the scheduler
  // that this worker is about to be unavailable, so it can start a
  // replacement rather than leaving runnable fibers waiting on a thread
  // that is stuck in read(2).
  class BlockingRegion {
   public:
    BlockingRegion();
    ~BlockingRegion();
    BlockingRegion(const BlockingRegion&) = delete;
    BlockingRegion& operator=(const BlockingRegion&) = delete;

   private:
    bool entered_ = false;
  };

  // ---- numbers, for `sched_info()` --------------------------------
  struct Stats {
    long spawned = 0;
    long finished = 0;
    long switches = 0;
    long steals = 0;
    long parks = 0;
    int workers = 0;
    int alive = 0;
    int runnable = 0;
    int waitingOnIO = 0;
    int timers = 0;
  };
  Stats stats() const;

 private:
  Scheduler() = default;
};

// The default machine stack size for a fiber, and the environment
// variable that changes it.
size_t defaultFiberStackBytes();

}  // namespace red

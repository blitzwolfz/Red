#include "sched.h"

#include <fcntl.h>
#include <poll.h>
#include <sys/mman.h>
#include <unistd.h>

#if defined(__linux__)
#include <sys/epoll.h>
#else
#include <sys/event.h>
#include <sys/time.h>
#endif

#include <cerrno>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <pthread.h>

#include <thread>
#include <vector>

#include "common.h"
#include "fiber_sanitizers.h"
#include "object.h"
#include "runtime.h"
#include "sched_tls.h"
#include "vm.h"

// Defined in fiber_asm.S. The only machine-dependent thing here.
extern "C" void red_fiber_swap(void** saveStackPointer, void* newStackPointer);

namespace red {

namespace {

double monotonicSeconds() {
  using Clock = std::chrono::steady_clock;
  return std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
}

long environmentNumber(const char* name, long fallback) {
  const char* text = std::getenv(name);
  if (text == nullptr || text[0] == '\0') return fallback;
  char* end = nullptr;
  long value = std::strtol(text, &end, 10);
  if (end == text || value <= 0) return fallback;
  return value;
}

enum class FiberState : uint8_t { Ready, Running, Parked, Done };

// Where a fiber is in the act of going to sleep.
//
// Parking cannot be one step. A fiber decides to wait while holding the
// lock that protects whatever it is waiting for, releases that lock, and
// only then switches off its stack. A wakeup can arrive in between, and
// queueing a fiber whose stack pointer has not been saved yet would be
// fatal.
//
// So the wakeup and the switch agree through this, with one atomic
// exchange each. Whichever of them finds the other already there is the
// one that puts the fiber on a run queue; the other does nothing. It is
// the same handshake Go uses between gopark and goready.
enum class ParkState : uint8_t {
  Running,
  // Has decided to park and has let its lock go, but is still on its
  // own stack. A wakeup here only leaves a note.
  Parking,
  // Off its stack and safe to queue.
  Parked,
  // A wakeup has arrived. Whoever sets this without finding Parked
  // leaves the queueing to the switch that is still in progress.
  Ready,
};

// What a fiber is asking its worker to do, set just before it switches
// back and read by the worker immediately after.
enum class Transfer : uint8_t { None, Yield, Park, Exit };

struct Worker;

}  // namespace

// Declared in the header only as an opaque type: nothing outside this
// file looks inside a fiber.
struct Fiber {
  // Where red_fiber_swap() left this fiber's stack pointer. Touched only
  // by whichever thread is switching to or from it, and that thread is
  // the only one that may be, because a fiber is on exactly one queue or
  // one worker at a time.
  void* stackPointer = nullptr;
  char* stackBase = nullptr;
  size_t stackBytes = 0;

  Runtime* runtime = nullptr;
  Thread* gcThread = nullptr;  // this fiber's entry in the collector's list
  ObjTask* task = nullptr;     // null for the program's first fiber
  Worker* worker = nullptr;

  std::atomic<FiberState> state{FiberState::Ready};
  std::atomic<ParkState> parkState{ParkState::Running};
  SanitizerContext sanitizer;
  std::function<void()> body;
  long id = 0;
  // False until the first switch into it, which is the one that has to
  // set up the collector's view of this fiber from the outside.
  bool started = false;
};

namespace {

// ---------------------------------------------------------------------
// Stacks
//
// Mapped rather than allocated, with an unreadable page at the low end.
// Mapping means a fiber that uses one page costs one page however large
// its stack is allowed to become, which is what makes a hundred thousand
// of them affordable. The guard page means a fiber that runs off the end
// dies there, instead of quietly writing over whatever was below it.

size_t pageSize() {
  static size_t size = (size_t)::sysconf(_SC_PAGESIZE);
  return size;
}

size_t roundToPage(size_t bytes) {
  size_t page = pageSize();
  return ((bytes + page - 1) / page) * page;
}

char* mapStack(size_t usable, size_t* total) {
  size_t whole = roundToPage(usable) + pageSize();
  void* memory = ::mmap(nullptr, whole, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANON, -1, 0);
  if (memory == MAP_FAILED) return nullptr;
  ::mprotect(memory, pageSize(), PROT_NONE);
  *total = whole;
  return (char*)memory;
}

void fiberEntry();

// Builds a stack that looks exactly like one red_fiber_swap() has just
// saved from, with the return address pointing at the trampoline.
// Switching to it therefore "returns" into a fiber that has never run.
void* prepareStack(char* base, size_t total) {
  uintptr_t top = (uintptr_t)(base + total);
  top &= ~(uintptr_t)15;
  // Slack below the very top, so an unwinder walking off the end of the
  // fiber has somewhere to stop.
  top -= 64;

#if defined(__x86_64__)
  // Six saved registers, the return address at +48, padding above it.
  // After the pops and the `ret`, rsp sits 8 past a 16-byte boundary,
  // which is where a System V function expects it on entry.
  uintptr_t sp = top - 64;
  std::memset((void*)sp, 0, 64);
  *(uintptr_t*)(sp + 48) = (uintptr_t)&fiberEntry;
  return (void*)sp;
#elif defined(__aarch64__) || defined(__arm64__)
  // The 160-byte frame red_fiber_swap() writes, with the trampoline in
  // the link register's slot.
  uintptr_t sp = top - 160;
  std::memset((void*)sp, 0, 160);
  *(uintptr_t*)(sp + 8) = (uintptr_t)&fiberEntry;
  return (void*)sp;
#else
#error "No fiber stack layout for this architecture."
#endif
}

// ---------------------------------------------------------------------
// Workers

struct Worker {
  int index = 0;
  // Where this worker's own stack pointer lives while it is inside a
  // fiber. Coming back out means switching to this.
  void* stackPointer = nullptr;
  Fiber* running = nullptr;
  Transfer transfer = Transfer::None;
  SanitizerContext sanitizer;

  // What this worker will run next. It pushes and pops at the back;
  // everybody else steals from the front, so the two ends rarely meet.
  std::mutex lock;
  std::deque<Fiber*> queue;

  // Every worker is joined at shutdown, including one started to cover
  // a blocking call. A spare is an ordinary worker that happened to be
  // started late; it stays and is reused rather than taking itself down
  // and being started again by the next blocking call.
  std::thread* osThread = nullptr;
};

// Read and written only through sched_tls.h, never as a plain
// thread_local here: a fiber may come back on a different thread than
// the one it left, and the accessors are what force the address to be
// worked out again on the thread that is running now.
Worker* currentWorker() { return (Worker*)schedulerCurrentWorker(); }
Fiber* currentFiber() { return (Fiber*)schedulerCurrentFiber(); }
void setCurrentWorker(Worker* worker) { schedulerSetCurrentWorker(worker); }
void setCurrentFiber(Fiber* fiber) { schedulerSetCurrentFiber(fiber); }

// ---------------------------------------------------------------------
// Waiting for a descriptor or a clock

struct Waiter {
  Fiber* fiber = nullptr;
  int fd = -1;
  bool forWrite = false;
  double deadline = 0;   // absolute, monotonic; 0 means no timeout
  bool ready = false;    // the descriptor became ready
  bool expired = false;  // the deadline passed first
  bool woken = false;    // already handed back to the scheduler
  Waiter* nextOnFd = nullptr;
};

void makeReady(Fiber* fiber);

// One thread owning kqueue or epoll, plus a heap of deadlines. It is the
// only thread that ever waits on the operating system, which is what
// lets every other thread stay busy.
class Poller {
 public:
  bool start();
  void stop();

  // Both are called with lock_ held by the fiber that is about to park.
  void add(Waiter* waiter);
  void remove(Waiter* waiter);

  std::mutex& lock() { return lock_; }
  int waiting() const { return waitingCount_.load(std::memory_order_relaxed); }
  int timers() const { return (int)timers_.size(); }

 private:
  void loop();
  void arm(int fd, bool forWrite);
  void disarm(int fd, bool forWrite);
  // Wakes every waiter on one side of a descriptor. lock_ is held.
  void fire(int fd, bool forWrite);
  void expire(double now);
  void nudge();
  void wake(Waiter* waiter);

  std::mutex lock_;
  std::map<int, Waiter*> readers_;
  std::map<int, Waiter*> writers_;
  std::multimap<double, Waiter*> timers_;
  std::atomic<int> waitingCount_{0};

  int queue_ = -1;
  int wakePipe_[2] = {-1, -1};
  std::thread* thread_ = nullptr;
  std::atomic<bool> stopping_{false};
  bool started_ = false;
};

// ---------------------------------------------------------------------
// The scheduler's own state
//
// One global run queue for fibers that were made runnable by a thread
// that is not a worker -- the poller, mostly -- and one queue per worker
// for everything else. A worker out of work looks at its own queue, then
// the global one, then steals half of somebody else's.

struct Impl {
  Runtime* runtime = nullptr;

  std::mutex lock;
  std::condition_variable wake;
  std::deque<Fiber*> global;
  std::vector<Worker*> workers;

  // Workers asleep on `wake`. Atomic because it is read without the
  // lock, as a hint about whether notifying is worth the lock at all.
  std::atomic<int> idle{0};
  int busy = 0;         // workers inside a fiber or looking for one
  long alive = 0;       // fibers that exist and have not finished
  bool stopping = false;
  bool running = false;

  size_t stackBytes = 0;
  long nextId = 1;

  Poller poller;

  // Counters for sched_info(). Plain longs under `lock` where they are
  // written under it, atomics where they are not.
  std::atomic<long> spawned{0};
  std::atomic<long> finished{0};
  std::atomic<long> switches{0};
  std::atomic<long> steals{0};
  std::atomic<long> parks{0};
};

Impl& impl() {
  static Impl state;
  return state;
}

// How many fibers are sitting in queues right now. Called with
// impl().lock held.
int runnableCountLocked() {
  Impl& state = impl();
  int total = (int)state.global.size();
  for (Worker* worker : state.workers) {
    std::lock_guard<std::mutex> guard(worker->lock);
    total += (int)worker->queue.size();
  }
  return total;
}

void pushGlobal(Fiber* fiber) {
  Impl& state = impl();
  std::unique_lock<std::mutex> guard(state.lock);
  state.global.push_back(fiber);
  if (state.idle.load(std::memory_order_relaxed) > 0) {
    state.wake.notify_one();
  }
}

// Puts a fiber on a run queue. Only called once it is known to be off
// its own stack.
void enqueue(Fiber* fiber) {
  fiber->state.store(FiberState::Ready, std::memory_order_release);
  // Onto the waker's own queue when the waker is a worker, because the
  // two fibers have almost certainly just passed a value between them
  // and are warm in the same cache. Onto the global queue otherwise.
  Worker* worker = currentWorker();
  if (worker != nullptr) {
    {
      std::lock_guard<std::mutex> guard(worker->lock);
      worker->queue.push_back(fiber);
    }
    Impl& state = impl();
    if (state.idle.load(std::memory_order_relaxed) > 0) {
      std::lock_guard<std::mutex> guard(state.lock);
      state.wake.notify_one();
    }
    return;
  }
  pushGlobal(fiber);
}

// Wakes a fiber that has decided to park, or has finished parking. The
// caller holds whatever lock the fiber announced itself under, so it
// cannot miss a decision that has already been made; the exchange below
// is what settles which side does the queueing when the fiber is still
// halfway off its stack.
void makeReady(Fiber* fiber) {
  if (fiber == nullptr) return;
  for (;;) {
    ParkState state = fiber->parkState.load(std::memory_order_acquire);
    if (state == ParkState::Ready || state == ParkState::Running) return;
    if (fiber->parkState.compare_exchange_weak(state, ParkState::Ready,
                                               std::memory_order_acq_rel)) {
      // Parked: it is off its stack and this side queues it. Parking: it
      // is still on its stack, and the worker finishing the switch will
      // find Ready instead of Parking and queue it there.
      if (state == ParkState::Parked) enqueue(fiber);
      return;
    }
  }
}

// ---------------------------------------------------------------------
// Poller

bool Poller::start() {
  if (started_) return true;
  if (::pipe(wakePipe_) != 0) return false;
  for (int i = 0; i < 2; i++) {
    int flags = ::fcntl(wakePipe_[i], F_GETFL, 0);
    ::fcntl(wakePipe_[i], F_SETFL, flags | O_NONBLOCK);
    ::fcntl(wakePipe_[i], F_SETFD, FD_CLOEXEC);
  }
#if defined(__linux__)
  queue_ = ::epoll_create1(EPOLL_CLOEXEC);
  if (queue_ >= 0) {
    struct epoll_event event;
    std::memset(&event, 0, sizeof(event));
    event.events = EPOLLIN;
    event.data.fd = wakePipe_[0];
    ::epoll_ctl(queue_, EPOLL_CTL_ADD, wakePipe_[0], &event);
  }
#else
  queue_ = ::kqueue();
  if (queue_ >= 0) {
    struct kevent change;
    EV_SET(&change, wakePipe_[0], EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0,
           nullptr);
    ::kevent(queue_, &change, 1, nullptr, 0, nullptr);
  }
#endif
  if (queue_ < 0) return false;
  started_ = true;
  thread_ = new std::thread([this] { loop(); });
  return true;
}

void Poller::stop() {
  if (!started_) return;
  stopping_.store(true, std::memory_order_release);
  nudge();
  thread_->join();
  delete thread_;
  thread_ = nullptr;
  ::close(queue_);
  ::close(wakePipe_[0]);
  ::close(wakePipe_[1]);
  queue_ = -1;
  started_ = false;
}

void Poller::nudge() {
  char byte = 1;
  ssize_t ignored = ::write(wakePipe_[1], &byte, 1);
  (void)ignored;
}

void Poller::arm(int fd, bool forWrite) {
#if defined(__linux__)
  // One registration per descriptor, with whichever directions are
  // wanted, because epoll refuses a second add for the same one.
  uint32_t events = 0;
  if (readers_.count(fd) != 0) events |= EPOLLIN;
  if (writers_.count(fd) != 0) events |= EPOLLOUT;
  struct epoll_event event;
  std::memset(&event, 0, sizeof(event));
  event.events = events | EPOLLRDHUP;
  event.data.fd = fd;
  bool both = readers_.count(fd) != 0 && writers_.count(fd) != 0;
  if (::epoll_ctl(queue_, both ? EPOLL_CTL_MOD : EPOLL_CTL_ADD, fd, &event) !=
          0 &&
      errno == EEXIST) {
    ::epoll_ctl(queue_, EPOLL_CTL_MOD, fd, &event);
  }
#else
  struct kevent change;
  EV_SET(&change, fd, forWrite ? EVFILT_WRITE : EVFILT_READ,
         EV_ADD | EV_ENABLE, 0, 0, nullptr);
  ::kevent(queue_, &change, 1, nullptr, 0, nullptr);
#endif
}

void Poller::disarm(int fd, bool forWrite) {
#if defined(__linux__)
  uint32_t events = 0;
  if (readers_.count(fd) != 0) events |= EPOLLIN;
  if (writers_.count(fd) != 0) events |= EPOLLOUT;
  if (events == 0) {
    ::epoll_ctl(queue_, EPOLL_CTL_DEL, fd, nullptr);
    return;
  }
  struct epoll_event event;
  std::memset(&event, 0, sizeof(event));
  event.events = events | EPOLLRDHUP;
  event.data.fd = fd;
  ::epoll_ctl(queue_, EPOLL_CTL_MOD, fd, &event);
#else
  struct kevent change;
  EV_SET(&change, fd, forWrite ? EVFILT_WRITE : EVFILT_READ, EV_DELETE, 0, 0,
         nullptr);
  ::kevent(queue_, &change, 1, nullptr, 0, nullptr);
#endif
}

void Poller::add(Waiter* waiter) {
  if (waiter->fd >= 0) {
    std::map<int, Waiter*>& side = waiter->forWrite ? writers_ : readers_;
    waiter->nextOnFd = side.count(waiter->fd) != 0 ? side[waiter->fd] : nullptr;
    side[waiter->fd] = waiter;
    arm(waiter->fd, waiter->forWrite);
  }
  if (waiter->deadline > 0) timers_.insert({waiter->deadline, waiter});
  waitingCount_.fetch_add(1, std::memory_order_relaxed);
  // The poller may be asleep on a deadline further out than this one.
  nudge();
}

void Poller::remove(Waiter* waiter) {
  if (waiter->fd >= 0) {
    std::map<int, Waiter*>& side = waiter->forWrite ? writers_ : readers_;
    auto found = side.find(waiter->fd);
    if (found != side.end()) {
      Waiter** link = &found->second;
      while (*link != nullptr && *link != waiter) link = &(*link)->nextOnFd;
      if (*link == waiter) *link = waiter->nextOnFd;
      if (found->second == nullptr) {
        side.erase(found);
        disarm(waiter->fd, waiter->forWrite);
      }
    }
  }
  if (waiter->deadline > 0) {
    auto range = timers_.equal_range(waiter->deadline);
    for (auto it = range.first; it != range.second; ++it) {
      if (it->second == waiter) {
        timers_.erase(it);
        break;
      }
    }
  }
  waiter->nextOnFd = nullptr;
  waitingCount_.fetch_sub(1, std::memory_order_relaxed);
}

void Poller::wake(Waiter* waiter) {
  if (waiter->woken) return;
  waiter->woken = true;
  makeReady(waiter->fiber);
}

void Poller::fire(int fd, bool forWrite) {
  std::map<int, Waiter*>& side = forWrite ? writers_ : readers_;
  auto found = side.find(fd);
  if (found == side.end()) return;
  for (Waiter* waiter = found->second; waiter != nullptr;
       waiter = waiter->nextOnFd) {
    waiter->ready = true;
    wake(waiter);
  }
}

void Poller::expire(double now) {
  while (!timers_.empty() && timers_.begin()->first <= now) {
    Waiter* waiter = timers_.begin()->second;
    timers_.erase(timers_.begin());
    // Taken off the heap but left registered: the waiter itself takes
    // itself off the rest of the lists when it wakes, under this lock,
    // so nothing here can free something a fiber is still holding.
    waiter->deadline = 0;
    waiter->expired = true;
    wake(waiter);
  }
}

void Poller::loop() {
  constexpr int kBatch = 64;
#if defined(__linux__)
  struct epoll_event events[kBatch];
#else
  struct kevent events[kBatch];
#endif

  while (!stopping_.load(std::memory_order_acquire)) {
    double timeout = -1;
    {
      std::lock_guard<std::mutex> guard(lock_);
      if (!timers_.empty()) {
        timeout = timers_.begin()->first - monotonicSeconds();
        if (timeout < 0) timeout = 0;
      }
    }

    int count;
#if defined(__linux__)
    int milliseconds = timeout < 0 ? -1 : (int)(timeout * 1000.0 + 0.5);
    count = ::epoll_wait(queue_, events, kBatch, milliseconds);
#else
    struct timespec deadline;
    struct timespec* deadlinePointer = nullptr;
    if (timeout >= 0) {
      deadline.tv_sec = (time_t)timeout;
      deadline.tv_nsec = (long)((timeout - (double)deadline.tv_sec) * 1e9);
      deadlinePointer = &deadline;
    }
    count = ::kevent(queue_, nullptr, 0, events, kBatch, deadlinePointer);
#endif

    std::lock_guard<std::mutex> guard(lock_);
    for (int i = 0; i < count; i++) {
#if defined(__linux__)
      int fd = events[i].data.fd;
      uint32_t what = events[i].events;
      if (fd == wakePipe_[0]) {
        char drain[64];
        while (::read(wakePipe_[0], drain, sizeof(drain)) > 0) {
        }
        continue;
      }
      // A hangup or an error wakes both sides: whichever call the fiber
      // makes next is the one that should report it.
      if ((what & (EPOLLIN | EPOLLHUP | EPOLLRDHUP | EPOLLERR)) != 0) {
        fire(fd, false);
      }
      if ((what & (EPOLLOUT | EPOLLHUP | EPOLLERR)) != 0) fire(fd, true);
#else
      int fd = (int)events[i].ident;
      if (fd == wakePipe_[0]) {
        char drain[64];
        while (::read(wakePipe_[0], drain, sizeof(drain)) > 0) {
        }
        continue;
      }
      bool forWrite = events[i].filter == EVFILT_WRITE;
      fire(fd, forWrite);
      if ((events[i].flags & EV_EOF) != 0) fire(fd, !forWrite);
#endif
    }
    expire(monotonicSeconds());
  }
}

// ---------------------------------------------------------------------
// Switching

// Leaves the fiber running on this thread and comes back out on the
// worker's own stack.
//
// Not instrumented, for the reason fiber_sanitizers.h gives: this
// function is entered on one fiber's shadow stack and left on another's,
// which is not something a sanitizer's per-fiber bookkeeping can
// survive. Everything after the swap runs later, when
// somebody switches back in.
__attribute__((no_sanitize("thread"))) void switchOut(Transfer transfer) {
  Fiber* fiber = currentFiber();
  Worker* worker = currentWorker();
  worker->transfer = transfer;

  // The collector must not find a thread whose stack is about to be put
  // down. A fiber that is not running is parked, which is exactly the
  // state a thread blocked in a system call used to be in, so the
  // collector needed no changes at all.
  Runtime* runtime = fiber->runtime;
  if (transfer != Transfer::Exit) {
    runtime->park();
    runtime->setCurrentThread(nullptr);
  }
  setCurrentFiber(nullptr);

  impl().switches.fetch_add(1, std::memory_order_relaxed);
  if (transfer == Transfer::Exit) {
    sanitizerLeavingForGood(&worker->sanitizer);
  } else {
    sanitizerLeaving(&fiber->sanitizer, &worker->sanitizer);
  }
  red_fiber_swap(&fiber->stackPointer, worker->stackPointer);
  sanitizerArrived(&fiber->sanitizer);

  // Back again, on some worker, possibly a different one.
  setCurrentFiber(fiber);
  fiber->worker = currentWorker();
  fiber->runtime->setCurrentThread(fiber->gcThread);
  fiber->runtime->unpark();
}

// Runs one fiber until it parks, yields or finishes. Uninstrumented for
// the same reason switchOut() is.
__attribute__((no_sanitize("thread"))) void enterFiber(Worker* worker, Fiber* fiber) {
  worker->running = fiber;
  worker->transfer = Transfer::None;
  fiber->worker = worker;
  fiber->state.store(FiberState::Running, std::memory_order_release);
  fiber->parkState.store(ParkState::Running, std::memory_order_release);

  setCurrentFiber(fiber);
  // A fiber that has run before restores the collector's view of itself
  // on the other side of its own swap, in switchOut(). One that has not
  // needs its entry bound here, because its trampoline starts running
  // before any of that.
  //
  // Bound, but left parked: the entry is registered parked, and the
  // first thing the fiber does is adopt it, which is what marks it
  // running under the lock the collector agrees with everyone about.
  // Unparking it from out here instead would leave a window in which
  // the entry claims to be running while its fiber has not started, and
  // a collection beginning in that window would wait for it forever.
  if (!fiber->started) {
    fiber->started = true;
    if (fiber->gcThread != nullptr) {
      fiber->runtime->setCurrentThread(fiber->gcThread);
    }
  }

  sanitizerLeaving(&worker->sanitizer, &fiber->sanitizer);
  red_fiber_swap(&worker->stackPointer, fiber->stackPointer);
  sanitizerArrived(&worker->sanitizer);

  // The fiber has switched back. Its stack is still there and still
  // safe: nothing else may touch it until this worker says so.
  setCurrentFiber(nullptr);
  worker->running = nullptr;

  switch (worker->transfer) {
    case Transfer::Park: {
      fiber->state.store(FiberState::Parked, std::memory_order_release);
      impl().parks.fetch_add(1, std::memory_order_relaxed);
      // The fiber is off its stack now, so it is safe to queue. Half of
      // the handshake with ready(): if a wakeup arrived while the switch
      // was happening, it left the queueing to this side.
      ParkState expected = ParkState::Parking;
      if (!fiber->parkState.compare_exchange_strong(
              expected, ParkState::Parked, std::memory_order_acq_rel)) {
        // A wakeup arrived mid-switch and left the queueing to us.
        enqueue(fiber);
      }
      break;
    }

    case Transfer::Yield:
      fiber->state.store(FiberState::Ready, std::memory_order_release);
      {
        std::lock_guard<std::mutex> guard(worker->lock);
        worker->queue.push_back(fiber);
      }
      break;

    case Transfer::Exit:
    default: {
      fiber->state.store(FiberState::Done, std::memory_order_release);
      Impl& state = impl();
      char* base = fiber->stackBase;
      size_t bytes = fiber->stackBytes;
      sanitizerDestroyFiber(&fiber->sanitizer);
      delete fiber;
      if (base != nullptr) ::munmap(base, bytes);
      state.finished.fetch_add(1, std::memory_order_relaxed);
      {
        std::lock_guard<std::mutex> guard(state.lock);
        state.alive--;
        // The last fiber leaving is what ends the program, so every
        // worker has to be told, not just one.
        if (state.alive == 0) state.wake.notify_all();
      }
      break;
    }
  }
}

// Where a fiber begins. Reached by `ret` off the stack prepareStack()
// built, so it has no caller and must never return.
void fiberEntry() {
  Fiber* fiber = currentWorker()->running;
  sanitizerArrived(&fiber->sanitizer);
  setCurrentFiber(fiber);
  fiber->body();
  // The body has taken its VM down and left the collector's list, so
  // there is no thread state left to park. Everything else -- the
  // stack this is standing on -- is the worker's to clean up.
  switchOut(Transfer::Exit);
  // Unreachable: nothing ever switches back to a finished fiber.
  std::abort();
}

// ---------------------------------------------------------------------
// Finding work

Fiber* takeFromWorker(Worker* worker) {
  std::lock_guard<std::mutex> guard(worker->lock);
  if (worker->queue.empty()) return nullptr;
  // The back is the most recently readied fiber, which is usually the
  // one this worker just handed a value to.
  Fiber* fiber = worker->queue.back();
  worker->queue.pop_back();
  return fiber;
}

Fiber* takeFromGlobal() {
  Impl& state = impl();
  std::lock_guard<std::mutex> guard(state.lock);
  if (state.global.empty()) return nullptr;
  Fiber* fiber = state.global.front();
  state.global.pop_front();
  return fiber;
}

// Takes work from another worker, from the end its owner is not using.
Fiber* steal(Worker* thief) {
  Impl& state = impl();
  std::vector<Worker*> others;
  {
    std::lock_guard<std::mutex> guard(state.lock);
    others = state.workers;
  }
  for (Worker* victim : others) {
    if (victim == thief) continue;
    Fiber* fiber = nullptr;
    // Half of what is there comes over with it: that amortises the lock
    // across several fibers rather than coming back for each one. It is
    // taken into a local first, because holding two workers' locks at
    // once is how two thieves robbing each other would deadlock.
    std::vector<Fiber*> taken;
    {
      std::lock_guard<std::mutex> guard(victim->lock);
      if (victim->queue.empty()) continue;
      fiber = victim->queue.front();
      victim->queue.pop_front();
      size_t half = victim->queue.size() / 2;
      for (size_t i = 0; i < half; i++) {
        taken.push_back(victim->queue.front());
        victim->queue.pop_front();
      }
    }
    if (!taken.empty()) {
      std::lock_guard<std::mutex> guard(thief->lock);
      for (Fiber* stolen : taken) thief->queue.push_back(stolen);
    }
    state.steals.fetch_add(1, std::memory_order_relaxed);
    return fiber;
  }
  return nullptr;
}

// Nothing is runnable, nothing is on its way to becoming runnable, and
// there are still fibers that have not finished. Every one of them is
// waiting for something that cannot now happen.
void reportDeadlock() {
  std::fflush(stdout);
  std::fprintf(stderr,
               "Deadlock: every task is waiting, and nothing can wake one.\n");
  std::fflush(stderr);
  std::_Exit(70);
}

// Where the calling operating system thread's own stack is, which the
// sanitizers need in order to know what they are switching back to.
void describeOwnStack(SanitizerContext* context) {
  pthread_t self = pthread_self();
#if defined(__APPLE__)
  char* top = (char*)pthread_get_stackaddr_np(self);
  size_t size = pthread_get_stacksize_np(self);
  sanitizerSetStack(context, top - size, size);
#else
  pthread_attr_t attributes;
  if (pthread_getattr_np(self, &attributes) == 0) {
    void* bottom = nullptr;
    size_t size = 0;
    pthread_attr_getstack(&attributes, &bottom, &size);
    pthread_attr_destroy(&attributes);
    sanitizerSetStack(context, bottom, size);
  }
#endif
  sanitizerAdoptCurrentFiber(context);
}

void workerLoop(Worker* worker) {
  Impl& state = impl();
  setCurrentWorker(worker);
  describeOwnStack(&worker->sanitizer);

  for (;;) {
    // Its own queue first, because a fiber there was almost certainly
    // just handed a value by the fiber this worker was running. Then
    // the global queue, which is where the poller leaves things. Then
    // somebody else's.
    Fiber* fiber = takeFromWorker(worker);
    if (fiber == nullptr) fiber = takeFromGlobal();
    if (fiber == nullptr) fiber = steal(worker);

    if (fiber != nullptr) {
      enterFiber(worker, fiber);
      continue;
    }

    std::unique_lock<std::mutex> guard(state.lock);
    if (state.stopping || state.alive == 0) break;
    state.busy--;

    // Nothing runnable, no other worker on its way to producing
    // something, nothing waiting on a descriptor or a clock, and fibers
    // that have not finished. Every one of them is waiting for
    // something that can no longer happen. Checked under the lock that
    // everything able to produce work is published under, so it cannot
    // see a handoff halfway through.
    if (state.busy == 0 && state.poller.waiting() == 0 && state.alive > 0 &&
        runnableCountLocked() == 0) {
      guard.unlock();
      reportDeadlock();
    }

    state.idle.fetch_add(1, std::memory_order_relaxed);
    // A bounded wait rather than a plain one: a wakeup that arrives in
    // the window between deciding to sleep and sleeping costs 50
    // milliseconds of latency rather than a hang.
    state.wake.wait_for(guard, std::chrono::milliseconds(50));
    state.idle.fetch_sub(1, std::memory_order_relaxed);
    state.busy++;
  }
  {
    std::lock_guard<std::mutex> guard(state.lock);
    state.busy--;
  }
  state.wake.notify_all();
  setCurrentWorker(nullptr);
}

Fiber* makeFiber(Runtime& runtime, ObjTask* task) {
  Impl& state = impl();
  size_t total = 0;
  char* base = mapStack(state.stackBytes, &total);
  if (base == nullptr) return nullptr;

  Fiber* fiber = new Fiber();
  fiber->stackBase = base;
  fiber->stackBytes = total;
  // The guard page is not part of what runs on it.
  sanitizerSetStack(&fiber->sanitizer, base + pageSize(), total - pageSize());
  sanitizerCreateFiber(&fiber->sanitizer);
  fiber->stackPointer = prepareStack(base, total);
  fiber->runtime = &runtime;
  fiber->task = task;
  {
    std::lock_guard<std::mutex> guard(state.lock);
    fiber->id = state.nextId++;
    state.alive++;
  }
  state.spawned.fetch_add(1, std::memory_order_relaxed);
  return fiber;
}

}  // namespace

// ---------------------------------------------------------------------
// The published interface

size_t defaultFiberStackBytes() {
  // Enough for the deepest ordinary native-through-Red-through-native
  // nesting, and small enough that a great many fibers fit. It is
  // mapped, not committed, so a fiber that never goes deep costs a page.
  return (size_t)environmentNumber("RED_FIBER_STACK", 256 * 1024);
}

Scheduler& Scheduler::instance() {
  static Scheduler scheduler;
  return scheduler;
}

Fiber* Scheduler::current() { return currentFiber(); }

bool Scheduler::active() { return impl().running; }

void Scheduler::prepareToPark() {
  Fiber* fiber = currentFiber();
  if (fiber == nullptr) return;
  // Announced while the caller still holds the lock that protects
  // whatever it is about to wait for, which is what makes a wakeup
  // arriving after the unlock impossible to miss.
  fiber->parkState.store(ParkState::Parking, std::memory_order_release);
}

void Scheduler::park() {
  Fiber* fiber = currentFiber();
  if (fiber == nullptr) return;
  switchOut(Transfer::Park);
}

void Scheduler::ready(Fiber* fiber) { makeReady(fiber); }

void Scheduler::yieldNow() {
  if (currentFiber() == nullptr) {
    std::this_thread::yield();
    return;
  }
  switchOut(Transfer::Yield);
}

void Scheduler::sleepFor(double seconds) {
  if (seconds <= 0) {
    yieldNow();
    return;
  }
  Fiber* fiber = currentFiber();
  if (fiber == nullptr) {
    std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
    return;
  }
  Poller& poller = impl().poller;
  Waiter waiter;
  waiter.fiber = fiber;
  waiter.deadline = monotonicSeconds() + seconds;

  {
    std::lock_guard<std::mutex> guard(poller.lock());
    prepareToPark();
    poller.add(&waiter);
  }
  park();
  {
    std::lock_guard<std::mutex> guard(poller.lock());
    poller.remove(&waiter);
  }
}

bool Scheduler::waitReady(int fd, bool forWrite, double timeout) {
  Fiber* fiber = currentFiber();
  if (fiber == nullptr) {
    // Outside the scheduler there is nothing to hand the thread to, so
    // it waits for the descriptor itself and the answer is the same.
    struct pollfd entry;
    entry.fd = fd;
    entry.events = (short)(forWrite ? POLLOUT : POLLIN);
    entry.revents = 0;
    int milliseconds = timeout > 0 ? (int)(timeout * 1000.0 + 0.5) : -1;
    return ::poll(&entry, 1, milliseconds) > 0;
  }

  Poller& poller = impl().poller;
  Waiter waiter;
  waiter.fiber = fiber;
  waiter.fd = fd;
  waiter.forWrite = forWrite;
  if (timeout > 0) waiter.deadline = monotonicSeconds() + timeout;

  {
    std::lock_guard<std::mutex> guard(poller.lock());
    prepareToPark();
    poller.add(&waiter);
  }
  park();
  bool ready;
  {
    std::lock_guard<std::mutex> guard(poller.lock());
    poller.remove(&waiter);
    ready = waiter.ready;
  }
  return ready;
}

bool Scheduler::spawn(Runtime& runtime, ObjTask* task) {
  Impl& state = impl();
  if (!state.running) return false;

  Fiber* fiber = makeFiber(runtime, task);
  if (fiber == nullptr) return false;

  // Made here, by the fiber doing the spawning, so that a collection
  // cannot begin in the gap before the new fiber has an entry of its
  // own. This is what the old runtime did with a thread, for the same
  // reason.
  fiber->gcThread = runtime.newThread();

  fiber->body = [fiber, &runtime] {
    ObjTask* task = fiber->task;
    {
      VM vm(runtime);
      vm.attach(fiber->gcThread);

      // Rebuild the call on this fiber's own value stack: callee first,
      // then its arguments, exactly as a compiled call site leaves them.
      vm.push(task->callee);
      for (Value argument : task->args) vm.push(argument);

      Value result = nilValue();
      InterpretResult status =
          vm.callAndRun(task->callee, (int)task->args.size(), &result);

      // `result` lives in a C++ local until the state below publishes
      // it, so it stays rooted while waiting for the mutex, which is a
      // place a collection can run. The root has to go out of scope
      // before the VM detaches: after that there is no thread entry left
      // to take it off.
      {
        GCRoot resultRoot(runtime, result);
        runtime.lockParked(runtime.lock);
        std::lock_guard<std::mutex> guard(runtime.lock, std::adopt_lock);
        task->result = result;
        task->failed = status != InterpretResult::Ok;
        // Kept whole rather than described, so join() can raise the same
        // error with the same kind and payload.
        if (task->failed) task->error = vm.lastError;
        task->done = true;
        // Whoever is waiting in join() is parked under this very lock,
        // which is what stops a wakeup being sent before there is
        // anything to wake.
        for (Fiber* waiter : task->waiters) makeReady(waiter);
        task->waiters.clear();
      }
      runtime.cond.notify_all();
      vm.detach();
    }
    fiber->gcThread = nullptr;
  };

  // Straight onto a queue: a fiber that has never run has nothing to
  // hand over to.
  enqueue(fiber);
  return true;
}

// Whether to use fibers at all.
//
// On by default. Off under ThreadSanitizer, because TSAN's fiber
// interface ties a fiber's state to the thread that created it, and a
// fiber here can be resumed by whichever worker steals it. What TSAN
// then reports is its own bookkeeping falling over rather than anything
// about this program. With fibers off, a task is an operating system
// thread again and the TSAN build checks exactly what it always did:
// real Red code running in parallel over one shared heap.
//
// RED_SCHEDULER=on turns it back on for deliberately hunting races in
// the scheduler itself, and RED_SCHEDULER=off turns it off anywhere.
// AddressSanitizer has no such limitation and exercises the fiber path
// in full.
bool fibersEnabled() {
  const char* setting = std::getenv("RED_SCHEDULER");
  if (setting != nullptr && setting[0] != '\0') {
    return !(std::strcmp(setting, "off") == 0 ||
             std::strcmp(setting, "0") == 0 ||
             std::strcmp(setting, "no") == 0);
  }
#if defined(RED_TSAN_FIBERS)
  return false;
#else
  return true;
#endif
}

void Scheduler::runMain(Runtime& runtime, const SchedulerOptions& options,
                        const std::function<void()>& body) {
  if (!fibersEnabled()) {
    // No scheduler: the program runs on this thread and a task is a
    // thread, which is what a task was before there was a scheduler.
    body();
    runtime.joinAllTasks();
    return;
  }
  Impl& state = impl();
  state.runtime = &runtime;
  state.stackBytes = options.fiberStackBytes > 0 ? options.fiberStackBytes
                                                 : defaultFiberStackBytes();

  int count = options.workers;
  if (count <= 0) count = (int)environmentNumber("RED_WORKERS", 0);
  if (count <= 0) {
    unsigned cores = std::thread::hardware_concurrency();
    count = cores == 0 ? 4 : (int)cores;
  }
  if (count < 1) count = 1;
  if (count > 256) count = 256;

  state.poller.start();

  // The program's own code is a fiber like any other. That is the whole
  // point: `chan.recv()` written in main code parks a fiber, exactly as
  // it does inside a task, rather than stopping an operating system
  // thread.
  Fiber* main = makeFiber(runtime, nullptr);
  if (main == nullptr) {
    // No stack to be had. Running the program on this thread is worse
    // than running it on a fiber, but much better than not running it.
    body();
    return;
  }
  main->gcThread = runtime.newThread();
  main->body = [main, &runtime, body] {
    // The entry was registered before this fiber started, the same way a
    // spawned task's is. adoptThread is what binds it to this thread and
    // says it is running; the VM the body makes attaches to it.
    runtime.adoptThread(main->gcThread, nullptr);
    body();
    runtime.clearThreadVM(main->gcThread, nullptr);
    Thread* entry = main->gcThread;
    main->gcThread = nullptr;
    runtime.detachThread(entry);
    delete entry;
  };

  // More than one thread is about to run Red code, which is what turns
  // on the guards around everything shared.
  if (count > 1) Runtime::becomeParallel();

  std::vector<Worker*> workers;
  for (int i = 0; i < count; i++) {
    Worker* worker = new Worker();
    worker->index = i;
    workers.push_back(worker);
  }
  {
    std::lock_guard<std::mutex> guard(state.lock);
    state.workers = workers;
    state.running = true;
    state.stopping = false;
    // Every worker counts as busy until it has looked for work and
    // found none. Starting them at zero would let the first one to look
    // decide the program had deadlocked before the others had woken up.
    state.busy = count;
  }

  // The calling thread's own entry in the collector's list belongs to
  // the thread, not to any fiber, and this thread is about to stop being
  // one that runs Red code. Park it, or the collector waits for it
  // forever.
  Thread* host = Runtime::currentThread();
  if (host != nullptr) {
    runtime.park();
    runtime.setCurrentThread(nullptr);
  }

  enqueue(main);

  // The calling thread becomes worker zero rather than sitting and
  // waiting, so a single-core machine runs the program on one thread and
  // starts nothing it does not need.
  for (int i = 1; i < count; i++) {
    workers[i]->osThread = new std::thread(workerLoop, workers[i]);
  }
  workerLoop(workers[0]);

  {
    std::lock_guard<std::mutex> guard(state.lock);
    state.stopping = true;
    // Spares started along the way are in here too, and are joined the
    // same as the rest.
    workers = state.workers;
  }
  state.wake.notify_all();
  for (Worker* worker : workers) {
    if (worker->osThread == nullptr) continue;
    worker->osThread->join();
    delete worker->osThread;
    worker->osThread = nullptr;
  }

  {
    std::lock_guard<std::mutex> guard(state.lock);
    state.running = false;
    state.workers.clear();
  }
  state.poller.stop();
  for (Worker* worker : workers) delete worker;

  if (host != nullptr) {
    runtime.setCurrentThread(host);
    runtime.unpark();
  }
}

Scheduler::Stats Scheduler::stats() const {
  Impl& state = impl();
  Stats out;
  out.spawned = state.spawned.load(std::memory_order_relaxed);
  out.finished = state.finished.load(std::memory_order_relaxed);
  out.switches = state.switches.load(std::memory_order_relaxed);
  out.steals = state.steals.load(std::memory_order_relaxed);
  out.parks = state.parks.load(std::memory_order_relaxed);
  {
    std::lock_guard<std::mutex> guard(state.lock);
    out.workers = (int)state.workers.size();
    out.alive = (int)state.alive;
    out.runnable = runnableCountLocked();
  }
  {
    std::lock_guard<std::mutex> guard(state.poller.lock());
    out.waitingOnIO = state.poller.waiting();
    out.timers = state.poller.timers();
  }
  return out;
}

// ---------------------------------------------------------------------
// Blocking regions

Scheduler::BlockingRegion::BlockingRegion() {
  Impl& state = impl();
  if (currentWorker() == nullptr || !state.running) return;
  entered_ = true;

  // This worker is about to be unavailable for as long as the call
  // takes. If no other worker is free and there is work waiting, start
  // one more, so that a task reading a file does not stop the tasks
  // serving sockets.
  //
  // The replacement is an ordinary worker and stays for the life of the
  // scheduler. That is what keeps a program doing file work in a loop
  // from starting a thread per call: after the first one, there is an
  // idle worker and the test below says no.
  std::unique_lock<std::mutex> guard(state.lock);
  state.busy--;
  bool needed = state.idle.load(std::memory_order_relaxed) == 0 &&
                state.busy <= 0 && state.alive > 1 &&
                state.workers.size() < 256 && !state.stopping;
  if (!needed) return;

  Worker* spare = new Worker();
  spare->index = (int)state.workers.size();
  state.workers.push_back(spare);
  state.busy++;
  Runtime::becomeParallel();
  // Started while the lock is still held, so that the worker is never
  // visible to shutdown without the thread that shutdown has to join.
  // The new worker takes this lock only once it runs out of work, so
  // there is nothing here for it to wait on.
  spare->osThread = new std::thread(workerLoop, spare);
}

Scheduler::BlockingRegion::~BlockingRegion() {
  if (!entered_) return;
  Impl& state = impl();
  {
    std::lock_guard<std::mutex> guard(state.lock);
    state.busy++;
  }
}

}  // namespace red

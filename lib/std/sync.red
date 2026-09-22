// Coordinating tasks.
//
//   import "std/sync" as sync;
//
// Channels are the first thing to reach for, and most programs need
// nothing else. These are for the cases a channel states awkwardly: a
// section only one task may be in, a count to wait for, a limit on how
// many things run at once.
//
// Every one of them is a channel underneath, so a task waiting on one is
// parked, not spinning.

// A lock. Only one task holds it at a time.
//
//   const lock = sync.Mutex();
//   lock.lock();
//   ...
//   lock.unlock();
//
// Or, so that an error inside cannot leave it held:
//
//   lock.with(fun () { ... });
class Mutex {
  init() {
    // One token. Holding the lock means holding the token.
    this.slot = chan(1);
    this.slot.send(true);
  }

  lock() {
    this.slot.recv();
    return this;
  }

  // Takes it if it is free. True when it did.
  try_lock() { return this.slot.try_recv() != nil; }

  unlock() {
    this.slot.try_send(true);
    return this;
  }

  // Runs `body` holding the lock, and lets go however it ends.
  with(body) {
    this.lock();
    try {
      return body();
    } finally {
      this.unlock();
    }
  }
}

// Lets at most `count` tasks through at once. A worker pool without the
// workers: the tasks are already there, this only limits how many of
// them are inside at a time.
class Semaphore {
  init(count) {
    this.slots = chan(count);
    for (let i in range(0, count)) { this.slots.send(true); }
  }

  acquire() {
    this.slots.recv();
    return this;
  }

  try_acquire() { return this.slots.try_recv() != nil; }

  release() {
    this.slots.try_send(true);
    return this;
  }

  with(body) {
    this.acquire();
    try {
      return body();
    } finally {
      this.release();
    }
  }
}

// Waits for a number of things to finish.
//
//   const group = sync.WaitGroup();
//   for (let job in jobs) {
//     group.add(1);
//     spawn fun () { try { handle(job); } finally { group.done(); } } ();
//   }
//   group.wait();
//
// Worth having over joining each task when the tasks are not all made in
// one place, or when there are too many handles to keep.
class WaitGroup {
  init() {
    this.guard = Mutex();
    this.count = 0;
    // Closed when the count reaches zero, which is what wakes every
    // waiter at once: a receive on a closed channel returns immediately,
    // however many tasks are doing it.
    this.gate = chan();
    this.closed = false;
  }

  // Say how many more there will be. Counting up from zero again starts
  // a new round with a gate of its own, so a group can be reused; but a
  // waiter that was already waiting is released by the round it was
  // waiting on, not by the new one. As in every library with this shape,
  // add what a round contains before waiting on it.
  add(n = 1) {
    this.guard.lock();
    if (this.closed) {
      this.gate = chan();
      this.closed = false;
    }
    this.count += n;
    this.guard.unlock();
    return this;
  }

  done() {
    this.guard.lock();
    this.count -= 1;
    const reached = this.count <= 0 and !this.closed;
    let gate = nil;
    if (reached) {
      this.closed = true;
      gate = this.gate;
    }
    this.guard.unlock();
    if (gate != nil) { gate.close(); }
    return this;
  }

  // Waits until the count reaches zero. Returns at once if it already
  // has, and may be called by any number of tasks.
  wait() {
    this.guard.lock();
    const already = this.closed or this.count <= 0;
    const gate = this.gate;
    this.guard.unlock();
    if (already) { return this; }
    gate.recv();
    return this;
  }

  pending() {
    this.guard.lock();
    const n = this.count;
    this.guard.unlock();
    return n;
  }
}

// A value one task sets and others wait for. A task handle without the
// task, for a result that arrives from somewhere else.
class Once {
  init() {
    this.gate = chan();
    this.value = nil;
    this.set = false;
    this.guard = Mutex();
  }

  // Sets the value and wakes everyone waiting. Later calls are ignored,
  // so a race to produce the value has one winner rather than an error.
  resolve(value) {
    this.guard.lock();
    const first = !this.set;
    if (first) {
      this.value = value;
      this.set = true;
    }
    this.guard.unlock();
    if (first) { this.gate.close(); }
    return this;
  }

  // Waits for it.
  get() {
    this.guard.lock();
    const ready = this.set;
    this.guard.unlock();
    if (!ready) { this.gate.recv(); }
    return this.value;
  }

  is_set() {
    this.guard.lock();
    const ready = this.set;
    this.guard.unlock();
    return ready;
  }
}

// A fixed set of tasks taking work off one channel. Use this when the
// work is unbounded and the tasks should not be: a spawn per item is
// usually better, and this is for when it is not.
class Pool {
  init(size, handler) {
    this.jobs = chan(size * 2);
    this.workers = [];
    this.group = WaitGroup();
    const jobs = this.jobs;
    const group = this.group;
    for (let i in range(0, size)) {
      this.workers.push(spawn fun () {
        for (;;) {
          const job = jobs.recv();
          if (job == nil) { break; }
          try {
            handler(job);
          } finally {
            group.done();
          }
        }
      } ());
    }
  }

  submit(job) {
    this.group.add(1);
    this.jobs.send(job);
    return this;
  }

  // Waits for everything submitted so far.
  drain() {
    this.group.wait();
    return this;
  }

  // No more work; waits for the workers to finish and stop.
  close() {
    this.jobs.close();
    for (let worker in this.workers) { worker.join(); }
    return this;
  }
}

// Runs `body` on its own task and gives up on it after `seconds`.
// Returns the result, or `fallback` if it took too long.
//
// The task is not stopped. Nothing in Red stops a running task from
// outside, so this is for waiting less, not for doing less: the
// abandoned task carries on, and the program still waits for it at the
// end. Give it something that finishes.
fun with_timeout(seconds, body, fallback = nil) {
  const done = chan(1);
  spawn fun () {
    try {
      done.send([true, body()]);
    } catch (e) {
      done.send([false, e]);
    }
  } ();
  const picked = select([done], seconds);
  if (picked == nil) { return fallback; }
  const [ok, value] = picked[1];
  if (!ok) { throw value; }
  return value;
}

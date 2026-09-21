# Concurrency

A task in Red is a green thread: its own stack and its own VM, scheduled
onto a small fixed set of operating system threads rather than getting
one each.

```red
const t = spawn work(input);
print(t.join());
```

Spawning one is a memory mapping and a queue push. A program can have a
hundred thousand tasks and still be doing useful work:

```
$ red bench/spawn.red
100000 tasks, spawned and joined: 0.61s
```

That is what makes a task the right unit for a connection, a request or
a job, rather than something to be rationed through a pool.

## The scheduler

```
                 +---------------------------------------+
 spawn ------->  |  worker 0   worker 1   ...   worker N  |
                 |   [queue]    [queue]         [queue]   |
                 +-------|----------|--------------|------+
                         |          |  steals      |
                         +----------+--------------+
                                    |
                      +-------------+-------------+
                      |   poller: kqueue/epoll,   |
                      |   timers, readiness       |
                      +---------------------------+
```

One worker per core by default. Each has a run queue it owns, pushes to
and pops from the back of; a worker that runs dry takes the front of
somebody else's, and half of what is behind it, so a burst of spawns on
one core spreads without anything having to plan it.

A task that waits — on a channel, on a socket, on a timer, on another
task — is taken off its worker, which picks up the next runnable task.
The operating system thread never waits. That is the whole idea: threads
are for using the cores, and tasks are for expressing what the program is
waiting on.

One thread owns `kqueue` (macOS, BSD) or `epoll` (Linux) and the timer
heap, and is the only thread in the program that ever asks the operating
system to wait. When a socket is ready or a deadline passes, it makes the
waiting task runnable and the next free worker picks it up.

### Settings

| Variable | Meaning |
|---|---|
| `RED_WORKERS` | How many worker threads. Defaults to one per core. |
| `RED_FIBER_STACK` | Bytes of machine stack per task. Defaults to 256 KB, and is mapped, so a task that uses a page costs a page. |
| `RED_SCHEDULER` | `off` runs every task on an operating system thread of its own, as Red did before the scheduler existed. |

`sched_info()` returns what the scheduler is doing right now: workers,
live tasks, runnable tasks, context switches, steals, parks, and how many
tasks are waiting on a descriptor or a clock.

## Channels

```red
const jobs = chan(16);       // buffered
const handoff = chan();      // unbuffered: a send waits for a receive
```

| Method | Result |
|---|---|
| `send(value)` | Waits until there is room. |
| `try_send(value)` | `true` when it went in, `false` when it would have waited. |
| `recv()` | Waits for a value. `nil` once the channel is closed and drained. |
| `try_recv()` | A value, or `nil` if none is waiting. |
| `close()` | No more sends. Receivers drain what is left, then get `nil` forever. |
| `len()` `is_closed()` | |

A closed, drained channel yielding `nil` forever is what lets a worker
loop end without a second signal:

```red
fun worker(jobs, out) {
  for (;;) {
    const job = jobs.recv();
    if (job == nil) { break; }
    out.send(handle(job));
  }
}
```

### Waiting on several at once

```red
const picked = select([requests, shutdown]);
if (picked == nil) { return; }            // everything closed
const [source, value] = picked;
```

`select(channels)` takes from whichever channel has something, and
returns the channel it came from alongside the value, so the caller can
tell which arrived. `select(channels, seconds)` gives up and returns
`nil` after a timeout. The scan starts at a different channel each time,
so a channel that is always ready cannot starve the others.

## Tasks

| Method | Result |
|---|---|
| `join()` | Waits, then gives the task's result. Raises what the task raised. |
| `is_done()` | Has it finished? |

An error inside a task is kept whole and raised again by `join()`, with
its kind and payload intact, so a `catch` on this side selects on the
same thing a `catch` inside the task would have:

```red
try {
  task.join();
} catch (e: "http") {
  print("request failed: ${e.message}");
}
```

A task that is never joined still runs. The program ends when the main
task and every task still running have finished.

## Other tools

| Function | Meaning |
|---|---|
| `sleep(seconds)` | Waits. A timer and a context switch, not a sleeping thread. |
| `yield()` | Gives up the rest of this task's turn without waiting for anything. |
| `sched_info()` | A map of what the scheduler is doing. |

## Deadlock

When every task in the program is waiting, no task is runnable, and
nothing is waiting on a descriptor or a clock, then nothing can ever wake
any of them. Red says so and stops, rather than hanging:

```
Deadlock: every task is waiting, and nothing can wake one.
```

The check is made under the same lock everything that could produce work
is published under, so it cannot fire on a wakeup that is halfway
delivered.

## I/O

Every socket is non-blocking underneath, and every socket call that would
have waited parks its task instead. A server holding ten thousand
connections open is ten thousand parked tasks, costing a stack each.

```red
const server = tcp_listen(8080);
for (;;) {
  const client = server.accept();
  spawn handle(client);
}
```

That loop is the whole design. `accept()` parks until a connection
arrives; `handle` runs on its own task and parks whenever it waits for
the client; and neither costs a thread.

Sockets take a deadline:

```red
client.set_timeout(30);      // seconds; 0 means as long as it takes
```

A call that runs out raises an error of kind `"timeout"`.

Reading a file, running a subprocess and calling into a C extension do
block the worker they are on, because the operating system gives no way
not to. Red notices and starts a replacement worker when nothing else is
free, so a task reading a file does not stop the tasks serving sockets.
The replacement stays and is reused, so a program doing file work in a
loop does not start a thread each time round.

## What the collector sees

The collector treats a task exactly as it used to treat a thread: one
entry in its list, with its own allocation list and its own VM stack to
scan. A task that is not running is *parked*, which is the same state a
thread blocked in a system call was in, so nothing in the collector
needed to change for tasks to stop being threads.

A task allocates onto a list of its own, so making an object needs no
agreement with anyone. Collections stop the world at safepoints, which
every task polls at each allocation, each backward jump and each call.

## Sharing

Tasks share one heap, so they can share values. A container touched by
more than one task is guarded, and that guard costs nothing until a
second thread exists — `docs/design.md` covers how.

Two tasks writing the same array will not corrupt it, but they will
interleave in whatever order they happen to run. Channels are how you
stop caring about that order.

## Running without the scheduler

The REPL, the debugger and the test runner each run a VM directly rather
than through the scheduler, so inside them a task is an operating system
thread. Everything here works the same way; there is simply no sharing of
threads. `RED_SCHEDULER=off` does the same for an ordinary program.

Under ThreadSanitizer the scheduler is off by default. TSAN ties a
fiber's state to the thread that made it, and a task here can be resumed
by whichever worker steals it; what TSAN reports in that case is its own
bookkeeping rather than anything about the program. AddressSanitizer has
no such limitation, and `./setup.sh --debug` exercises the fiber path in
full.

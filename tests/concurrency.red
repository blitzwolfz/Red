// Tasks and channels.

// A task is an operating system thread. join() waits and yields its
// result.
fun compute(n) { return n * n; }
const task = spawn compute(9);
print(task.join());                  // expect: 81

// Channels move values between tasks.
fun producer(out, count) {
  for (let i = 1; i <= count; i = i + 1) {
    out.send(i);
  }
  out.close();
}

const numbers = chan(4);
const producerTask = spawn producer(numbers, 5);

let total = 0;
for (;;) {
  const value = numbers.recv();
  if (value == nil) { break; }
  total = total + value;
}
producerTask.join();
print(total);                        // expect: 15

// Several workers sharing one queue. Results come back out of order, so
// the test checks the set rather than the sequence.
fun worker(jobs, results) {
  for (;;) {
    const job = jobs.recv();
    if (job == nil) { break; }
    results.send(job * 2);
  }
}

const jobs = chan(2);
const results = chan(32);
let workers = [];
for (let i = 0; i < 4; i = i + 1) {
  workers.push(spawn worker(jobs, results));
}
for (let j = 1; j <= 12; j = j + 1) { jobs.send(j); }
jobs.close();
for (let w = 0; w < workers.len(); w = w + 1) { workers[w].join(); }
results.close();

let doubled = [];
for (;;) {
  const value = results.recv();
  if (value == nil) { break; }
  doubled.push(value);
}
print(doubled.len());                // expect: 12
print(doubled.sort());               // expect: [2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24]

// An unbuffered channel makes the sender wait for a receiver.
const handoff = chan();
const sender = spawn fun (ch) { ch.send("passed"); }(handoff);
print(handoff.recv());               // expect: passed
sender.join();

// An error inside a task surfaces when the task is joined.
fun failing() { throw "task blew up"; }
const doomed = spawn failing();
try {
  doomed.join();
} catch (e) {
  print(e.message.contains("task blew up"));  // expect: true
}

// Tasks allocate on the shared heap, so the collector has to see their
// stacks as roots.
fun allocator(out, rounds) {
  for (let i = 0; i < rounds; i = i + 1) {
    let scratch = [];
    for (let j = 0; j < 20; j = j + 1) { scratch.push("t ${i} ${j}"); }
  }
  out.send("done");
}
const finished = chan(8);
let allocators = [];
for (let i = 0; i < 4; i = i + 1) {
  allocators.push(spawn allocator(finished, 25));
}
for (let i = 0; i < 4; i = i + 1) { allocators[i].join(); }
finished.close();
let count = 0;
for (;;) {
  if (finished.recv() == nil) { break; }
  count = count + 1;
}
print(count);                        // expect: 4

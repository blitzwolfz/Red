// Spawning and joining tasks. A task is a green thread, so this measures
// the scheduler rather than the operating system's thread creation.
const N = 100000;
const results = chan(N);

fun work(n, out) { out.send(n); }

let tasks = [];
for (let i = 0; i < N; i = i + 1) { tasks.push(spawn work(i, results)); }
for (let i = 0; i < N; i = i + 1) { tasks[i].join(); }

let total = 0;
for (let i = 0; i < N; i = i + 1) { total = total + results.recv(); }
print("${N} tasks, spawned and joined");

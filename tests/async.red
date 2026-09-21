// async and await.

// `await` waits for a task and gives its result.
fun double(n) { return n * 2; }
print(await spawn double(21)); // expect: 42

// `async f(x)` is another spelling of `spawn f(x)`. It reads better
// against `await`, and `spawn` reads better when nobody waits.
print(await async double(5)); // expect: 10

// `async { ... }` runs a block as a task, closing over what it sees.
const base = 100;
const offset = async { return base + 11; };
print(await offset); // expect: 111

// A task keeps running while the code that made it carries on.
const slow = async {
  sleep(0.01);
  return "late";
};
print("early");      // expect: early
print(await slow);   // expect: late

// `await` on an array waits for every task in it, and gives an array of
// results in the same order. The tasks are already running, so this
// waits for the slowest rather than for the sum.
let tasks = [];
for (let i = 1; i <= 5; i = i + 1) { tasks.push(async double(i)); }
print(await tasks); // expect: [2, 4, 6, 8, 10]

// An array with no tasks in it is already a result.
print(await [1, 2, 3]); // expect: [1, 2, 3]

// And so is anything that is not a task, which is what lets `await` be
// written in front of a call without knowing whether it spawns.
print(await 7);      // expect: 7
print(await "text"); // expect: text
print(await nil);    // expect: nil

// An error raised inside a task surfaces where it is awaited, with its
// kind and payload intact.
try {
  await async { throw error("broke", 42, "custom"); };
} catch (e: "custom") {
  print(e.message);  // expect: broke
  print(e.payload);  // expect: 42
}

// Awaiting the same task twice gives the same result rather than
// waiting again.
const once = async double(6);
print(await once); // expect: 12
print(await once); // expect: 12

// `await` binds like the other prefix operators, so this waits for the
// task and then adds.
print(await async double(4) + 1); // expect: 9

// Tasks waiting on each other through a channel, with await for the
// results.
const relay = chan();
const sender = async { relay.send("through"); };
const receiver = async { return relay.recv(); };
await sender;
print(await receiver); // expect: through

// yield() gives up a turn without waiting for anything.
let order = [];
const first = async {
  order.push("a");
  yield();
  order.push("c");
};
await first;
order.push("d");
print(order.len() >= 3); // expect: true

// Allocation heavy work, to give the collector something to do.
//
// Run with --gc-stress to collect before every allocation. Nothing here
// checks timing, only that the results survive collection.

fun buildList(n) {
  let out = [];
  for (let i = 0; i < n; i = i + 1) {
    out.push("item ${i}");
  }
  return out;
}

// Most of these lists become garbage immediately.
for (let round = 0; round < 20; round = round + 1) {
  buildList(50);
}

const kept = buildList(100);
print(kept.len());                   // expect: 100
print(kept[0], kept[99]);            // expect: item 0 item 99

// Closures keep their captured variables alive across collections.
fun makeCounters(n) {
  let counters = [];
  for (let i = 0; i < n; i = i + 1) {
    let count = i;
    counters.push(fun () { count = count + 1; return count; });
  }
  return counters;
}
const counters = makeCounters(10);
buildList(200);
collect();
print(counters[5]());                // expect: 6
print(counters[9]());                // expect: 10

// Instances, their fields and their classes all survive.
class Node {
  init(value, next) {
    this.value = value;
    this.next = next;
  }
}
let head = nil;
for (let i = 0; i < 100; i = i + 1) {
  head = Node(i, head);
}
buildList(200);
collect();

let length = 0;
let walk = head;
while (walk != nil) {
  length = length + 1;
  walk = walk.next;
}
print(length);                       // expect: 100
print(head.value);                   // expect: 99

// Map contents are reachable through the map.
const registry = {};
for (let i = 0; i < 50; i = i + 1) {
  registry["key ${i}"] = [i, "value ${i}"];
}
collect();
print(registry.len());               // expect: 50
print(registry["key 25"][1]);        // expect: value 25

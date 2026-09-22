// The modules under lib/std.

import "std/strings" as strings;
import "std/path" as path;
import "std/url" as url;
import "std/time" as time;
import "std/fs" as fs;
import "std/sync" as sync;

// ---- strings --------------------------------------------------------

print(strings.cut("key=value=more", "="));       // expect: ["key", "value=more"]
print(strings.cut_last("a/b/c", "/"));           // expect: ["a/b", "c"]
print(strings.cut("no separator", "="));         // expect: ["no separator", ""]
print(strings.split_n("a:b:c:d", ":", 2));       // expect: ["a", "b:c:d"]
print(strings.words("  one  two three "));       // expect: ["one", "two", "three"]
print(strings.lines("a\nb\n"));                  // expect: ["a", "b"]
print(strings.lines("a\r\nb"));                  // expect: ["a", "b"]
print(strings.count("banana", "an"));            // expect: 2
print(strings.replace_first("aaa", "a", "b"));   // expect: baa
print(strings.without_prefix("prefix-rest", "prefix-")); // expect: rest
print(strings.without_suffix("name.red", ".red"));       // expect: name
print(strings.trim_chars("xxhixx", "x"));        // expect: hi
print(strings.center("ab", 6, "-"));             // expect: --ab--
print(strings.title("hello wide world"));        // expect: Hello Wide World
print(strings.wrap("aa bb cc dd", 5));           // expect: ["aa bb", "cc dd"]
print(strings.ellipsis("abcdefgh", 5));          // expect: ab...
print(strings.human_bytes(1536));                // expect: 1.5 kB
print(strings.human_bytes(500));                 // expect: 500 B
print(["f10", "f2", "f1"].sort(strings.natural_less)); // expect: ["f1", "f2", "f10"]
print(strings.is_digit("7"), strings.is_alpha("z"), strings.is_hex("F"));
// expect: true true true

// ---- path -----------------------------------------------------------

print(path.join("a", "b", "c.red"));   // expect: a/b/c.red
print(path.join("a", "/absolute"));    // expect: /absolute
print(path.dir("/x/y/z.red"));         // expect: /x/y
print(path.base("/x/y/z.red"));        // expect: z.red
print(path.ext("/x/y/z.red"));         // expect: .red
print(path.ext(".gitignore") == "");   // expect: true
print(path.stem("/x/y/z.red"));        // expect: z
print(path.with_ext("a/b.red", ".redc")); // expect: a/b.redc
print(path.clean("a/./b/../c//d"));    // expect: a/c/d
print(path.clean("/a/../.."));         // expect: /
print(path.relative("a/b", "a/c/d"));  // expect: ../c/d
print(path.contains("/x", "/x/y"));    // expect: true
print(path.contains("/x", "/xy"));     // expect: false
print(path.is_absolute("/a"), path.is_absolute("a")); // expect: true false

// ---- url ------------------------------------------------------------

const parsed = url.parse("http://bob@example.com:8080/a%20b?x=1&y=two+words#end");
print(parsed["scheme"], parsed["host"], parsed["port"]); // expect: http example.com 8080
print(parsed["user"]);              // expect: bob
print(parsed["path"]);              // expect: /a%20b
print(parsed["query"]["y"]);        // expect: two words
print(parsed["fragment"]);          // expect: end
print(url.format(parsed));
// expect: http://bob@example.com:8080/a%20b?x=1&y=two+words#end
print(url.encode("a b/c"));         // expect: a%20b%2Fc
print(url.decode("a%20b%2Fc"));     // expect: a b/c
print(url.decode("a+b"));           // expect: a b
print(url.build_query({"b": 2, "a": [1, 3]})); // expect: a=1&a=3&b=2
print(url.parse("https://example.com")["port"]); // expect: 443
print(url.parse("/only/a/path")["host"] == "");  // expect: true

// ---- time -----------------------------------------------------------

print(time.duration(0.25));    // expect: 250ms
print(time.duration(12.5));    // expect: 12.5s
print(time.duration(95));      // expect: 1m 35s
print(time.duration(3700));    // expect: 1h 1m
print(time.duration(90000));   // expect: 1d 1h
print(time.iso(0));            // expect: 1970-01-01T00:00:00Z
print(time.http_date(0));      // expect: Thu, 01 Jan 1970 00:00:00 GMT

const watch = time.Stopwatch();
sleep(0.02);
print(watch.stop() >= 0.02);   // expect: true

const deadline = time.Deadline(0.02);
print(deadline.passed());      // expect: false
deadline.wait();
print(deadline.passed());      // expect: true

// ---- fs -------------------------------------------------------------

const scratch = fs.temp_dir("redtest");
fs.ensure_dir(scratch + "/one/two");
fs.write_lines(scratch + "/one/a.txt", ["first", "second"]);
fs.write_atomic(scratch + "/one/two/b.red", "body");
print(fs.read_lines(scratch + "/one/a.txt"));   // expect: ["first", "second"]
print(fs.read(scratch + "/one/two/b.red"));     // expect: body
print(fs.walk(scratch).len());                  // expect: 4
print(fs.with_ext(scratch, [".red"]).len());    // expect: 1
print(fs.glob(scratch, "*.txt").len());         // expect: 1
print(fs.matches("main.red", "*.red"));         // expect: true
print(fs.matches("a1.c", "a[0-9].?"));          // expect: true
print(fs.matches("ab.c", "a[!0-9].c"));         // expect: true
print(fs.tree_size(scratch) > 0);               // expect: true

try {
  fs.read(scratch + "/missing");
} catch (e: "io") {
  print("io error");                            // expect: io error
}

fs.remove_tree(scratch);
print(exists(scratch));                         // expect: false

// ---- sync -----------------------------------------------------------

const lock = sync.Mutex();
let counter = 0;
const group = sync.WaitGroup();
group.add(40);
for (let i in range(0, 40)) {
  spawn fun () {
    try {
      lock.with(fun () { counter += 1; });
    } finally { group.done(); }
  } ();
}
group.wait();
print(counter);   // expect: 40

// A semaphore never lets more than its count through at once.
const limit = sync.Semaphore(3);
let inside = 0;
let peak = 0;
const second = sync.WaitGroup();
second.add(12);
for (let i in range(0, 12)) {
  spawn fun () {
    try {
      limit.with(fun () {
        lock.with(fun () {
          inside += 1;
          if (inside > peak) { peak = inside; }
        });
        sleep(0.005);
        lock.with(fun () { inside -= 1; });
      });
    } finally { second.done(); }
  } ();
}
second.wait();
print(peak <= 3);   // expect: true

const once = sync.Once();
spawn fun () { sleep(0.01); once.resolve("settled"); } ();
print(once.get());          // expect: settled
print(once.is_set());       // expect: true

let handled = [];
const pool = sync.Pool(4, fun (job) { lock.with(fun () { handled.push(job); }); });
for (let i in range(0, 20)) { pool.submit(i); }
pool.drain();
pool.close();
print(handled.len());       // expect: 20

// The abandoned task is not stopped -- nothing in Red stops a running
// task from outside -- and the program waits for it at the end, so this
// sleeps for a short time rather than a long one.
print(sync.with_timeout(0.02, fun () { sleep(0.2); return "slow"; }, "gave up"));
// expect: gave up
print(sync.with_timeout(2, fun () { return "quick"; }));
// expect: quick

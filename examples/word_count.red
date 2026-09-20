// Parallel word count, using file input and output, tasks and channels.
//
//   red examples/word_count.red [file]
//
// With no argument the example writes its own sample text first, so it
// runs anywhere. The work is split into equal slices of lines. Each task
// counts its slice into a private map, then sends that map back. The main
// task merges the partial results.
//
// Each task builds its own map on purpose. Sharing one map between tasks
// would need a lock around every update, and the merge at the end is
// cheaper than that.

const SAMPLE = "./word_count_sample.txt";
const WORKERS = 4;

fun writeSample(path) {
  const words = ["red", "green", "blue", "red", "small", "fast", "red",
                 "green", "clear", "simple"];
  let lines = [];
  for (let i = 0; i < 2000; i = i + 1) {
    let line = [];
    for (let j = 0; j < 8; j = j + 1) {
      line.push(words[(i * 8 + j) % words.len()]);
    }
    lines.push(line.join(" "));
  }
  write_file(path, lines.join("\n"));
}

// Counts the words in lines[start:end] and sends the partial map back.
fun countSlice(lines, start, end, out) {
  const counts = {};
  for (let i = start; i < end; i = i + 1) {
    for (let raw in lines[i].split(" ")) {
      const word = raw.trim().lower();
      if (word == "") { continue; }
      counts[word] = counts.get(word, 0) + 1;
    }
  }
  out.send(counts);
}

fun merge(into, partial) {
  for (let [word, count] in partial.entries()) {
    into[word] = into.get(word, 0) + count;
  }
  return into;
}

// ---------------------------------------------------------------------

let path = SAMPLE;
let madeSample = false;
if (args().len() > 0) {
  path = args()[0];
} else {
  writeSample(SAMPLE);
  madeSample = true;
}

const text = read_file(path);
if (text == nil) {
  print("cannot read ${path}");
  exit(1);
}

const lines = text.split("\n");
print("counting ${lines.len()} lines with ${WORKERS} tasks");

const results = chan(WORKERS);
const perWorker = ceil(lines.len() / WORKERS);

const started = clock();
let tasks = [];
for (let w = 0; w < WORKERS; w = w + 1) {
  const start = w * perWorker;
  let end = start + perWorker;
  if (end > lines.len()) { end = lines.len(); }
  if (start >= end) { continue; }
  tasks.push(spawn countSlice(lines, start, end, results));
}
for (let task in tasks) { task.join(); }
results.close();

let totals = {};
for (;;) {
  const partial = results.recv();
  if (partial == nil) { break; }
  totals = merge(totals, partial);
}
const elapsed = clock() - started;

// Report the most common words, highest count first.
const words = totals.keys();
words.sort(fun (a, b) { return totals[a] > totals[b]; });
print("distinct words: ${words.len()}");
for (let i = 0; i < min(5, words.len()); i = i + 1) {
  print("  ${words[i]}: ${totals[words[i]]}");
}

// Check the parallel result against a plain serial count.
const check = chan(1);
countSlice(lines, 0, lines.len(), check);
const serial = check.recv();
let matches = true;
for (let [word, count] in serial.entries()) {
  if (totals.get(word, 0) != count) { matches = false; }
}
print("matches a serial count: ${matches}");

if (madeSample) {
  remove_file(SAMPLE);
}

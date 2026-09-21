// Arrays and their methods.

const empty = [];
print(empty, empty.len()); // expect: [] 0

const nums = [1, 2, 3];
print(nums);             // expect: [1, 2, 3]
print(nums.len());       // expect: 3
print(nums[0], nums[2]); // expect: 1 3
print(nums[-1]);         // expect: 3

nums[1] = 20;
print(nums); // expect: [1, 20, 3]

nums.push(4);
nums.push(5, 6);
print(nums);           // expect: [1, 20, 3, 4, 5, 6]
print(nums.pop());     // expect: 6
print(nums.remove(1)); // expect: 20
print(nums);           // expect: [1, 3, 4, 5]

nums.insert(0, 0);
print(nums);              // expect: [0, 1, 3, 4, 5]
print(nums.slice(1, 3));  // expect: [1, 3]
print(nums.slice(-2));    // expect: [4, 5]
print(nums.contains(3));  // expect: true
print(nums.index_of(4));  // expect: 3
print(nums.index_of(99)); // expect: -1
print(nums.join(", "));   // expect: 0, 1, 3, 4, 5

// Mixed contents are allowed, and print with their own formatting.
print([1, "two", true, nil, [3]]); // expect: [1, "two", true, nil, [3]]

// Higher order methods run Red functions from inside the runtime.
const source = [1, 2, 3, 4, 5];
print(source.map(fun (n) { return n * n; }));           // expect: [1, 4, 9, 16, 25]
print(source.filter(fun (n) { return n % 2 == 0; }));   // expect: [2, 4]
print(source.reduce(fun (a, b) { return a + b; }));     // expect: 15
print(source.reduce(fun (a, b) { return a + b; }, 10)); // expect: 25

const words = ["pear", "apple", "fig"];
print(words.sort());        // expect: ["apple", "fig", "pear"]
print([3, 1, 2].sort());    // expect: [1, 2, 3]
print([1, 2, 3].reverse()); // expect: [3, 2, 1]

// sort with a comparator calls back into Red for each comparison.
print([3, 1, 2].sort(fun (a, b) { return a > b; })); // expect: [3, 2, 1]

const clearMe = [1, 2];
clearMe.clear();
print(clearMe); // expect: []

// Arrays compare by identity, not by contents.
print([1] == [1]); // expect: false
const same = [1];
print(same == same); // expect: true

// equals() compares contents, where == compares identity.
print([1, [2, 3]].equals([1, [2, 3]])); // expect: true
print([1, 2].equals([1, 2, 3]));        // expect: false
print([1, 2] == [1, 2]);                // expect: false

// any, all and find stop at the first answer rather than walking the
// whole array the way filter does.
const numbers = [1, 4, 9, 16];
print(numbers.any(fun (n) { return n > 10; }));  // expect: true
print(numbers.any(fun (n) { return n > 100; })); // expect: false
print(numbers.all(fun (n) { return n > 0; }));   // expect: true
print(numbers.all(fun (n) { return n > 1; }));   // expect: false
print([].any(fun (n) { return true; }));         // expect: false
print([].all(fun (n) { return false; }));        // expect: true

print(numbers.find(fun (n) { return n > 5; }));        // expect: 9
print(numbers.find(fun (n) { return n > 50; }));       // expect: nil
print(numbers.find_index(fun (n) { return n > 5; }));  // expect: 2
print(numbers.find_index(fun (n) { return n > 50; })); // expect: -1

// find_index is how a nil element is told apart from no match.
const holes = [nil, 3];
print(holes.find(fun (n) { return n == nil; }));       // expect: nil
print(holes.find_index(fun (n) { return n == nil; })); // expect: 0

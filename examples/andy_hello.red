// The smallest useful andy program: ask a question, print the answer.
//
//   red examples/andy_hello.red
//   ANDY_BACKEND=native red examples/andy_hello.red
//
// The interface is one expression with the shape of the thing it
// describes. Nothing here says which screen it will end up on.

import "andy" as andy;

const name = andy.Input("", "your name");
let answered = false;

fun done(button) {
  answered = true;
  andy.stop();
}

const form = andy.Column(
  andy.Label("What should I call you?"),
  name.on_submitted(done),
  andy.Row(andy.Spacer(), andy.Button("Done", done).as_primary())
).spaced(1).padded(andy.uniform(1));

andy.run(andy.Center(andy.Panel("Hello", form).sized(44, 9)),
  {"title": "Hello"});

if (answered and name.get_value() != "") {
  print("hello, ${name.get_value()}");
} else {
  print("never mind");
}

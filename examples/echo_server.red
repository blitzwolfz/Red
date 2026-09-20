// A concurrent TCP echo server, with a client that exercises it.
//
//   red examples/echo_server.red
//
// The server listens on a port the system picks, so the example never
// clashes with something already running. One task runs the accept loop.
// Each connection gets its own task, so a slow client cannot hold up the
// others.

const CLIENTS = 4;
const MESSAGES = 3;

// Handles one connection until the peer closes it.
fun handleConnection(client, id) {
  for (;;) {
    const request = client.read(1024);
    // read() answers nil when the other side has closed its end.
    if (request == nil) { break; }
    client.write("echo ${id}: ${request}");
  }
  client.close();
  return id;
}

// Accepts exactly `expected` connections, then stops.
fun acceptLoop(server, expected) {
  let handlers = [];
  for (let i = 0; i < expected; i = i + 1) {
    const client = server.accept();
    handlers.push(spawn handleConnection(client, i));
  }
  // Wait for every connection to finish before reporting.
  let handled = 0;
  for (let handler in handlers) {
    handler.join();
    handled += 1;
  }
  return handled;
}

// Connects, sends a few messages and checks each reply.
fun runClient(port, name, out) {
  const socket = tcp_connect("127.0.0.1", port);
  let replies = [];
  for (let i = 1; i <= MESSAGES; i = i + 1) {
    socket.write("${name} message ${i}");
    const reply = socket.read(1024);
    replies.push(reply);
  }
  socket.close();
  out.send("${name} got ${replies.len()} replies");
}

const server = tcp_listen(0);
const port = server.port();
print("listening on port ${port}");

const accepting = spawn acceptLoop(server, CLIENTS);

const replies = chan(CLIENTS);
let clients = [];
for (let i = 0; i < CLIENTS; i = i + 1) {
  clients.push(spawn runClient(port, "client-${i}", replies));
}
for (let client in clients) { client.join(); }
replies.close();

let lines = [];
for (;;) {
  const line = replies.recv();
  if (line == nil) { break; }
  lines.push(line);
}
lines.sort();
for (let line in lines) { print(line); }

print("connections handled: ${accepting.join()}");
server.close();

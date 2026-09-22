// Buffered reading over a socket.
//
//   import "std/bufio" as bufio;
//
//   const reader = bufio.Reader(client);
//   const line = reader.line();
//   const body = reader.exact(length);
//
// A socket hands back whatever has arrived, which is almost never the
// shape a protocol wants. Reading a line by asking for one byte at a
// time is a system call per byte; reading 4096 and then looking for the
// newline leaves the remainder somewhere. This keeps the remainder.
//
// std/http is written on top of it, and any other line-oriented protocol
// can be.

// How much to ask the socket for at a time.
const CHUNK = 8192;
// How long a single line may be before this gives up, so that a peer
// sending an endless line cannot make the reader hold the whole of it.
const MAX_LINE = 65536;

class Reader {
  init(source, limit = MAX_LINE) {
    this.source = source;
    this.buffer = "";
    this.at = 0;
    this.closed = false;
    this.limit = limit;
  }

  // Drops what has already been handed out, so the buffer does not grow
  // with everything ever read. Done on a threshold rather than every
  // time, because copying the tail is the cost this is avoiding.
  compact() {
    if (this.at > 0 and this.at * 2 >= this.buffer.len()) {
      this.buffer = this.buffer.sub(this.at);
      this.at = 0;
    }
  }

  // Pulls more in. False when the peer has closed its side.
  fill() {
    if (this.closed) { return false; }
    const piece = this.source.read(CHUNK);
    if (piece == nil) {
      this.closed = true;
      return false;
    }
    this.compact();
    this.buffer += piece;
    return true;
  }

  // How much is already here.
  buffered() { return this.buffer.len() - this.at; }

  // The next line, without its terminator. Handles both "\n" and
  // "\r\n". Returns nil at the end of the stream.
  //
  // A line longer than the limit raises an error of kind "bufio" rather
  // than growing without bound.
  line() {
    for (;;) {
      const found = this.buffer.sub(this.at).find("\n");
      if (found >= 0) {
        let end = this.at + found;
        let text = this.buffer.sub(this.at, end);
        this.at = end + 1;
        if (text.ends_with("\r")) { text = text.sub(0, text.len() - 1); }
        return text;
      }
      if (this.buffered() > this.limit) {
        throw error("line longer than ${this.limit} bytes", this.limit,
                    "bufio");
      }
      if (!this.fill()) {
        // Whatever is left with no newline after it is still a line, if
        // there is anything at all.
        if (this.buffered() == 0) { return nil; }
        const text = this.buffer.sub(this.at);
        this.at = this.buffer.len();
        return text;
      }
    }
  }

  // Exactly `count` bytes. Raises when the stream ends first, because a
  // caller that asked for a known length has been told a lie otherwise.
  exact(count) {
    if (count <= 0) { return ""; }
    while (this.buffered() < count) {
      if (!this.fill()) {
        throw error("stream ended after ${this.buffered()} of ${count} bytes",
                    this.buffered(), "bufio");
      }
    }
    const text = this.buffer.sub(this.at, this.at + count);
    this.at += count;
    this.compact();
    return text;
  }

  // Up to `count` bytes: whatever is here, or the next thing to arrive.
  // Returns nil at the end of the stream.
  some(count = CHUNK) {
    while (this.buffered() == 0) {
      if (!this.fill()) { return nil; }
    }
    let take = this.buffered();
    if (take > count) { take = count; }
    const text = this.buffer.sub(this.at, this.at + take);
    this.at += take;
    this.compact();
    return text;
  }

  // Everything until the stream ends.
  rest() {
    while (this.fill()) { }
    const text = this.buffer.sub(this.at);
    this.at = this.buffer.len();
    return text;
  }

  // Is there more? Pulls once to find out, so a caller can use it as a
  // loop condition.
  more() {
    if (this.buffered() > 0) { return true; }
    return this.fill();
  }
}

// Buffered writing.
//
// Collects small writes and sends them as one. A response built out of a
// status line, eight headers and a body is one write rather than ten,
// which for a request/response protocol is the difference between one
// packet and several.
class Writer {
  init(sink, limit = CHUNK) {
    this.sink = sink;
    this.pending = "";
    this.limit = limit;
  }

  write(...parts) {
    for (let part in parts) { this.pending += str(part); }
    if (this.pending.len() >= this.limit) { this.flush(); }
    return this;
  }

  line(text = "") {
    return this.write(text, "\r\n");
  }

  flush() {
    if (this.pending == "") { return this; }
    const text = this.pending;
    this.pending = "";
    this.sink.write(text);
    return this;
  }
}

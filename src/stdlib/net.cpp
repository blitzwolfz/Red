// TCP sockets.
//
// Every call that can block releases the runtime lock first. Nothing on
// the heap is touched while it is released.
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#include "../vm.h"
#include "builtins.h"

namespace red {

namespace {

Value nativeTcpListen(VM& vm, int argCount, Value* args) {
  if (!isNumber(args[0])) {
    return vm.fail("tcp_listen() expects a port number.");
  }
  int port = (int)asNumber(args[0]);
  int backlog = argCount > 1 && isNumber(args[1]) ? (int)asNumber(args[1]) : 16;

  int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return vm.fail("socket() failed: %s", std::strerror(errno));

  // Without this a restarted server cannot rebind while the old socket is
  // still in TIME_WAIT.
  int reuse = 1;
  ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

  struct sockaddr_in address;
  std::memset(&address, 0, sizeof(address));
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_ANY);
  address.sin_port = htons((uint16_t)port);

  if (::bind(fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
    int saved = errno;
    ::close(fd);
    return vm.fail("bind() on port %d failed: %s", port, std::strerror(saved));
  }
  if (::listen(fd, backlog) < 0) {
    int saved = errno;
    ::close(fd);
    return vm.fail("listen() failed: %s", std::strerror(saved));
  }
  return objValue((Obj*)vm.runtime().newSocket(fd, true));
}

Value nativeTcpConnect(VM& vm, int, Value* args) {
  if (!isString(args[0]) || !isNumber(args[1])) {
    return vm.fail("tcp_connect() expects a host string and a port number.");
  }
  std::string host(asString(args[0])->chars, asString(args[0])->length);
  std::string port = std::to_string((int)asNumber(args[1]));

  struct addrinfo hints;
  std::memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  struct addrinfo* results = nullptr;
  vm.releaseLock();
  int status = ::getaddrinfo(host.c_str(), port.c_str(), &hints, &results);
  int fd = -1;
  int savedErrno = 0;
  if (status == 0) {
    for (struct addrinfo* it = results; it != nullptr; it = it->ai_next) {
      fd = ::socket(it->ai_family, it->ai_socktype, it->ai_protocol);
      if (fd < 0) continue;
      if (::connect(fd, it->ai_addr, it->ai_addrlen) == 0) break;
      savedErrno = errno;
      ::close(fd);
      fd = -1;
    }
    ::freeaddrinfo(results);
  }
  vm.acquireLock();

  if (status != 0) {
    return vm.fail("tcp_connect() could not resolve '%s': %s", host.c_str(),
                   ::gai_strerror(status));
  }
  if (fd < 0) {
    return vm.fail("tcp_connect() to %s:%s failed: %s", host.c_str(),
                   port.c_str(), std::strerror(savedErrno));
  }
  return objValue((Obj*)vm.runtime().newSocket(fd, false));
}

bool requireOpenSocket(VM& vm, ObjSocket* socket, const char* who) {
  if (socket->closed || socket->fd < 0) {
    vm.fail("%s on a closed socket.", who);
    return false;
  }
  return true;
}

Value socketAccept(VM& vm, int, Value* args) {
  ObjSocket* server = asSocket(args[0]);
  if (!requireOpenSocket(vm, server, "accept()")) return nilValue();
  if (!server->listening) return vm.fail("accept() on a client socket.");

  int fd = server->fd;
  vm.releaseLock();
  int client = ::accept(fd, nullptr, nullptr);
  int savedErrno = errno;
  vm.acquireLock();

  if (client < 0) {
    return vm.fail("accept() failed: %s", std::strerror(savedErrno));
  }
  return objValue((Obj*)vm.runtime().newSocket(client, false));
}

Value socketRead(VM& vm, int argCount, Value* args) {
  ObjSocket* socket = asSocket(args[0]);
  if (!requireOpenSocket(vm, socket, "read()")) return nilValue();

  size_t limit = 4096;
  if (argCount > 1) {
    if (!isNumber(args[1]) || asNumber(args[1]) <= 0) {
      return vm.fail("read() expects a positive byte count.");
    }
    limit = (size_t)asNumber(args[1]);
  }

  std::string buffer;
  buffer.resize(limit);
  int fd = socket->fd;
  vm.releaseLock();
  ssize_t got = ::recv(fd, &buffer[0], limit, 0);
  int savedErrno = errno;
  vm.acquireLock();

  if (got < 0) return vm.fail("read() failed: %s", std::strerror(savedErrno));
  // Zero bytes means the peer closed its side, reported as nil so a read
  // loop can stop.
  if (got == 0) return nilValue();
  return objValue((Obj*)vm.runtime().copyString(buffer.data(), (size_t)got));
}

Value socketWrite(VM& vm, int argCount, Value* args) {
  ObjSocket* socket = asSocket(args[0]);
  if (!requireOpenSocket(vm, socket, "write()")) return nilValue();

  std::string text;
  for (int i = 1; i < argCount; i++) text += valueToString(args[i]);

  int fd = socket->fd;
  vm.releaseLock();
  size_t sent = 0;
  int savedErrno = 0;
  while (sent < text.size()) {
    ssize_t wrote = ::send(fd, text.data() + sent, text.size() - sent, 0);
    if (wrote <= 0) {
      savedErrno = errno;
      break;
    }
    sent += (size_t)wrote;
  }
  vm.acquireLock();

  if (sent < text.size()) {
    return vm.fail("write() failed after %zu bytes: %s", sent,
                   std::strerror(savedErrno));
  }
  return numberValue((double)sent);
}

Value socketClose(VM&, int, Value* args) {
  ObjSocket* socket = asSocket(args[0]);
  if (!socket->closed && socket->fd >= 0) {
    ::close(socket->fd);
    socket->closed = true;
    socket->fd = -1;
  }
  return nilValue();
}

Value socketFd(VM&, int, Value* args) {
  return numberValue((double)asSocket(args[0])->fd);
}

// The port this socket is actually bound to. Listening on port 0 asks the
// system to pick a free one, and this is how a program finds out which.
Value socketPort(VM& vm, int, Value* args) {
  ObjSocket* socket = asSocket(args[0]);
  if (!requireOpenSocket(vm, socket, "port()")) return nilValue();

  struct sockaddr_in address;
  socklen_t length = sizeof(address);
  if (::getsockname(socket->fd, (struct sockaddr*)&address, &length) < 0) {
    return vm.fail("port() failed: %s", std::strerror(errno));
  }
  return numberValue((double)ntohs(address.sin_port));
}

}  // namespace

void installNet(Runtime& runtime) {
  defineGlobalFn(runtime, "tcp_listen", nativeTcpListen, -1);
  defineGlobalFn(runtime, "tcp_connect", nativeTcpConnect, 2);

  defineMethodFn(runtime, ObjType::Socket, "accept", socketAccept, 1);
  defineMethodFn(runtime, ObjType::Socket, "read", socketRead, -1);
  defineMethodFn(runtime, ObjType::Socket, "write", socketWrite, -1);
  defineMethodFn(runtime, ObjType::Socket, "close", socketClose, 1);
  defineMethodFn(runtime, ObjType::Socket, "fd", socketFd, 1);
  defineMethodFn(runtime, ObjType::Socket, "port", socketPort, 1);
}

}  // namespace red

// SPDX-License-Identifier: GPL-3.0-or-later
// Standalone C++20 Unix socket protocol test; no real pairing or network.
#include "../sunshine/rkmoon_admin_io.hpp"
#include <sys/socket.h>
#include <unistd.h>
#include <cassert>
#include <chrono>
#include <string>
#include <thread>
#include <stop_token>
using namespace std::chrono_literals;
int main() {
  int fds[2];
  assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
  std::thread sender([&] {
    for (char c : std::string("LIST\n")) {
      assert(send(fds[1], &c, 1, 0) == 1);
      std::this_thread::sleep_for(2ms);
    }
  });
  std::string command;
  assert(rkmoon_sunshine::admin_io::receive_line(fds[0], command, {}));
  assert(command == "LIST\n");
  sender.join();
  std::thread receiver([&] {
    std::string result;
    char chunk[19];
    while (result.size() < 3400) {
      ssize_t n = recv(fds[1], chunk, sizeof(chunk), 0);
      assert(n > 0);
      result.append(chunk, size_t(n));
    }
    assert(result == std::string(3400, 'a'));
  });
  assert(rkmoon_sunshine::admin_io::send_reply(fds[0], std::string(3400,'a'), {}));
  receiver.join();
  close(fds[0]);close(fds[1]);
  assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
  std::stop_source source;
  auto before = std::chrono::steady_clock::now();
  std::thread cancel([&] { std::this_thread::sleep_for(50ms); source.request_stop(); });
  std::string empty;
  assert(!rkmoon_sunshine::admin_io::receive_line(fds[0], empty, source.get_token()));
  assert(std::chrono::steady_clock::now()-before < 1s);
  cancel.join();
  close(fds[0]);close(fds[1]);
}

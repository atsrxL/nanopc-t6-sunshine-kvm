// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <poll.h>
#include <sys/socket.h>
#include <algorithm>
#include <chrono>
#include <cerrno>
#include <cstdint>
#include <string>
#include <string_view>
#include <stop_token>

namespace rkmoon_sunshine::admin_io {
using clock = std::chrono::steady_clock;
// One absolute deadline survives interrupted/fragmented I/O.
inline bool ready(int fd, short event, clock::time_point deadline, std::stop_token stop) {
  while (!stop.stop_requested()) {
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - clock::now()).count();
    if (left <= 0) return false;
    pollfd pfd {fd, event, 0};
    const auto rc = poll(&pfd, 1, int(std::min<int64_t>(left, 200)));
    if (rc < 0 && errno == EINTR) continue;
    if (rc < 0 || (rc > 0 && !(pfd.revents & event))) return false;
    if (rc > 0) return true;
  }
  return false;
}
inline bool receive_line(int fd, std::string &request, std::stop_token stop) {
  const auto deadline = clock::now() + std::chrono::seconds(5);
  char chunk[128];
  while (request.size() < 512 && ready(fd, POLLIN, deadline, stop)) {
    ssize_t n = recv(fd, chunk, std::min(sizeof(chunk), 512 - request.size()), 0);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) return false;
    request.append(chunk, size_t(n));
    if (request.find('\n') != std::string::npos) return request.back() == '\n';
  }
  return false;
}
inline bool send_reply(int fd, std::string_view reply, std::stop_token stop) {
  const auto deadline = clock::now() + std::chrono::seconds(5);
  while (!reply.empty() && ready(fd, POLLOUT, deadline, stop)) {
    ssize_t n = send(fd, reply.data(), reply.size(), MSG_NOSIGNAL);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) return false;
    reply.remove_prefix(size_t(n));
  }
  return reply.empty();
}
} // namespace rkmoon_sunshine::admin_io

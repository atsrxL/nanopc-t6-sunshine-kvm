// SPDX-License-Identifier: GPL-3.0-or-later
// Private local operator socket. PIN is never passed on argv, logged, or exposed on TCP.
#include "src/nvhttp.h"
#include "src/logging.h"
#include "rkmoon_admin_io.hpp"
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <poll.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <thread>

namespace rkmoon_sunshine {

// Called synchronously from main: failure aborts the host before GameStream
// listeners start. Direct binary invocation must obey supervisor's 0700 state.
int open_admin_socket(std::string &socket_path) {
  const char *path = std::getenv("RKMOON_ADMIN_SOCKET");
  if (!path || path[0] != '/' || std::strlen(path) >= sizeof(sockaddr_un::sun_path)) return -1;
  socket_path = path;
  const auto parent = std::filesystem::path(socket_path).parent_path();
  struct stat dir {};
  if (lstat(parent.c_str(), &dir) != 0 || !S_ISDIR(dir.st_mode) ||
      dir.st_uid != geteuid() || (dir.st_mode & 0077) != 0) return -1;
  struct stat existing {};
  if (lstat(path, &existing) == 0 || errno != ENOENT) return -1;
  int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
  if (fd < 0) return -1;
  sockaddr_un addr {};
  addr.sun_family = AF_UNIX;
  std::strcpy(addr.sun_path, path);
  // bind permissions are constrained even if invoked without tools/run.py.
  auto previous = umask(0077);
  int rc = bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
  umask(previous);
  if (rc != 0) { close(fd); return -1; }
  if (chmod(path, 0600) != 0 || listen(fd, 4) != 0) {
    close(fd); unlink(path); return -1;
  }
  return fd;
}
void close_admin_socket(int fd, const std::string &path) {
  close(fd);
  unlink(path.c_str());
}

void admin_socket(std::stop_token stop, int fd) {
  while (!stop.stop_requested()) {
    pollfd pfd {fd, POLLIN, 0};
    int rc = poll(&pfd, 1, 200);
    if (rc < 0 && errno == EINTR) continue;
    if (rc <= 0 || !(pfd.revents & POLLIN)) continue;
    int client = accept4(fd, nullptr, nullptr, SOCK_CLOEXEC | SOCK_NONBLOCK);
    if (client < 0) continue;
    struct ucred cred {};
    socklen_t len = sizeof(cred);
    if (getsockopt(client, SOL_SOCKET, SO_PEERCRED, &cred, &len) != 0 || cred.uid != geteuid()) { close(client); continue; }
    std::string req, reply = "ERR\n";
    if (admin_io::receive_line(client, req, stop)) {
      if (req == "LIST\n") {
        reply.clear();
        for (const auto &session : nvhttp::get_pending_pairings()) reply += session.id + "\n";
        reply += ".\n";
      } else if (req.starts_with("PIN ")) {
        const auto separator = 4 + nvhttp::PAIRING_ID_SIZE;
        if (req.size() >= separator + 8 && req[separator] == ' ' && req[separator + 5] == ' ') {
          auto id = std::string_view(req).substr(4, nvhttp::PAIRING_ID_SIZE);
          auto pin = std::string_view(req).substr(separator + 1, 4);
          auto name = std::string_view(req).substr(separator + 6, req.size() - separator - 7);
          if (nvhttp::is_valid_pairing_id(id) && nvhttp::is_valid_pairing_pin(pin) && nvhttp::is_valid_pairing_name(name)) {
            reply = nvhttp::pin(id, std::string(pin), std::string(name), stop) ? "OK\n" : "DENIED\n";
          }
        }
      }
    }
    admin_io::send_reply(client, reply, stop);
    close(client);
  }
}
} // namespace rkmoon_sunshine

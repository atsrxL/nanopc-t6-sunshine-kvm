// SPDX-License-Identifier: GPL-3.0-or-later
// Private local operator socket. Never expose pairing PIN on argv, web, logs or network.
#include "src/nvhttp.h"
#include "src/logging.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <poll.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <string>
#include <string_view>
#include <thread>

namespace rkmoon_sunshine {
void admin_socket(std::stop_token stop) {
  const char *path = std::getenv("RKMOON_ADMIN_SOCKET");
  if (!path || path[0] != '/' || std::strlen(path) >= sizeof(sockaddr_un::sun_path)) {
    BOOST_LOG(error) << "RKMoon requires an absolute private admin socket path";
    return;
  }
  // Don't unlink an existing socket of unknown origin. The supervisor validates
  // stale sockets before launch. Filesystem permissions defend against other users.
  if (access(path, F_OK) == 0) {
    BOOST_LOG(error) << "RKMoon admin socket already exists; refusing to replace it";
    return;
  }
  int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
  if (fd < 0) return;
  sockaddr_un addr {};
  addr.sun_family = AF_UNIX;
  std::strcpy(addr.sun_path, path);
  if (bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) { close(fd); return; }
  chmod(path, 0600);
  if (listen(fd, 4) != 0) { unlink(path); close(fd); return; }
  while (!stop.stop_requested()) {
    pollfd pfd {fd, POLLIN, 0};
    if (poll(&pfd, 1, 200) <= 0) continue;
    int client = accept4(fd, nullptr, nullptr, SOCK_CLOEXEC);
    if (client < 0) continue;
    struct ucred cred {};
    socklen_t len = sizeof(cred);
    if (getsockopt(client, SOL_SOCKET, SO_PEERCRED, &cred, &len) != 0 || cred.uid != getuid()) { close(client); continue; }
    timeval timeout {5, 0};
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    char buf[512];
    auto bytes = recv(client, buf, sizeof(buf), 0);
    std::string reply = "ERR\n";
    if (bytes > 0 && bytes < (ssize_t)sizeof(buf)) {
      std::string_view req(buf, bytes);
      if (req == "LIST\n") {
        reply.clear();
        for (const auto &session : nvhttp::get_pending_pairings()) reply += session.id + "\n";
        reply += ".\n";
      } else if (req.starts_with("PIN ") && req.back() == '\n') {
        // Fixed-width id and PIN; name is only the local operator label.
        auto id = req.substr(4, nvhttp::PAIRING_ID_SIZE);
        auto separator = 4 + nvhttp::PAIRING_ID_SIZE;
        if (req.size() >= separator + 8 && req[separator] == ' ' && req[separator + 5] == ' ') {
          auto pin = req.substr(separator + 1, 4);
          auto name = req.substr(separator + 6, req.size() - separator - 7);
          if (nvhttp::is_valid_pairing_id(id) && nvhttp::is_valid_pairing_pin(pin) && nvhttp::is_valid_pairing_name(name)) {
            reply = nvhttp::pin(id, std::string(pin), std::string(name)) ? "OK\n" : "DENIED\n";
          }
        }
      }
    }
    send(client, reply.data(), reply.size(), MSG_NOSIGNAL);
    close(client);
  }
  close(fd);
  unlink(path);
}
} // namespace rkmoon_sunshine

// SPDX-License-Identifier: GPL-3.0-or-later
// Minimal dedicated host entrypoint: no web server, tray, UPnP, mDNS, or app file.
// The pinned Sunshine protocol/crypto/RTP/RTSP/input implementations are retained.
#include "src/config.h"
#include "src/globals.h"
#include "src/httpcommon.h"
#include "src/input.h"
#include "src/logging.h"
#include "src/nvhttp.h"
#include "src/platform/common.h"
#include "src/process.h"
#include "src/rtsp.h"
#include "src/video.h"
#include "src/utility.h"
#include <rs.h>
#include <sys/auxv.h>
#include <sys/stat.h>
#include <cstdlib>
#include <unistd.h>
#include <signal.h>
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <thread>

namespace rkmoon_sunshine {
int open_admin_socket(std::string &path);
void close_admin_socket(int fd, const std::string &path);
void admin_socket(std::stop_token stop, int fd);
}
namespace {
std::atomic_bool stopping {false};
extern "C" void shutdown_signal(int) { stopping.store(true); }
}

int main(int argc, char *argv[]) {
  if (getuid() == 0 || geteuid() == 0 || getauxval(AT_SECURE) || platf::has_elevated_privileges(true)) {
    std::cerr << "RKMoon refuses root, setuid, or capability-based execution\n";
    return 1;
  }
  // No Sunshine command dispatcher or config overrides from untrusted argv.
  if (argc != 2 || argv[1][0] != '/') {
    std::cerr << "usage: rkmoon-kvm /absolute/path/to/private/sunshine.conf\n";
    return 2;
  }
  // Required private state, also for direct invocation (not only the supervisor).
  for (const char *name : {"HOME", "XDG_CONFIG_HOME"}) {
    const char *path = std::getenv(name);
    struct stat state {};
    if (!path || path[0] != '/' || lstat(path, &state) != 0 || !S_ISDIR(state.st_mode) ||
        state.st_uid != geteuid() || (state.st_mode & 0077) != 0) {
      std::cerr << "Private absolute HOME and XDG_CONFIG_HOME directories (0700) required\n";
      return 1;
    }
  }
  umask(0077);
  // Only the two shipped compatibility assets are relative. Private config,
  // HOME/XDG, worker and admin socket paths remain absolute and user-owned.
  std::error_code cwd_error;
  auto binary = std::filesystem::read_symlink("/proc/self/exe", cwd_error);
  if (cwd_error) { std::cerr << "Cannot resolve dedicated binary directory\n"; return 1; }
  for (const char *name : {"apps.json", "box.png"}) {
    if (!std::filesystem::is_regular_file(binary.parent_path() / "assets" / name, cwd_error) || cwd_error) {
      std::cerr << "Dedicated compatibility asset missing; refusing to start\n";
      return 1;
    }
  }
  std::filesystem::current_path(binary.parent_path(), cwd_error);
  if (cwd_error) { std::cerr << "Cannot anchor dedicated assets directory\n"; return 1; }
  mail::man = std::make_shared<safe::mail_raw_t>();
  lifetime::argv = argv;
  if (config::parse(argc, argv)) return 1;
  auto log_guard = logging::init(config::sunshine.min_log_level, config::sunshine.log_file);
  auto shutdown = mail::man->event<bool>(mail::shutdown);
  std::string admin_path;
  int admin_fd = rkmoon_sunshine::open_admin_socket(admin_path);
  if (admin_fd < 0) {
    BOOST_LOG(error) << "RKMoon private admin socket unavailable; refusing to start GameStream";
    return 1;
  }
  auto admin_guard = util::fail_guard([&] { rkmoon_sunshine::close_admin_socket(admin_fd, admin_path); });
  // Discard any apps.json, including prep/detached commands, regardless of its content.
  // Kept proc implementation is only used for Sunshine's fixed session bookkeeping.
  auto &apps = proc::proc.get_apps();
  apps.clear();
  proc::ctx_t hdmi {};
  hdmi.name = "HDMI";
  hdmi.id = "1";
  apps.push_back(std::move(hdmi));
  task_pool.start(1);
  struct sigaction action {};
  action.sa_handler = shutdown_signal;
  sigemptyset(&action.sa_mask);
  sigaction(SIGTERM, &action, nullptr);
  sigaction(SIGINT, &action, nullptr);
  auto platform = platf::init();
  if (!platform) { BOOST_LOG(error) << "RKMoon platform initialization failed"; return 1; }
  auto process = proc::init();
  reed_solomon_init();
  auto input = input::init();
  if (video::probe_encoders() != 0 || http::init() != 0) {
    BOOST_LOG(error) << "RKMoon hardware or certificate initialization failed";
    return 1;
  }
  std::jthread http_thread {nvhttp::start};
  std::jthread rtsp_thread {rtsp_stream::start};
  std::jthread admin_thread {rkmoon_sunshine::admin_socket, admin_fd};
  using namespace std::chrono_literals;
  while (!stopping.load() && !shutdown->peek()) std::this_thread::sleep_for(100ms);
  shutdown->raise(true);
  admin_thread.request_stop();
  admin_thread.join();
  http_thread.join();
  rtsp_thread.join();
  task_pool.stop();
  task_pool.join();
  return lifetime::desired_exit_code;
}

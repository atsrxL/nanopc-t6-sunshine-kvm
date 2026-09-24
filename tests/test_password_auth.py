# SPDX-License-Identifier: GPL-3.0-or-later
"""Production password verifier and transformed route guards; no hardware."""
import importlib.util
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("password_overlay", ROOT / "tools/apply_sunshine.py")
overlay = importlib.util.module_from_spec(spec)
spec.loader.exec_module(overlay)

class PasswordTests(unittest.TestCase):
    def test_verifier_and_all_routes(self):
        repo = ROOT / "vendor/sunshine"
        if not (repo / ".git").exists():
            self.skipTest("pinned Sunshine checkout required")
        original = subprocess.check_output(["git", "-C", str(repo), "show", overlay.PIN + ":src/nvhttp.cpp"], text=True)
        changed = overlay.make_changes({"src/nvhttp.cpp": original})["src/nvhttp.cpp"]
        self.assertNotIn("http_server.verify =", changed)
        self.assertNotIn("resource[\"^/pair$\"]", changed)
        self.assertIn("root.RKMoonAuth", changed)
        self.assertEqual(changed.count('const auto rkmoon_mode = rkmoon_sunshine::mouse_mode(args);'),2)
        self.assertEqual(changed.count('launch_session->rkmoon_absolute_mouse = *rkmoon_mode;'),2)
        for prefix in ('auto','const auto'):
            assignment=changed.index('    '+prefix+' launch_session = make_launch_session(host_audio, args);')
            self.assertGreater(changed.rfind('if (!rkmoon_mode.has_value())',0,assignment),0)
        def transformed(name):
            original=subprocess.check_output(['git','-C',str(repo),'show',overlay.PIN+':'+name],text=True)
            return overlay.make_changes({name:original})[name]
        video=transformed('src/video.h')
        self.assertGreater(video.index('bool rkmoon_absolute_mouse'),video.index('int enableIntraRefresh'))
        rtsp=transformed('src/rtsp.cpp')
        self.assertLess(rtsp.index('config.monitor.rkmoon_absolute_mouse ='),rtsp.index('rkmoon_sunshine::request_supported'))
        inputs=transformed('src/input.cpp')
        self.assertIn('if (rkmoon_sunshine::enabled()) return; // HDMI needs no desktop cursor wake',inputs)
        self.assertEqual(changed.count("rkmoon_sunshine::put_display_info(tree);"), 1)
        self.assertIn('resource["^/serverinfo$"]["GET"] = serverinfo<SimpleWeb::HTTP>', changed)
        begin = changed.index("    // Authenticate every HTTP request")
        end = changed.index("    http_server.config.reuse_address", begin)
        guard = changed[begin:end]
        source = r'''
#include "sunshine/rkmoon_password.hpp"
#include <cassert>
#include <functional>
#include <map>
#include <memory>
struct Request { std::multimap<std::string,std::string> header; };
struct Response {
  std::string body; bool close_connection_after_response = false;
  void write(const std::string &s) { body = s; }
};
using Handler = std::function<void(std::shared_ptr<Response>,std::shared_ptr<Request>)>;
struct Server { std::map<std::string,std::map<std::string,Handler>> resource; };
int main() {
  Server http_server;
  int called = 0;
  for (auto path : {"serverinfo","applist","appasset","launch","resume","cancel"})
    http_server.resource[path]["GET"] = [&](auto,auto) { ++called; };
''' + guard + r'''
  for (auto &[path, methods] : http_server.resource) {
    auto request = std::make_shared<Request>();
    auto response = std::make_shared<Response>();
    auto &handler = methods.at("GET");
    request->header.emplace("Authorization", "Basic a3ZtOmt2bQ==");
    auto before = called;
    handler(response, request); assert(called == before + 1);
    // Same request/connection without credentials must not inherit admission.
    request->header.clear(); handler(response, request);
    assert(called == before + 1 && response->close_connection_after_response);
    assert(response->body.find("401") != std::string::npos);
  }
  rkmoon_sunshine::password_auth verifier;
  auto duplicate = std::make_shared<Request>();
  duplicate->header.emplace("Authorization", "Basic a3ZtOmt2bQ==");
  duplicate->header.emplace("Authorization", "Basic a3ZtOmt2bQ==");
  auto response = std::make_shared<Response>();
  auto before = called;
  http_server.resource.at("launch").at("GET")(response, duplicate);
  assert(called == before && response->body.find("401") != std::string::npos);
  assert(!verifier.authorize("Basic a3ZtOndyb25n"));
  assert(!verifier.authorize("kvm"));
  assert(!verifier.authorize(std::string(1025, char(65))));
  assert(verifier.authorize("Basic a3ZtOmt2bQ=="));
  for (int i = 0; i < 10; ++i) verifier.authorize("");
  assert(!verifier.authorize("Basic a3ZtOmt2bQ=="));
  rkmoon_sunshine::password_auth custom("other");
  assert(!custom.authorize("Basic a3ZtOmt2bQ=="));
}
'''
        compiler = shutil.which("c++")
        if not compiler: self.skipTest("C++ compiler required")
        flags = []
        openssl = Path("/opt/homebrew/opt/openssl@3")
        if openssl.exists():
            flags = ["-I" + str(openssl / "include"), "-L" + str(openssl / "lib")]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / "test.cpp").write_text(source)
            subprocess.run([compiler, "-std=c++17", "-I" + str(ROOT), *flags, str(path / "test.cpp"), "-lcrypto", "-o", str(path / "test")], check=True, capture_output=True)
            subprocess.run([str(path / "test")], check=True, capture_output=True)

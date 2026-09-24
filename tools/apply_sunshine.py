#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Commit-locked, fail-closed Sunshine source overlay. Default operation is a dry run."""
import argparse
import difflib
import hashlib
import json
import os
import re
from pathlib import Path
import shutil
import subprocess
import tempfile

PIN="63d35f702ee9e362e43263742981836ec0710384"
ROOT=Path(__file__).resolve().parents[1]

class PatchError(RuntimeError):
    pass

def once(text, old, new):
    count=text.count(old)
    if count != 1:
        raise PatchError(f"anchor count {count}, expected 1: {old[:100]!r}")
    return text.replace(old,new,1)

def at_entry(text, signature, body):
    # Permit upstream's multiline formatting without guessing a different symbol/overload.
    tokens=re.findall(r"\w+|::|&&|[^\w\s]",signature)
    pattern=r"\s*".join(re.escape(t) for t in tokens)+r"\s*\{[ \t]*\n"
    matches=list(re.finditer(pattern,text))
    if len(matches)!=1:
        raise PatchError(f"function anchor count {len(matches)}, expected 1: {signature}")
    end=matches[0].end()
    return text[:end]+body+text[end:]

def make_changes(original):
    changes={}
    for path,text in original.items():
        if path not in {"src/thread_safe.h", "CMakeLists.txt", "cmake/targets/common.cmake", "src/nvhttp.h", "src/video.h", "src/rtsp.h", "cmake/compile_definitions/linux.cmake"}:
            text=once(text,"// local includes\n",'// local includes\n#include "src/rkmoon/rkmoon_bridge.hpp"\n')
        if path=="src/video.h":
            anchor="    int enableIntraRefresh;  ///< Intra refresh setting: 0 = disabled, 1 = enabled.\n"
            text=once(text,anchor,anchor+"    bool rkmoon_absolute_mouse = false;\n")
        elif path=="src/rtsp.h":
            anchor="    std::string client_cert;  ///< PEM certificate for the paired Moonlight client.\n"
            text=once(text,anchor,anchor+"    bool rkmoon_absolute_mouse = false;\n")
        elif path=="src/video.cpp":
            text=at_entry(text,"  void capture(safe::mail_t mail, config_t config, void *channel_data)",
                "    if (rkmoon_sunshine::enabled()) { rkmoon_sunshine::capture(std::move(mail), config, channel_data); return; }\n")
            text=at_entry(text,"  int probe_encoders()", "    if (rkmoon_sunshine::enabled()) { return rkmoon_sunshine::probe(); }\n")
        elif path=="src/input.cpp":
            text=once(text,"    task_pool.pushDelayed([]() {\n      platf::move_mouse(platf_input, 1, 1);","    task_pool.pushDelayed([]() {\n      if (rkmoon_sunshine::enabled()) return; // HDMI needs no desktop cursor wake; preserve selected USB mode.\n      platf::move_mouse(platf_input, 1, 1);")
            text=at_entry(text,"  void print(void *payload)",
                "    if (rkmoon_sunshine::enabled()) { return; } // Never log decoded input, even at verbose.\n")
            text=at_entry(text,"  inline int apply_shortcut(short keyCode)","    if (rkmoon_sunshine::enabled()) { return 0; } // Send shortcuts to the USB host, not T6.\n")
        elif path=="src/platform/virtualhid_input.cpp":
            text=at_entry(text,"  std::unique_ptr<lvh::Runtime> create_runtime(lvh::BackendKind backend)",
                "    if (rkmoon_sunshine::enabled()) { return {}; } // No T6 uinput/libvirtualhid devices.\n")
            calls={
                "  void move_mouse(input_t &input, int deltaX, int deltaY)":"rkmoon_sunshine::relative(deltaX, deltaY);",
                "  void abs_mouse(input_t &input, const touch_port_t &touch_port, float x, float y)":"rkmoon_sunshine::absolute(touch_port, x, y);",
                "  void button_mouse(input_t &input, int button, bool release)":"rkmoon_sunshine::button(button, release);",
                "  void scroll(input_t &input, int high_res_distance)":"rkmoon_sunshine::scroll(0, high_res_distance);",
                "  void hscroll(input_t &input, int high_res_distance)":"rkmoon_sunshine::scroll(high_res_distance, 0);",
                "  void keyboard_update(input_t &input, uint16_t modcode, bool release, uint8_t flags)":"rkmoon_sunshine::key(modcode, release, flags);",
                "  void unicode(input_t &input, const char *utf8, int size)":"/* No clipboard/text injection. */",
                "  void touch_update(client_input_t *input, const touch_port_t &touch_port, const touch_input_t &touch)":"/* No touch injection. */",
                "  void pen_update(client_input_t *input, const touch_port_t &touch_port, const pen_input_t &pen)":"/* No pen injection. */",
            }
            for signature,call in calls.items():
                text=at_entry(text,signature,"    if (rkmoon_sunshine::enabled()) { "+call+" return; }\n")
        elif path=="src/platform/linux/misc.cpp":
            text=at_entry(text,"  fs::path appdata()",
                '    if (rkmoon_sunshine::enabled()) { auto base = fs::path(lizardbyte::common::get_env("XDG_CONFIG_HOME")); return base.is_absolute() ? base / "sunshine" : fs::path("/nonexistent/rkmoon-private-state-required"); } // Never migrate an installed Sunshine state.\n')
            text=at_entry(text,"  std::unique_ptr<deinit_t> init()",
                "    if (rkmoon_sunshine::enabled()) { return std::make_unique<deinit_t>(); } // No EGL/desktop capture initialization.\n")
        elif path=="src/audio.cpp":
            text=at_entry(text,"  void capture(safe::mail_t mail, config_t config, void *channel_data)",
                "    if (rkmoon_sunshine::enabled()) { rkmoon_sunshine::audio_capture(std::move(mail), config, channel_data); return; }\n")
        elif path=="src/httpcommon.cpp":
            text=at_entry(text, "  int init()",
                "    if (rkmoon_sunshine::enabled()) { unique_id = uuid_util::uuid_t::generate().string(); return 0; } // HTTP-only: no TLS credentials needed.\n")
            text=once(text,'      BOOST_LOG(info) << "Open the Web UI to set your new username and password and getting started";',
                '      BOOST_LOG(info) << "RKMoon IP/password authentication over HTTPS; no Web UI";')
        elif path=="src/nvhttp.h":
            text=once(text,"#include <string_view>\n", "#include <string_view>\n#include <stop_token>\n")
            text=once(text,"  bool pin(std::string_view pairing_id, std::string pin, std::string name);",
                "  bool pin(std::string_view pairing_id, std::string pin, std::string name, std::stop_token stop = {});")
        elif path=="src/nvhttp.cpp":
            for prefix in ("auto","const auto"):
                anchor="    "+prefix+" launch_session = make_launch_session(host_audio, args);"
                text=once(text,anchor,'''    const auto rkmoon_mode = rkmoon_sunshine::mouse_mode(args);
    if (!rkmoon_mode.has_value()) {
      tree.put("root.resume", 0);
      tree.put("root.<xmlattr>.status_code", 400);
      tree.put("root.<xmlattr>.status_message", "Unsupported RKMoon mouse mode");
      return;
    }
'''+anchor+'\n    launch_session->rkmoon_absolute_mouse = *rkmoon_mode;')
            text=once(text, '// local includes\n', '// local includes\n#include "src/rkmoon/rkmoon_display.hpp"\n')
            text=once(text, '    tree.put("root.PairStatus", pair_status);', '    rkmoon_sunshine::put_display_info(tree);\n    tree.put("root.PairStatus", pair_status);')
            text=at_entry(text,"  void print_req(std::shared_ptr<typename SimpleWeb::ServerBase<T>::Request> request)",
                '    if (rkmoon_sunshine::enabled()) { BOOST_LOG(debug) << "RKMoon GameStream request (content redacted)"; return; }\n')
            text=once(text,'    BOOST_LOG(debug) << sess.client.cert;',
                '    BOOST_LOG(debug) << "RKMoon pairing certificate received (redacted)";')
            text=once(text,'        BOOST_LOG(debug) << subject_name << " -- "sv << (verified ? "verified"sv : "denied"sv);',
                '        BOOST_LOG(debug) << "Client certificate " << (verified ? "verified"sv : "denied"sv);')
            text=once(text,"  bool pin(const std::string_view pairing_id, std::string pin, std::string name) {",
                "  bool pin(const std::string_view pairing_id, std::string pin, std::string name, std::stop_token stop) {")
            text=once(text,"completion_deadline = std::min(sess.async_insert_pin.expires_at, now + config::stream.ping_timeout);",
                "completion_deadline = std::min({sess.async_insert_pin.expires_at, now + config::stream.ping_timeout, now + std::chrono::seconds(30)});")
            text=once(text,"""      if (completion->condition.wait_until(lock, completion_deadline, [&completion]() {
            return completion->result.has_value();
          })) {
        return *completion->result;
      }
""","""      // Bounded wait so SIGTERM cannot stall the admin socket worker for
      // an operator-configured multi-minute ping_timeout.
      while (!stop.stop_requested() && std::chrono::steady_clock::now() < completion_deadline) {
        if (completion->condition.wait_until(lock, std::min(completion_deadline,
              std::chrono::steady_clock::now() + std::chrono::milliseconds(100)), [&completion]() {
              return completion->result.has_value();
            })) return *completion->result;
      }
""")
            # Replace enrollment with per-request password authentication.
            # Pairing used to persist the freshly generated identity. Password mode
            # must persist it at startup or every restart breaks the client binding.
            text=once(text, "    // Verify certificates after establishing connection\n", "    // Password authentication replaces client certificate enrollment.\n")
            text=once(text, "    if (!clean_slate) {\n      load_state();\n    }",
                "    if (!clean_slate) {\n      load_state();\n      if (!fs::exists(config::nvhttp.file_state)) save_state();\n    }")
            text=once(text, "// local includes\n", "// local includes\n#include \"src/rkmoon/rkmoon_password.hpp\"\n")
            text=once(text, """    https_server.verify = [](SSL *ssl) {
      crypto::x509_t x509 {
#if OPENSSL_VERSION_MAJOR >= 3
        SSL_get1_peer_certificate(ssl)
#else
        SSL_get_peer_certificate(ssl)
#endif
      };
      if (!x509) {
        BOOST_LOG(info) << "unknown -- denied"sv;
        return 0;
      }

      int verified = 0;

      auto fg = util::fail_guard([&]() {
        char subject_name[256];

        X509_NAME_oneline(X509_get_subject_name(x509.get()), subject_name, sizeof(subject_name));

        BOOST_LOG(debug) << "Client certificate " << (verified ? "verified"sv : "denied"sv);
      });

      std::lock_guard lock {client_auth_mutex()};
      auto err_str = verify_client_certificate(x509.get());
      if (err_str) {
        BOOST_LOG(warning) << "SSL Verification error :: "sv << err_str;

        return verified;
      }

      // Check if this client is enabled
      auto pem = crypto::pem(x509);
      auto [enabled, client_name] = get_client_status(pem);
      if (!enabled) {
        BOOST_LOG(info) << "Client is disabled -- denied"sv;
        return verified;
      }

      last_verified_client_cert = pem;
      last_verified_client_name = client_name;
      verified = 1;

      return verified;
    };

""", '')
            text=once(text, "    https_server.resource[\"^/pair$\"][\"GET\"] = [](auto resp, auto req) {\n      pair<SunshineHTTPS>(resp, req);\n    };", '')
            text=once(text, "    http_server.resource[\"^/pair$\"][\"GET\"] = [](auto resp, auto req) {\n      pair<SimpleWeb::HTTP>(resp, req);\n    };", '')
            text=once(text, '    tree.put("root.PairStatus", pair_status);', "    tree.put(\"root.RKMoonAuth\", \"password-v1\");\n    tree.put(\"root.PairStatus\", std::is_same_v<SunshineHTTPS, T> ? 1 : 0);")
            text=once(text, '    https_server.config.reuse_address = true;', r"""    // Authenticate every HTTPS request, including keep-alive requests.
    rkmoon_sunshine::password_auth password;
    for (auto &[path, methods] : https_server.resource) {
      for (auto &[method, handler] : methods) {
        handler = [&password, next = handler](auto response, auto request) {
          const auto range = request->header.equal_range("Authorization");
          const bool single = range.first != range.second && std::next(range.first) == range.second;
          if (!password.authorize(single ? std::string_view(range.first->second) : std::string_view{})) {
            response->write("<root status_code=\"401\" status_message=\"Password authentication failed\"/>");
            response->close_connection_after_response = true;
            return;
          }
          next(response, request);
        };
      }
    }

""" + '    https_server.config.reuse_address = true;')
            # User-requested plain HTTP transport, with every route password gated.
            text=once(text, '    // Password authentication replaces client certificate enrollment.\n    https_server.on_verify_failed = [](resp_https_t resp, req_https_t req) {\n      pt::ptree tree;\n      auto g = util::fail_guard([&]() {\n        std::ostringstream data;\n\n        pt::write_xml(data, tree);\n        resp->write(data.str());\n        resp->close_connection_after_response = true;\n      });\n\n      tree.put("root.<xmlattr>.status_code"s, 401);\n      tree.put("root.<xmlattr>.query"s, req->path);\n      tree.put("root.<xmlattr>.status_message"s, "The client is not authorized. Certificate verification failed."s);\n    };\n\n', '')
            text=once(text, '    auto pkey = file_handler::read_file(config::nvhttp.pkey.c_str());\n    auto cert = file_handler::read_file(config::nvhttp.cert.c_str());\n    setup(pkey, cert);\n', '')
            text=once(text, '    https_server_t https_server {config::nvhttp.cert, config::nvhttp.pkey};\n', '')
            text=once(text, '    https_server.config.reuse_address = true;\n    https_server.config.address = net::get_bind_address(address_family);\n    https_server.config.port = port_https;\n\n    http_server.default_resource["GET"] = not_found<SimpleWeb::HTTP>;\n    http_server.resource["^/serverinfo$"]["GET"] = serverinfo<SimpleWeb::HTTP>;\n', '')
            text=once(text, '    std::jthread ssl {accept_and_run, &https_server};\n', '')
            text=once(text, '    https_server.stop();\n', '')
            text=once(text, '    ssl.join();\n', '')
            text=once(text, '    tree.put("root.HttpsPort", net::map_port(PORT_HTTPS));', '    tree.put("root.HttpsPort", 0);')
            text=once(text, '    tree.put("root.RKMoonAuth", "password-v1");', '    tree.put("root.RKMoonAuth", "password-http-v1");')
            text=once(text, '    tree.put("root.PairStatus", std::is_same_v<SunshineHTTPS, T> ? 1 : 0);', '    tree.put("root.PairStatus", 1);')
            text=once(text, '  using resp_https_t = std::shared_ptr<typename SimpleWeb::ServerBase<SunshineHTTPS>::Response>;', '  using resp_https_t = std::shared_ptr<typename SimpleWeb::ServerBase<SimpleWeb::HTTP>::Response>;')
            text=once(text, '  using req_https_t = std::shared_ptr<typename SimpleWeb::ServerBase<SunshineHTTPS>::Request>;', '  using req_https_t = std::shared_ptr<typename SimpleWeb::ServerBase<SimpleWeb::HTTP>::Request>;')
            if text.count("https_server.") != 8: raise PatchError("HTTP route migration count changed")
            text=text.replace("https_server.", "http_server.")
            if text.count("print_req<SunshineHTTPS>(request);") != 5: raise PatchError("HTTP handler migration count changed")
            text=text.replace("print_req<SunshineHTTPS>(request);", "print_req<SimpleWeb::HTTP>(request);")
            text=once(text, "= not_found<SunshineHTTPS>;", "= not_found<SimpleWeb::HTTP>;")
            text=once(text, "= serverinfo<SunshineHTTPS>;", "= serverinfo<SimpleWeb::HTTP>;")
            text=once(text, "// Authenticate every HTTPS request", "// Authenticate every HTTP request")
        elif path=="src/stream.cpp":
            text=once(text,'        << util::hex_vec(payload) << std::endl',
                '        << "[RKMoon control payload redacted]" << std::endl')
            for kind in ('ping [v2]', 'ping [v1]', 'non-ping'):
                old='        BOOST_LOG(debug) << "Received '+kind+' from "sv << recv_peer.address() << \':\' << recv_peer.port() << " ["sv << util::hex_vec(msg) << \']\';'
                new='        BOOST_LOG(debug) << "Received '+kind+' (payload redacted)";'
                text=once(text,old,new)
        elif path=="src/rtsp.cpp":
            # Keep sizes/status but never log SDP/options/raw request or response.
            text=at_entry(text,"  void print_msg(PRTSP_MESSAGE msg)",
                '    if (rkmoon_sunshine::enabled()) { BOOST_LOG(debug) << "RKMoon RTSP message (content redacted)"; return; }\n')
            text=once(text,'          BOOST_LOG(debug) << "Found Host: "sv << content;',
                '          BOOST_LOG(debug) << "Found RTSP Host header (redacted)";')
            anchor="    auto stream_session = stream::session::alloc(config, session);\n"
            text=once(text,anchor,"""    // RKMoon admission occurs AFTER upstream parsing/encryption validation and BEFORE input allocation.
    if (rkmoon_sunshine::enabled()) {
      if (server->session_count() != 0) {
        respond(sock, session, &option, 453, "RKMoon exclusive session busy", req->sequenceNumber, {});
        return;
      }
      config.monitor.rkmoon_absolute_mouse = session.rkmoon_absolute_mouse;
      if (!rkmoon_sunshine::request_supported(config.monitor)) {
        respond(sock, session, &option, 400, "RKMoon unsupported mode or capture not authorized", req->sequenceNumber, {});
        return;
      }
    }
"""+anchor)
        elif path=="src/thread_safe.h":
            anchor="    explicit queue_t(std::uint32_t max_elements = 32, overflow_policy_e overflow = overflow_policy_e::drop_oldest):"
            methods="""    // RKMoon producer-specific methods: bounded, locked, no implicit reference-frame loss.
    bool rkmoon_try_raise(std::size_t limit, T &&item) {
      std::lock_guard lock {_lock};
      if (!_continue || _queue.size() >= limit) { return false; }
      _queue.emplace_back(std::move(item));
      _cv.notify_all();
      return true;
    }
    std::size_t rkmoon_size() { std::lock_guard lock {_lock}; return _queue.size(); }
    bool rkmoon_running() { std::lock_guard lock {_lock}; return _continue; }

"""
            text=once(text,anchor,methods+anchor)
        elif path=="cmake/compile_definitions/linux.cmake":
            text=once(text,"if(NOT ${CUDA_FOUND}\n        AND NOT ${LIBDRM_FOUND}",
                "if(NOT RKMOON_MINIMAL_BUILD AND NOT ${CUDA_FOUND}\n        AND NOT ${LIBDRM_FOUND}")
        elif path=="cmake/targets/common.cmake":
            start=text.index("#WebUI build\n")
            end=text.index("# docs\n",start)
            text=text[:start]+"# No NPM/Web UI target in this dedicated build.\n\n"+text[end:]
        elif path=="CMakeLists.txt":
            text=once(text,"# setup compile definitions\n", "set(RKMOON_MINIMAL_BUILD ON) # Explicit external HDMI/MPP path; no desktop capture backend.\nset(SUNSHINE_ASSETS_DIR_DEF assets) # Main anchors cwd to this binary directory.\n# setup compile definitions\n")
            text=once(text,"# target definitions\n",'''# Only the GameStream host and RTSP network service; no Web UI, UPnP or desktop entrypoint.
list(REMOVE_ITEM SUNSHINE_TARGET_FILES
  "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/confighttp.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/upnp.cpp")
list(APPEND SUNSHINE_TARGET_FILES "${CMAKE_CURRENT_SOURCE_DIR}/src/rkmoon/rkmoon_main.cpp")
# target definitions
''')
            text+='''
# RKMoon dedicated HDMI build. Do not install over a pre-existing Sunshine.
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  message(FATAL_ERROR "RKMoon overlay requires Linux")
endif()
target_sources(sunshine PRIVATE
  src/rkmoon/rkmoon_bridge.cpp
  src/rkmoon/rkmoon_audio.cpp
  src/rkmoon/core.cpp
  src/rkmoon/annexb.cpp
  src/rkmoon/hid_client.cpp)
target_include_directories(sunshine PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/rkmoon/include")
find_library(RKMOON_ALSA_LIB asound REQUIRED)
target_link_libraries(sunshine "${RKMOON_ALSA_LIB}")
set_target_properties(sunshine PROPERTIES OUTPUT_NAME rkmoon-kvm)
# Only compatibility defaults/artwork, never the Web UI tree.
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/assets")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/src/rkmoon/hdmi-apps.json"
  "${CMAKE_CURRENT_BINARY_DIR}/assets/apps.json" COPYONLY)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/src_assets/common/assets/box.png"
  "${CMAKE_CURRENT_BINARY_DIR}/assets/box.png" COPYONLY)
'''
        else:
            raise PatchError("unexpected source file")
        changes[path]=text
    return changes

FILES=["src/video.h","src/rtsp.h","src/video.cpp","src/input.cpp","src/platform/virtualhid_input.cpp","src/platform/linux/misc.cpp","src/audio.cpp","src/httpcommon.cpp","src/nvhttp.cpp","src/nvhttp.h","src/rtsp.cpp","src/stream.cpp","src/thread_safe.h","cmake/compile_definitions/linux.cmake","cmake/targets/common.cmake","CMakeLists.txt"]

def git(repo,*args):
    return subprocess.check_output(["git","-C",str(repo),*args],text=True).strip()

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("repository",type=Path)
    parser.add_argument("--apply",action="store_true",help="explicitly modify only this independent pinned Sunshine checkout")
    parser.add_argument("--patch-output",type=Path,help="optional generated unified diff path")
    args=parser.parse_args();repo=args.repository.resolve()
    if git(repo,"rev-parse","HEAD") != PIN:
        raise PatchError("Sunshine HEAD does not match the audited pin")
    if git(repo,"status","--porcelain"):
        raise PatchError("checkout is not clean; use a new independent clone, never overwrite local work")
    original={p:(repo/p).read_text() for p in FILES}
    changes=make_changes(original)
    diff="".join("".join(difflib.unified_diff(original[p].splitlines(True),changes[p].splitlines(True),fromfile="a/"+p,tofile="b/"+p)) for p in FILES)
    if args.patch_output:
        args.patch_output.write_text(diff)
    if args.apply:
        if (repo/"src/rkmoon").exists():raise PatchError("overlay directory already exists")
        # Stage every payload before altering original files. Roll back source files on any write failure.
        stage=Path(tempfile.mkdtemp(prefix="rkmoon-overlay-",dir=repo.parent))
        try:
            payload=stage/"payload";payload.mkdir()
            for name in ("rkmoon_bridge.hpp","rkmoon_bridge.cpp","rkmoon_audio.cpp","rkmoon_main.cpp","rkmoon_admin.cpp","rkmoon_admin_io.hpp","rkmoon_audio_pcm.hpp","rkmoon_password.hpp","rkmoon_display.hpp"):shutil.copy2(ROOT/"sunshine"/name,payload/name)
            for name in ("core.cpp","annexb.cpp","hid_client.cpp"):shutil.copy2(ROOT/"src"/name,payload/name)
            shutil.copy2(ROOT/"config/hdmi-apps.json",payload/"hdmi-apps.json")
            shutil.copytree(ROOT/"include",payload/"include")
            shutil.copy2(ROOT/"LICENSE",payload/"LICENSE")
            try:
                for p,s in changes.items():
                    tmp=repo/(p+".rkmoon-new");tmp.write_text(s);os.replace(tmp,repo/p)
                shutil.move(str(payload),repo/"src/rkmoon")
            except BaseException:
                for p,s in original.items():(repo/p).write_text(s)
                shutil.rmtree(repo/"src/rkmoon",ignore_errors=True)
                raise
        finally:shutil.rmtree(stage,ignore_errors=True)
    print(json.dumps({"commit":PIN,"operation":"applied" if args.apply else "check-only", "changed_files":len(FILES),
                      "patch_sha256":hashlib.sha256(diff.encode()).hexdigest(),"patch_lines":len(diff.splitlines())},indent=2))

if __name__=="__main__":
    try:main()
    except (PatchError,subprocess.CalledProcessError,OSError) as e:
        raise SystemExit(f"Patch refused: {e}")

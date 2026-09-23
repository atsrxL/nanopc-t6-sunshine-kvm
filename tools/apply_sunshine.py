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
        if path not in {"src/thread_safe.h", "CMakeLists.txt", "cmake/targets/common.cmake", "src/nvhttp.h", "cmake/compile_definitions/linux.cmake"}:
            text=once(text,"// local includes\n",'// local includes\n#include "src/rkmoon/rkmoon_bridge.hpp"\n')
        if path=="src/video.cpp":
            text=at_entry(text,"  void capture(safe::mail_t mail, config_t config, void *channel_data)",
                "    if (rkmoon_sunshine::enabled()) { rkmoon_sunshine::capture(std::move(mail), config, channel_data); return; }\n")
            text=at_entry(text,"  int probe_encoders()", "    if (rkmoon_sunshine::enabled()) { return rkmoon_sunshine::probe(); }\n")
        elif path=="src/input.cpp":
            text=at_entry(text,"  inline int apply_shortcut(short keyCode)","    if (rkmoon_sunshine::enabled()) { return 0; } // Send shortcuts to the USB host, not T6.\n")
        elif path=="src/platform/virtualhid_input.cpp":
            text=at_entry(text,"  std::unique_ptr<lvh::Runtime> create_runtime(lvh::BackendKind backend)",
                "    if (rkmoon_sunshine::enabled()) { return {}; } // No T6 uinput/libvirtualhid devices.\n")
            calls={
                "  void move_mouse(input_t &input, int deltaX, int deltaY)":"rkmoon_sunshine::relative(deltaX, deltaY);",
                "  void abs_mouse(input_t &input, const touch_port_t &touch_port, float x, float y)":"/* Absolute mouse is outside v1 scope. */",
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
            text=at_entry(text,"  std::unique_ptr<deinit_t> init()",
                "    if (rkmoon_sunshine::enabled()) { return std::make_unique<deinit_t>(); } // No EGL/desktop capture initialization.\n")
        elif path=="src/audio.cpp":
            text=at_entry(text,"  void capture(safe::mail_t mail, config_t config, void *channel_data)",
                "    if (rkmoon_sunshine::enabled()) { rkmoon_sunshine::audio_capture(std::move(mail), config, channel_data); return; }\n")
        elif path=="src/nvhttp.h":
            text=once(text,"#include <string_view>\n", "#include <string_view>\n#include <stop_token>\n")
            text=once(text,"  bool pin(std::string_view pairing_id, std::string pin, std::string name);",
                "  bool pin(std::string_view pairing_id, std::string pin, std::string name, std::stop_token stop = {});")
        elif path=="src/nvhttp.cpp":
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
        elif path=="src/rtsp.cpp":
            anchor="    auto stream_session = stream::session::alloc(config, session);\n"
            text=once(text,anchor,"""    // RKMoon admission occurs AFTER upstream parsing/encryption validation and BEFORE input allocation.
    if (rkmoon_sunshine::enabled()) {
      if (server->session_count() != 0) {
        respond(sock, session, &option, 453, "RKMoon exclusive session busy", req->sequenceNumber, {});
        return;
      }
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
            text=once(text,"# setup compile definitions\n", "set(RKMOON_MINIMAL_BUILD ON) # Explicit external HDMI/MPP path; no desktop capture backend.\n# setup compile definitions\n")
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
  src/rkmoon/rkmoon_admin.cpp
  src/rkmoon/core.cpp
  src/rkmoon/annexb.cpp
  src/rkmoon/hid_client.cpp)
target_include_directories(sunshine PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/rkmoon/include")
find_library(RKMOON_ALSA_LIB asound REQUIRED)
target_link_libraries(sunshine PRIVATE "${RKMOON_ALSA_LIB}")
set_target_properties(sunshine PROPERTIES OUTPUT_NAME rkmoon-kvm)
'''
        else:
            raise PatchError("unexpected source file")
        changes[path]=text
    return changes

FILES=["src/video.cpp","src/input.cpp","src/platform/virtualhid_input.cpp","src/platform/linux/misc.cpp","src/audio.cpp","src/nvhttp.cpp","src/nvhttp.h","src/rtsp.cpp","src/thread_safe.h","cmake/compile_definitions/linux.cmake","cmake/targets/common.cmake","CMakeLists.txt"]

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
            for name in ("rkmoon_bridge.hpp","rkmoon_bridge.cpp","rkmoon_audio.cpp","rkmoon_main.cpp","rkmoon_admin.cpp","rkmoon_admin_io.hpp"):shutil.copy2(ROOT/"sunshine"/name,payload/name)
            for name in ("core.cpp","annexb.cpp","hid_client.cpp"):shutil.copy2(ROOT/"src"/name,payload/name)
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

# Client handoff

Scope: `client/` only, branch `feat/minimal-moonlight-client`, base `d5f2ba5`.
Original Claude session stopped due quota; inherited skeleton was **not complete**.
Retained files were inspected rather than overwritten. Added main, single-host
binding/PIN/certificate logic, Widgets frontend, real Session invocation, audio controls,
build recipes and tests. Missing nested ENet and QtSvg/Discord/entrypoint/link issues
were found through actual compilation and fixed. No server changes were merged.

Current critical invariants:

- Require saved certificate + paired state before authenticated fixed-HDMI app lookup.
  Preserve upstream PIN crypto; client displays PIN, local host CLI approves it.
- Keep Session/NvComputer alive until upstream asynchronous `readyForDeletion`, not
  merely until SDL `exec()` returns. Block new UI actions/close while pairing/cleanup.
- Relative mouse forced; absolute-toggle disabled in audited overlay. Gamepad stub
  only disables removed controls; no stub video/audio/network path.
- Opus stereo decode and upstream timing/recovery remain active. Gain is applied
  after decode; no microphone uplink. A/V sync and hotplug recovery need real testing.
- Whitelist HTTP operation logging; no request URL query or launch response body.
  Test logs use synthetic sentinels only.
- First exported evaluation ZIP is unsafe for pairing/streaming and is retained only
  as offline build evidence; exact hash + input manifest are in ACCEPTANCE.md.

Reviewed source commit **b9c273b299e0c906a846b63eea854d2f30b5ba4a** is built on
Windows x64 Qt6.8.3/MSVC2022; offline QtTest6/6, gain assertions and packaged4s
clean-PATH offscreen smoke passed. New reviewed ZIP/evidence and exact hashes are
in ACCEPTANCE.md; source inputs map back to that commit. This later documentation
commit does not change the binary's source identity. VM9004 stopped, lease closed,
controller key material removed; final guard deadline **2026-09-24 21:58:01 CST**.

Next: parent-coordinated actual host PIN approval, HEVC/H.264 GPU decoding, HDMI audio
A/V sync/source recovery, USB input and disconnect release. Do not start VM200,
borrow VM301 or operate the user's .180 desktop without the integration window.
No live KVM endpoint was used by this client task. Licensing/source-offer gaps remain
a public-release blocker; initial evaluation ZIP remains offline-only evidence.

Known UX limitation for follow-up: upstream get-server-cert pairing request uses no
client timeout while waiting for operator approval. This frontend blocks reentry and
normal window close during that worker; it does not yet expose pairing cancellation.
Do not call operator-timeout/cancel recovery validated. Audio renderer retry remains
upstream (200 packets, so wall-clock retry interval depends on negotiated frame size).
The hardware integration should test operator absence, wrong PIN, and 5/10/20ms audio
recovery separately rather than infer them from unit/offscreen tests.

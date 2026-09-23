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

Next: commit reviewed sources, same-task VM9004 resume with locked lifecycle/lineage
checks, build that exact revision, run offline tests, export a newly named package
with source/binary manifest and SMB readback, close lease and stop/refresh 24h guard.
Do not reuse source-template VM200, VM301 or user's .180 desktop. T6 HDMI/audio/input
and Windows GPU acceptance await parent-coordinated integration after server ARM64
build. Licensing/source-offer gaps remain a public-release blocker.

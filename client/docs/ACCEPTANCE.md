# Client acceptance — 2026-09-23

Do not substitute official Moonlight's RX9060XT HEVC1080p60/D3D11VA result for this
custom client. No T6/.180 desktop was accessed by this task.

| Gate | State | Evidence / boundary |
|---|---|---|
| Real upstream + required nested pins | PASS | lockfile; Python git-object/pin tests |
| Exact patched tree + missing-anchor regressions | PASS | `python3 client/tools/test_client.py`; 2 unittest groups |
| Mac arm64 Qt6.11.2 application compile/link | PASS | local installed clang/qmake; not Windows/hardware |
| Windows x64 Qt6.8.3/MSVC2022 initial compile/link | PASS | stopped task-owned VM9004; source-input manifest below |
| Initial Windows offscreen process startup | PASS | isolated clone only, 4 seconds; not a visible display/decoder test |
| Signed16/float audio gain/mute/clamp unit | PASS | Mac ASan/UBSan and Windows MSVC assertion build; not Opus/RTP playback |
| Synthetic secret-log/modifier/relative-policy/reentrant UI regressions | PASS on Mac | QtTest: 4 functional + init/cleanup (6 pass); no network |
| Reviewed-source Windows x64 compile/link | PASS | exact committed b9c273b299e0c906a846b63eea854d2f30b5ba4a, clean client checkout |
| Reviewed Windows synthetic log/modifier/relative-policy/reentry tests | PASS | QtTest 6/6 (4 functional + init/cleanup), signed16/float assertions |
| Reviewed packaged Windows offscreen clean-PATH startup | PASS | 4 seconds, packaged DLLs + OS only; extra offscreen plugin in separate test directory |
| Reviewed internal package / evidence SMB readback | PASS | newly named artifacts below; source/binary/file manifests retained |
| Real pairing / wrong PIN / cert change / encrypted control | NOT TESTED | code reuses pinned upstream; requires host integration |
| Real HEVC/H.264 hardware decode / reconnect / full screen | NOT TESTED | do not claim from offscreen startup |
| Opus RTP 5/10/20ms / A/V sync / mute-unmute / source loss-recovery | NOT TESTED | production renderer preserved; host silence/recovery not exercised |
| Actual relative keyboard/mouse / release on disconnect | NOT TESTED | requires coordinated hardware window |
| Public-release license/source bundle | INCOMPLETE | see LICENSES.md; internal evaluation only |

## First Windows evaluation — superseded, offline evidence ONLY

**Do not pair or stream with this first ZIP**, including on the user's .180 desktop.
Later review identified upstream HTTP URL logging of launch `rikey`, an incorrect
modifier-mask comparison, absolute-mouse toggle and UI reentry issues. All are fixed
in current sources but **not in this initial ZIP**. No real PIN/session/stream was
created during its offscreen test, so that test did not log actual pairing/input keys.

- URI: `smb://192.168.123.10/zssd/Target/RKMoon-Windows-x64-evaluation-20260923-9004.zip`
- Size: **21,300,162 bytes**.
- SHA256: **a21379069604492686a877ffc55d9c16fe6cb382b4da1b0a73daa60f3cfa5bd6**.
- SHA256 sidecar exists; exact Target probe and final destination size/hash readback
  succeeded through the existing authenticated SMB mount. No share permissions changed.
- Project base: `d5f2ba5`; then-uncommitted client inputs are individually hashed in
  `client/results/20260923/windows-build-inputs.json` (not attributed to the later commit).
- Applied patch: `client/results/20260923/evaluation-overlay.patch`, SHA256
  `3d1f3b3d0b77ba2dbe128d95b1a59f0811204cfacaaa43e9bf6d7c20dcce8fed`.
- Actual old source: `D:\Build\src\rkmoon-client-9004`; build/logs/Qt/scripts:
  `D:\Build\work\rkmoon-client-9004`, `D:\Build\logs\rkmoon-client-9004`.
- Logs retained: `fetch.log`, `patch.log`, `source-test.log`, `build.log` (MSVC empty
  array failure), `build-retry.log` (SDL_main entry failure), `build-retry2.log`,
  `build-final.log` (successful final link), `gain-test-build.log`, `smoke.stdout/stderr`,
  `windeployqt.log`. Both compile failures were fixed and rerun before export.
- Current source changes after that package are deliberately **not** described as
  Windows-verified until a new exact-revision binary is built.

## VM lifecycle evidence (first build)

MS-A2 originally online; left online. VM301 server worker untouched. Source VM200
stopped/agent1/no lock, exact `env-completed` EFI/scsi0/TPM snapshots verified.
Candidate9004 unused in VM/CT/storage/cleanup audit. Both `qm clone --full 1` and
`--full 0` refused exact EFI snapshot with exit2; neither left partial config/volumes.
After explicit linked authorization, three ZFS overlays were created from exact
`vmdata/data/vm-200-disk-{0,1,2}@env-completed`; origins verified before/after assembly.
Clone9004 `codex-win-rkmoon-client-20260923`: 4vCPU,16GiB,one384GiB scsi0,EFI/TPM,
source disk options preserved. Private generation-token state/description bound ownership.
QGA ping + health passed; C:NTFS/D:ReFS healthy. New publickey-only SSH lease fingerprint
matched QGA Ed25519 SHA256. Exact lease closed through QGA; health then showed no lease,
sshd stopped/manual and zero host private keys. Clone gracefully stopped; source still
stopped; guest sources/logs intentionally retained and cross-task reuse forbidden.

Initial final cleanup deadline (superseded by reviewed build below): **2026-09-24 21:32:29 CST**,
`codex-win-clone-9004-cleanup.timer/service`; clone-specific guard `--check` passed.
Any same-build resume must first lock, set active lease/hold, refresh heartbeat/deadline
and revalidate metadata/origins/competing cleanup. Record the renewed final deadline in
the reviewed-build result; never treat this old deadline as current after resume.

## Reviewed Windows build — exact source commit b9c273b

- Commit: **b9c273b299e0c906a846b63eea854d2f30b5ba4a**, detached public-project checkout,
  `git status --porcelain -- client` empty before/after build. Later commits only
  record results/docs unless explicitly stated; do not relabel the binary revision.
- Reviewed app SHA256: **806ced4903f8c9d5bd290eb9fa4be2b11760b5d1acba797ce2dfc34bc99fe554**,
  **606,208 bytes**. Qt6.8.3 / VS2022 x64 Release.
- Package URI: **smb://192.168.123.10/zssd/Target/RKMoon-Windows-x64-reviewed-b9c273b299e0.zip**
- Package size: **29,327,080 bytes**; SHA256:
  **96e816abadecfb2b5de63ceca2e79569c886ae71d208767a4e74d5b2b858896f**.
- Evidence ZIP: **smb://192.168.123.10/zssd/Target/RKMoon-Windows-x64-build-evidence-b9c273b299e0.zip**,
  **18,273 bytes**; SHA256:
  **ea02e6058f5be4a79745488aef5d9aac47d9db639c3a26208097b9d8d91858d3**.
- Both have SHA256 sidecars; unique Target write/read probe, temporary-name transfer,
  rename, final full size/hash readback all passed. Old evaluation ZIP was not overwritten.
- Exact build/input manifest: `client/results/20260923/reviewed-windows-manifest.json`.
  All 16 application/project inputs were compared back to the committed Git objects;
  they match allowing only Git's Windows CRLF checkout normalization. Normalized
  generated overlay SHA256: `d3901d01a9a865e7fbeb10ad499d2396f020a573638c533cd589569b555bb798`;
  Windows CRLF patch file SHA256: `6e7e0f7d90c421594575d65450b35e7674e5a29ac0fe9895c2551f581405e651`.
- Build command: node `Invoke-InBuildEnvironment.ps1 -VisualStudio 2022`, then
  committed `client/tools/build_windows.ps1` with task-local Qt and a fresh shadow
  parent. `package_windows.ps1` from the same commit generated the reviewed ZIP.
- Tests: `python client/tools/test_client.py`; app qmake `CONFIG+=rkmoon_tests` + nmake;
  QtTest `QT_QPA_PLATFORM=offscreen`; standalone cl-built gain assertions. All exited0.
  QtTest log committed separately; harmless offscreen missing-font-directory warning,
  not evidence of an actual desktop font failure. No live endpoint was contacted.
- Package clean-PATH smoke copied the package into a separate test directory and
  added only Qt's offscreen plugin there. Process lived4s then was explicitly stopped;
  no test plugin was inserted into the exported package.
- Full build/log/source evidence retained under `D:\\Build\\{src,work,logs}\\rkmoon-client-9004-reviewed`;
  packaging under `D:\\Build\\artifacts\\rkmoon-client-9004-reviewed`. Build evidence ZIP
  preserves logs beyond the VM retention window. Package includes source/binary and
  per-file dependency manifests, exact overlay, GPL/MIT/LGPL notices and license-gap warning.

### Reviewed lifecycle closeout

Same-task resume audited known cleanup paths, checked guard/metadata/token/three
ZFS origins, locked state, set active controller + deletion hold, refreshed deadline,
then started only9004. QGA health passed; a **new** task key/lease matched the new
QGA Ed25519 fingerprint before login. After export, exact lease closed via QGA;
health verified lease=null, sshd stopped/manual, host-private-key count0. Under the
clone lock: graceful shutdown and both9004/200 stopped verified, delivery=verified,
controller cleared, hold=false, deadline refreshed. Guest environment intentionally
not cleaned, no cross-task reuse; controller keys/lease/known_hosts deleted and absence checked.

**Final deadline: 2026-09-24 21:58:01 CST**. Persistent unit pair:
`codex-win-clone-9004-cleanup.timer/service`; clone-specific identity/lineage/delivery
`--check` passed after stopping. Source snapshots pinned while clone exists. MS-A2
was originally online and stays online; no host or VM301 shutdown scheduled.

**Remaining gate:** parent-coordinated actual host PIN approval, hardware HEVC/H.264,
HDMI audio/AV sync/source recovery and USB input. This reviewed package is still
internal evaluation, not a public release or an end-to-end pass.

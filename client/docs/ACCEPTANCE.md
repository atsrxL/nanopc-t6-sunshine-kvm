# Client acceptance — 2026-09-24 (password over plaintext HTTP)

## 1440p90 client follow-up

Explicit 2560×1440/90 experimental selection is implemented in the settings UI;
1920×1080/60 remains the default. Mac arm64 Qt 6.11.2 offline tests now pass
12/12, including UI preset selection, persistence, and exact StreamingPreferences
dimensions/fps. Python source checks pass. This is not Windows or hardware validation.
Native Windows compile/package and offline Qt tests now PASS for this follow-up;
real client stream/decoder performance remains **NOT TESTED**. The saved MS-A2 password works when its Markdown backticks are
excluded; the earlier authentication failure was an automation error, not a device
credential change. VM9006 was assembled from VM200 `env-completed` snapshots and
passed QGA health, but VM200 unexpectedly became `running` before build setup.
The phase stopped; VM9006 was gracefully stopped with a deletion hold. After VM200
returned to stopped and the recorded lineage/control guard passed, the same-project
VM9006 resumed. Windows 11 IoT LTSC x64, Qt 6.8.3, VS2022 Release linked successfully;
offline Qt suite 12/12 passed including both new 1440p90 tests. Input snapshot commit
inside the clone: `bac1b3e776b1e1bc9abea6e6c4457ec0ac67ea88`; the shared tree was
not committed. App: 589,312 bytes, SHA256
`f291a14e401b024ebd07950c81161f4d0a76ed0d0a7ec33cae3bcc40706e633b`.
Internal evaluation ZIP: `smb://192.168.123.10/zssd/Target/RKMoon-Windows-x64-1440p90-experimental-bac1b3e776b1.zip`,
29,320,363 bytes, SHA256 `06381939e037dc79b13060f706959ee243b5e639e43d809ec26f3977fe9ef07e`.
Target probe, temporary-name transfer, final full size/hash readback and SHA256 sidecar
passed. VM9006 and VM200 stopped; 9006 guard check passed, same-project retention
until 2026-09-25 10:08:38 CST. The parent reports a real 30-second T6 V4L2 capture at 2560×1440:
2695 frames, timestamp rate 89.9981 fps, 11,059,200 bytes per frame, and zero
sequence gaps or errors. This is a capture-stage result, separate from the earlier
NVIDIA output and T6 timing lock. Real 60-second HEVC and H.264 MPP DMA-BUF import,
encoding, and independent decoding each passed at 5394 frames; dequeue rate was
89.99825/89.98159 fps, raw skips 0/1, and dequeue-to-AU P95 was 7.025/6.672 ms
respectively (see `results/20260924-1440p90/ENCODE.md`). RTP delivery and client
decoding/presentation at 90 fps remain **NOT TESTED**. The planned server-side opt-in is
`RKMOON_ALLOW_1440P90_EXPERIMENT`; the client does not enable that server setting.

`pair`/PIN, TLS, trust-on-first-use and certificate pinning were all removed; the client
now binds an address, a port and a password and speaks to one plaintext HTTP base port
per docs/ADR-005-http-password.md. **The password and all control traffic are readable
and modifiable on the wire by design.** The 2026-09-23 record below is **superseded for
anything touching authentication or transport**.

| Gate | State | Evidence / boundary |
|---|---|---|
| Overlay pins + anchor fail-closed incl. auth and HTTP-base-port anchors | PASS | `python3 client/tools/test_client.py`, 3 groups |
| No pairing, no identity key, no persisted password in client sources | PASS | same run, `test_client_never_pairs_or_persists_a_password` |
| Mac arm64 Qt 6.11.2 application compile/link | PASS | local installed clang/qmake; not Windows, not hardware |
| Offline QtTest suite (10/10) | PASS | offscreen; scoping, UTF-8 base64, redirect policy, no persisted password |
| Loopback HTTP integration of credential and rejection paths | PASS | in-process 127.0.0.1 servers only; **not** the real server |
| Windows x64 package with HTTP password auth | PASS | VM9005, Qt 6.8.3/MSVC2022 Release; exact source snapshot b4367616e16909642e77878abddea64b61798c69; details below |
| Real host login, wrong password, failure budget | NOT TESTED | needs the parent-coordinated integration window |
| Real HEVC/H.264 decode, audio, input | NOT TESTED | unchanged by this task |

## Windows HTTP/password build delivered (2026-09-24)

The current client tree was copied into a new Windows checkout and committed there as
b4367616e16909642e77878abddea64b61798c69. The shared workspace was not committed
or reset. Pinned Moonlight and five nested submodules were fetched fresh; the HTTP
overlay applied to the fixed commit. Source pin/anchor tests: 3/3.

| Check | State | Evidence / boundary |
|---|---|---|
| Native Windows x64 VS2022 Release link | PASS | VM9005, Qt 6.8.3; app 586,752 bytes, SHA256 941d845ab9985f162293ddaa1c6adb5c8b4a2a9226c240b6b6a55fd0591c9217 |
| Windows QtTest | PASS | 10/10 including HTTP loopback, request scoping, redirect refusal and window reentry |
| Audio gain assertions | PASS | Native MSVC debug CRT standalone test |
| Packaged clean-PATH startup | PASS | Extracted ZIP; process alive after 4s offscreen, then stopped |
| Target SMB write and readback | PASS | ZIP and SHA256 sidecar sizes and full hashes matched after final rename |
| Real HDMI/HEVC/H264/audio/USB | NOT TESTED | Requires a separate integration window |

Package: smb://192.168.123.10/zssd/Target/RKMoon-Windows-x64-reviewed-b4367616e169.zip,
29,319,559 bytes, SHA256 c187fad05223b47fc0735bc162672aa00c6ebfb455a7918cf6727db7c1fd960b.
The matching .zip.sha256 sidecar is 112 bytes and was also read back. This is an
internal evaluation package, not a public release.

MS-A2 was online before and remains online; VM301 was untouched. VM200 stayed
stopped with its env-completed snapshot. VM9005 is a task-owned manual ZFS linked
clone of that exact snapshot; three overlay origins were verified. QGA health and
SSH Ed25519 fingerprint verification passed. The exact temporary lease was closed;
QGA then reported no lease, sshd stopped/manual and zero host private keys.
Controller key material was deleted. VM9005 and VM200 are stopped. VM9005 guest
build/log state is retained with reuse=forbidden. Its guarded destroy-only timer
passed --check and targets 2026-09-25 02:31:26 CST after 24h inactivity.

## What the loopback slot actually proved

`loopbackAuthenticateAndReject` runs `KvmHost` against plain HTTP servers started inside
the test process on 127.0.0.1. It needs no key material or configuration, so it always
runs. Verified end to end:

- An authorized `serverinfo` plus `applist` exchange resolves the host identity and the
  fixed HDMI app, and every request carries the agreed `Authorization` header (value not
  reproduced here).
- `applist` — an upstream "HTTPS" call site — arrives on the single plaintext base port,
  proving the overlay's base-URL redirect. `NvComputer` still substitutes upstream's
  default for the `HttpsPort=0` the server reports, but that value is inert because the
  patched `NvHTTP::setHttpsPort` ignores it.
- A wrong password produces the server's HTTP 200 / root `status_code=401` answer, is
  reported as a rejected password, and leaves no armed credential.
- A host that authorizes but does not advertise `RKMoonAuth` is refused.

Boundaries: loopback servers are synthetic XML responders, not the production Sunshine
handlers. They do not exercise RTSP, RTP/FEC, the real failure budget, real timing, or
Windows. A loopback pass is evidence about this client's logic only.

## Removed attack surface

- `nvpairingmanager` is not compiled: no GameStream PIN pairing crypto in the binary.
- `identitymanager` is not compiled and the upstream client-certificate installation was
  removed from `NvHTTP::openConnection`. The client no longer generates or stores an RSA
  identity key or certificate; the offline test run confirms the previous
  "Wrote new identity credentials to settings" step no longer happens.
- Nothing certificate-related is persisted or checked; `KvmConfig` has no certificate or
  HTTPS-port field.

---

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
| Real pairing / wrong PIN / cert change / encrypted control | SUPERSEDED | pairing removed 2026-09-24; see the password rows above |
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

### Automatic source mode / local cursor client — 2026-09-24

Local Mac arm64 Qt 6.11.2 application and test binaries compile/link. Qt offline suite
17/17 passed; Python pinned-source and exact-overlay regressions 3/3 passed. Dynamic
loopback covers jitter, changed mode, deferred-cleanup guard, cancellation, asynchronous
networking and no-signal wait. Internal transport stops no longer use SDL_QUIT; atomic
pending fallback covers event registration/queue failure. This is synthetic lifecycle
validation, not actual HDMI mode-switch or Windows cursor presentation acceptance.
Native Windows build is running in project-owned VM9006; no new delivery claim yet.


### Native Windows automatic-display delivery (final)

VM9006 env-completed lineage; Qt6.8.3 / VS2022 x64 Release, snapshot 1b610d1bf94a.
Automatic bind/launch, exception reset, forced quitAppAfter and unique rkmoon_main.cpp
are included. Windows Qt17/17, gain and clean-PATH 4-second smoke passed. Initial
runner DLL/plugin path and output quoting issues were fixed before the passing run.
Delivered smb://192.168.123.10/zssd/Target/RKMoon-Windows-x64-auto-display-1b610d1bf94a.zip
29,321,156 bytes; final readback SHA256
475e71cd6ce075768fb819b078c6a7297c05cce5ed01528ef90ddb90d1705c07.
Sidecar readback passed. No actual HDMI client rendering/cursor/switching acceptance.
Legacy SSH lease closed, sshd stopped/manual, controller key deleted. VM9006 stopped
(after PVE shutdown timeout, guest finished naturally; no force-stop). VM200 stopped.
Same-project cache retained; cleanup deadline 2026-09-25 11:11:02 CST. Reopen legacy
SSH via QGA on resume, do not retrofit env-admin-ssh credentials.


### First-frame exit fix — b49e86cccbc3

Root cause confirmed in real SDL cold-process regression: SDL_RegisterEvents allocated
0x8000, colliding with Moonlight hardcoded SDL_USEREVENT frame-ready/barrier events.
The old internal-stop type branch therefore exited on the first ordinary event.
Fix removes custom event allocation and uses only atomic pending stop, checked within
the existing 20ms SDL loop. No server/source change. Old 1b610d1 package is superseded.
Windows Qt6.8.3 / VS2022 Release: cold SDL slot 3/3 (including init/cleanup), full Qt18/18,
gain and clean-PATH 4s smoke passed; Mac cold3/3/full18/18, Python3/3 passed.
SMB Target RKMoon-Windows-x64-auto-display-exitfix-b49e86cccbc3.zip: 29,320,927 bytes;
full destination readback and sidecar passed. SHA256
 e9b6e076f0d19dfd195076b58798cd8c2568eb2124edc3b77f60298ffc5a6f5d
Still requires user live HDMI playback confirmation; synthetic/queue tests do not
claim actual rendered-stream acceptance.

Exit-fix cleanup: VM9006/VM200 stopped; legacy lease closed, sshd stopped/manual,
controller key deleted. Cleanup deadline 2026-09-25 11:30:41 CST.


## Windows dual mouse modes — c6b7b7db715c (2026-09-24)

Delivered smb://192.168.123.10/zssd/Target/RKMoon-Windows-x64-mouse-modes-c6b7b7db715c.zip
29,324,997 bytes; SHA256 e6fe614dc07c7956764855118253c547290298ae2551a610de4be3e11baa6f53.
Target full readback and sidecar passed. Windows Qt6.8.3 / VS2022 x64 Release,
Qt22/22, cold SDL regression3/3 (init/cleanup included), gain and clean-PATH4s smoke passed.
Absolute(default)/relative persisted GUI selection; absolute capability required,
legacy missing capabilities permit only explicit relative. Both launch/resume send
rkmoonMouseMode before any lease. CtrlAltShiftC changes visibility only. Direct touch
is ALWAYS mouse emulation even when host advertises native touch, avoiding silent no-op.
Aspect-fit mapping rejects black-bar press/down, clamps last video pixel, preserves
active drag release. Tests cover non16:9, HiDPI-equivalent units, source aspect changes.
Actual packaged-client HID position/HiDPI/touch/stream switching not yet live-verified.
Requires new server capability+launch mode support; no server/hardware changes by client agent.

Dual-mode cleanup: VM9006/VM200 stopped, legacy SSH lease closed, sshd stopped/manual,
controller key deleted. Same-project destruction deadline 2026-09-25 12:16:14 CST.


## Windows common wake-fix / unpacked delivery — 0fb281698fd2

Delivered runnable folder: smb://192.168.123.10/zssd/Target/RKMoon-Windows-x64
(local mount /Volumes/ZSSD/Target/RKMoon-Windows-x64; run rkmoon-client.exe).
All 40 payload files, 69,670,665 bytes, were read back and checked by size/SHA256
in staging and again after promotion. delivery-manifest.json SHA256:
82012d23f635f24404b85bac2cbf97ebcf6eff202a3e16c837c977e5b07c2d12.
No new final ZIP is left in Target; parent owns old version ZIP/sidecar cleanup.

Windows snapshot: 0fb281698fd2385854b43e3888ef328c61064df0 (clone only; shared repo uncommitted).
Qt6.8.3 / VS2022 x64 Release built in a new rkmoon-common-fix shadow tree.
Build log contains Connection.c; new Connection.obj and common library were produced
before the final application link. Connection.obj SHA256:
6c9d4d69c10785c6403b09ff7755fb410f05ee09ec6738a301d06bffe0694094.
Pinned common8599b6042a4ba27749b0f94134dd614b4328a9bc transformed Connection.c SHA256:
a291563193baee41984ed10c8ad1b941998fe899a53f28b2634a23f270700650.
Overlay SHA256 b62e7a72809eb97232208ebbf8dc575cda78e1f0109d110577e350a533c0ad16.
The exact post-connect relative wake-jiggle and its waits are removed; strict server
wrong-mode rejection remains unchanged. c6b7b7db715c is superseded for this defect.

Main-window Mouse mode selector persists absolute/relative choice and is disabled
while connecting, polling, streaming, or cleaning up. Settings contains bitrate/codec.
CtrlAltShiftC remains visibility-only. Python4/4, Mac Qt22/22, Windows Qt22/22,
separate cold SDL3/3 (includes init/cleanup), gain and clean-PATH4s smoke passed.
These are source/offline/synthetic/startup results. Real streaming, touch, absolute
position and live mode-follow behavior of this final executable await live acceptance.
No server, T6 or Znas source changes were made by the client agent.

Legacy SSH lease closed, sshd stopped/manual; controller keys and internal transfer
archive deleted. VM9006 and VM200 stopped. Build environment intentionally retained
for same-project reuse; inactivity cleanup deadline 2026-09-25 13:17:43 CST.


## Windows flexible-source delivery — 176733ef3254

Delivered unpacked smb://192.168.123.10/zssd/Target/RKMoon-Windows-x64.
Snapshot176733ef3254efe64ee356fb698711f2f7f30c2b exists only in build clone;
no shared repository commit by client agent. Payload40files/69,678,372bytes, each
size/SHA256 checked from SMB staging and final folder. ManifestSHA256:
999837e143318132d1f07c2b0cd010442090cc8e59cfb8850e4b6fd9a31c9bb0.
ExecutableSHA256 a3cbcfb9a6303f9f4b52241ebc6a6fe5dd51d56f37a9076f840ddc97ad981934.
Windows Qt6.8.3 VS2022 Release, Qt24/24, coldSDL3/3, gain, clean-PATH4s smoke passed.
Mac Qt24/24, Python4/4 and diff-check passed. Final parser upper bound12010 matches
server120Hz clock tolerance; arbitrary even dimensions <=3840x2160, integer requests
round fpsX100, no monitor refresh cap. Server owns capture/pixel-rate budget.
Persistent sanitized diagnostics are now compiled; transient poll tolerance is NOT
claimed as the root-cause fix for the prior reported disconnect. Actual HDMI playback,
custom fractional timing and reconnect of this executable remain unverified.
No T6/Znas operations or server edits. Old stable folder preserved as
.RKMoon-previous-176733ef3254efe64ee356fb698711f2f7f30c2b for parent cleanup after readback.


## Delivered-package source audit — 2026-09-24 (no rebuild)

Current delivered RKMoon-Windows-x64 (176733ef3254, manifest 999837e1…, exe a3cbcfb9…)
IS the latest client. All 22 build-manifest app inputs (incl. rkmoon-app.pro,
rkmoon_display.h, kvmwindow.cpp, rkmoon_session_control.h, rkmoon_diagnostics.h) match
the working tree by SHA256. Overlay regenerated by current apply_moonlight.py from a
fresh pinned checkout (moonlight-qt f786e94c, common-c 8599b604) has SHA256 23ba6825…;
the delivered overlay.patch (d642c3ad…) is byte-identical after CRLF->LF (Windows
write_text newline translation only). Remaining working-tree differences are
tests/client_ui.cpp (already in the Qt24/24 run) and docs, which do not enter the
binary. Target folder re-read from the Mac: 40/40 manifest files size+SHA256 match,
no extra files. VM9006 was not started. Old .RKMoon-previous-176733ef… folder: 20
files (incl. rkmoon-client.exe) still "Resource busy" over SMB, presumably an old
client still running on the user's Windows PC; left in place, user PC not touched.


## Reconnect buffering — Windows delivery 4efcef777571

Client-only change (kvmwindow.cpp/.h, tests/client_ui.cpp); server-side HDMI frame
timeout tolerance is owned by the parent and not duplicated here.
- serverinfo poll during playback: transient transport failures tolerated for 10
  consecutive polls (~10 s); the 10th stops. Auth/identity/protocol errors still stop
  immediately (unchanged fail-closed path).
- Unexpected session end after connectionStarted (not user quit, not auth stop, not
  a confirmed mode-change restart) schedules automatic reconnect at 1/2/4/8/8 s,
  max 5 attempts (~23 s + launch time). Status shows "连接中断，正在重连（n/5）";
  the main button reads "Cancel / Stop waiting" and cancels. connectionStarted
  resets the counter. Each attempt re-polls serverinfo first (password/identity/mode
  revalidated). A transport failure while recovering consumes one attempt; HTTP/protocol
  refusal stops. A first launch that never connected does not auto-loop.
  After 5 failures the follow stops with a retry hint.
- Mode change: a new source mode must be seen on 2 consecutive polls (~2 s) before
  restart; an intermediate different mode resets the debounce.
Diagnostics add fixed labels only (display-change-seen, reconnect-scheduled/attempt/
exhausted, launch-ended-before-connect). Non-ASCII status strings use UTF-16 escapes so
Windows cp1252 source tests read kvmwindow.cpp.

Delivered unpacked smb://192.168.123.10/zssd/Target/RKMoon-Windows-x64 (40 files,
69,682,468 bytes). Build snapshot 4efcef7775717834450facd06a7362fc4f44a551 exists
only in the VM9006 clone; no shared-repo commit. Manifest SHA256
44d4acb997d1c41d1a23a374533157ca662a071835146caa195b340786c112f1; executable SHA256
438974e51735f9c0cb7631431d6ba215e315de724b44334e42cb69cadfc72a89; internal package
d40ff68a…; overlay 23ba6825… (pinned). Every file size/SHA256 read back from SMB staging,
the final folder, and again independently from the Mac; no extra files.
Mac Qt25/25, Python4/4, diff-check; Windows Qt6.8.3/VS2022 Qt25/25, coldSDL3/3, gain,
clean-PATH4s smoke passed. These are offline/synthetic results: real HDMI playback,
real network drop/reconnect timing and live mode-follow of this executable are unverified.
Old .RKMoon-previous-4efcef… (the 176733ef release) removed. .RKMoon-previous-176733ef…
still has 20 files "Resource busy" (old client running on user PC); left in place.
VM9006 stopped, lease cleared, verified delivery recorded; cleanup deadline
2026-09-25 15:53:30 CST.

## Start button / no-signal session — Windows delivery d4fdd3de99d7 (2026-09-25)

Client change (kvmwindow.cpp/.h, tests/client_ui.cpp; uncommitted in the shared repo):
Connect only authenticates and shows continuously refreshed server info, no longer
auto-streams; the former "View HDMI" button is now "Start"; Start stays enabled with no
HDMI signal and then requests 1920x1080@60. Client sources were not edited by the build.

Build clone: VM9003 codex-win-rkmoon-202609252045, project rkkvm-sunmoon-client, from
VM200 snapshot env-admin-ssh. Full and PVE linked clone both refused the EFI snapshot;
the standing-authorized manual ZFS linked path was used (disk-0/1/2 origins
vm-200-disk-N@env-admin-ssh verified). QGA ready; Administrator SSH host key
SHA256:Ai4UDg… matched and was used for file transfer/build. Qt 6.8.3 msvc2022_64 was
installed clone-locally by aqtinstall under D:/Build/work/rkmoon-9003/Qt; VS2022 wrapper.
Build snapshot d4fdd3de99d7bfc6092497ebfde210de7f1ed8e0 exists only inside VM9003
(working-tree kvmwindow.cpp f587988b…, kvmwindow.h 5680892c…, client_ui.cpp 6782ca82…,
identical to the Mac working tree); no shared-repo commit.

Results — OFFLINE/SYNTHETIC ONLY:
- Windows: fetch pinned sources + overlay apply + test_client.py passed; Release link OK
  (common-c Connection.c compiled); QtTest 26/26 (incl.
  connectShowsInfoWithoutStreamingAndStartAcceptsNoSignal), cold SDL 3/3, audio_gain
  assertions, packaged clean-PATH 4 s startup smoke passed.
- Mac (reported by the requester, not rerun here): Qt 26/26, test_client.py 4/4.
- NOT verified: real Connect/serverinfo refresh against the T6, real Start with and
  without HDMI signal, actual 1920x1080@60 no-signal session, playback/input.

Delivery: smb://192.168.123.10/zssd/Target/RKMoon-Windows-x64, 41 files (40 package
files + delivery-manifest.json), 69,694,807 bytes (package payload 69,689,124).
delivery-manifest.json SHA256 71e1d9b35cb654183c2629fb7179f7c86b8f038e4e3132c8baebadf2215df98d;
rkmoon-client.exe 615,424 bytes SHA256 175d0728dee3432cc9b7f18584ef8dff60a5d4ace51a4d1993cc58885e2ccb4c;
internal ZIP 68ddd66a… (not left in Target); overlay d642c3ad… (pinned, CRLF form).
Every file size/SHA256 read back from SMB staging, the final folder, and again
independently from the Mac; no extra files.
Old folder moved to .RKMoon-previous-d4fdd3de99d7… (it holds the prior 4efcef release);
16 files removed, 22 files incl. rkmoon-client.exe/Qt/SDL DLLs still "Resource busy"
over SMB (old client presumably still running on the user PC); left in place, user PC
not touched. The earlier .RKMoon-previous-176733ef… is no longer present.
VM9003 stopped, lease cleared, attempt recorded success/verified; guest intentionally
not cleaned (same-project reuse only). Guarded cleanup timer
codex-win-clone-9003-cleanup.timer deadline 2026-09-26 21:07:25 CST.

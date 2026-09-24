# Client handoff

> 2026-09-24 password authentication over plaintext HTTP: `pair`/PIN, TLS, trust-on-first-
> use and certificate pinning are all gone from this client. It binds an address, a port
> and a password (fixed user name `kvm`, initial server default `kvm`) and talks to one
> plaintext HTTP base port, per docs/ADR-005-http-password.md. **The password and all
> control traffic are readable and modifiable on the wire; this was chosen deliberately
> for a trusted local network.** Local verification below; no device, remote host or live
> KVM endpoint was touched while changing the protocol. The later Windows build used
> only a fresh task-owned clone; real hardware streaming remains untested.

Scope: `client/` only, branch `feat/minimal-moonlight-client`, base `d5f2ba5`.
Original Claude session stopped due quota; inherited skeleton was **not complete**.
Retained files were inspected rather than overwritten. Added main, single-host
binding/credential logic, Widgets frontend, real Session invocation, audio controls,
build recipes and tests. Missing nested ENet and QtSvg/Discord/entrypoint/link issues
were found through actual compilation and fixed. No server changes were merged.

Current critical invariants:

- Require a matching host UUID, the `RKMoonAuth=password-http-v1` marker, the authorized
  `PairStatus=1` answer and an armed in-memory credential before the fixed-HDMI app
  lookup. There is no certificate to check; do not reintroduce a certificate gate.
- The password lives only in `RkmoonAuth` (memory). It is attached to exactly one
  `http://host:port`, never to another host, port or scheme, and never to a URL, a log
  line or QSettings. `prepareRequest` also forces `ManualRedirectPolicy` on every request,
  because Qt would otherwise follow a 3xx and replay the header at whatever host the
  redirect names.
- The overlay points upstream's "HTTPS" base URL at the same plaintext base port and makes
  `NvHTTP::setHttpsPort` a no-op. `NvComputer` still substitutes the upstream default for
  the `HttpsPort=0` the server reports, so never derive a port from `activeHttpsPort`;
  the patched `setHttpsPort` is what keeps every call site on the base port.
- Neither `NvPairingManager` nor `IdentityManager` is compiled. The client generates and
  stores no key or certificate of any kind. Do not reintroduce a `/pair` call: the host
  answers 404, and its failure budget (10 burst, 1/s) is shared across clients.
- Keep Session/NvComputer alive until upstream asynchronous `readyForDeletion`, not
  merely until SDL `exec()` returns. Block new UI actions/close while connecting or cleaning up.
- Relative mouse forced; absolute-toggle disabled in audited overlay. Gamepad stub
  only disables removed controls; no stub video/audio/network path.
- Opus stereo decode and upstream timing/recovery remain active. Gain is applied
  after decode; no microphone uplink. A/V sync and hotplug recovery need real testing.
- Whitelist HTTP operation logging; no request URL query or launch response body.
  Test logs use synthetic sentinels only.
- First exported evaluation ZIP is unsafe for pairing/streaming and is retained only
  as offline build evidence; exact hash + input manifest are in ACCEPTANCE.md.

The HTTP/password Windows x64 Release package has now been built and delivered from
remote-only source snapshot b4367616e16909642e77878abddea64b61798c69.
QtTest 10/10 (including HTTP loopback), gain assertions, clean-PATH offscreen startup
and SMB Target size/hash readback passed. See ACCEPTANCE.md for the URI and hashes.
VM9005 is stopped with its SSH lease closed and a verified 24h destroy-only guard.
Real host login, HDMI video/audio and USB input remain untested.

The older reviewed Windows binary below predates password authentication and contains
the PIN pairing path; use the new HTTP package for subsequent integration.
Reviewed source commit **b9c273b299e0c906a846b63eea854d2f30b5ba4a** is built on
Windows x64 Qt6.8.3/MSVC2022; offline QtTest6/6, gain assertions and packaged4s
clean-PATH offscreen smoke passed. New reviewed ZIP/evidence and exact hashes are
in ACCEPTANCE.md; source inputs map back to that commit. This later documentation
commit does not change the binary's source identity. VM9004 stopped, lease closed,
controller key material removed; final guard deadline **2026-09-24 21:58:01 CST**.

Next: parent-coordinated real host login against the production server, HEVC/H.264 GPU
decoding, HDMI audio A/V sync/source recovery, USB input and disconnect release. Do not start VM200,
borrow VM301 or operate the user's .180 desktop without the integration window.
No live KVM endpoint was used by this client task. Licensing/source-offer gaps remain
a public-release blocker; initial evaluation ZIP remains offline-only evidence.

Known UX limitations for follow-up: connecting runs on the GUI thread inside upstream's
bounded nested event loops (5 s per request), so the window is unresponsive for up to a
few seconds and offers no cancel button; reentry and window
close stay blocked meanwhile. A wrong password must be retyped, and the host's shared failure budget can briefly reject
a correct one; that interaction has not been exercised against the real server. Audio renderer retry remains
upstream (200 packets, so wall-clock retry interval depends on negotiated frame size).
The hardware integration should test a wrong password, an exhausted failure budget and
5/10/20ms audio recovery separately rather than infer them from unit, offscreen or
loopback tests.
2026-09-24 1440p90 follow-up: settings now offers an explicit experimental
2560×1440/90 preset. The default remains 1920×1080/60; KvmConfig passes selected
dimensions/fps unchanged to StreamingPreferences. Added offline Qt coverage for
selection, persistence and request. Mac arm64 Qt 6.11.2 compiled and ran the
offline suite: 12/12 passed, including both new preset tests; this is not Windows
or real-stream validation.
The saved MS-A2 password works when parsed without its Markdown backticks. VM9006
was assembled from VM200 env-completed snapshots, QGA health passed, and an SSH
fallback identity was fingerprint-verified. Before build setup VM200 unexpectedly
became running, so progression stopped; VM9006 was gracefully stopped with deletion
hold. No Windows package was produced. Do not attribute this to invalid credentials.
Later same-project resume after VM200 returned to stopped: VM9006 env-completed origin,
ownership and guard rechecked; QGA and renewed SSH fallback verified. Windows 11 LTSC
VS2022/Qt6.8.3 Release app linked, 12/12 offline Qt tests passed. Clone-local input
commit bac1b3e776b1e1bc9abea6e6c4457ec0ac67ea88; shared tree still uncommitted.
Package: smb://192.168.123.10/zssd/Target/RKMoon-Windows-x64-1440p90-experimental-bac1b3e776b1.zip
(29,320,363 bytes; SHA256 06381939e037dc79b13060f706959ee243b5e639e43d809ec26f3977fe9ef07e),
Target full readback verified. VM9006/200 stopped; 9006 same-project cleanup guard
passed, deadline 2026-09-25 10:08:38 CST. Client live stream remains untested.
Parent-reported T6 V4L2 capture now passed 30 seconds at 2560×1440: 2695 frames,
89.9981 fps by timestamps, 11,059,200 bytes/frame, zero sequence gaps/errors.
T6 real HEVC and H.264 MPP DMA-BUF encode plus independent decode each passed
60 seconds/5394 frames at 89.99825/89.98159 fps, with raw skips 0/1 and P95
dequeue-to-AU 7.025/6.672 ms. Keep 90 fps experimental: full RTP and client
stream/decoder performance remain untested. Planned server opt-in:
RKMOON_ALLOW_1440P90_EXPERIMENT.

## 2026-09-24 automatic display client (local implementation)

Client now requires authenticated serverinfo display contract v1. Missing metadata gives
an explicit server-upgrade error; there is no manual-resolution fallback. Connect, launch
preflight and ~1 s session polling use asynchronous QNetworkAccessManager with bounded
5 s requests and no redirects. Width/height are authoritative server values; FpsX100 is
rounded to the nearest integer for upstream streaming preferences. Differences <=15 in
FpsX100 are ignored to tolerate input clock jitter. Settings expose only bitrate
(1000–35000 kbps) and codec. Old persisted dimensions are removed on save.

A mode change closes the current session, waits for readyForDeletion, then polls again
before reconnecting. No signal/unsupported/unavailable waits without launching. User
quit cancels following; transport termination uses a separate registered SDL event and
atomic pending flag rather than masquerading as user SDL_QUIT. Failed event registration
or a full queue cannot lose an internal stop because the SDL loop also checks the flag.
Unexpected disconnect waits up to three polls for a source transition before ending.
Qt networking is serviced in the SDL loop with user-input events excluded; session and
cleanup guards prevent nested sessions and protect the NvComputer pointer lifetime.

Ctrl+Alt+Shift+C now toggles the local OS cursor while retaining LiSendMouseMoveEvent
relative HID. Showing it uses SDL's non-raw capture path: movement is constrained by
local screen/window edges; hiding it restores raw relative capture where SDL supports
it. This is a local cursor aid, not absolute-position synchronization with the HDMI
source. F9/F10/F11 audio shortcuts are unchanged.

Local Mac arm64 Qt tests: 17 passed (including dynamic HTTP loopback lifecycle simulation,
clock jitter, user/internal quit reasons, no-signal wait, async requests and close guard).
Python source/overlay checks: 3 passed. These are synthetic/offline tests, not live HDMI
or Windows streaming acceptance. Native Windows rebuild is in progress in same-project
VM9006, with deletion hold and controller lease while active.


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


## First-frame exit defect (2026-09-24)

The auto-display 1b610d1 package is defective: SDL_RegisterEvents(1) allocated
SDL_USEREVENT (0x8000), which pinned Moonlight already uses directly for frame-ready
(code 0) and window barrier (code 100). The type-only internal-quit branch consumed
these normal events and exited immediately. Reproduced with real cold SDL event queue;
consistent with first-frame then return-to-GUI, source remaining ready/8999.
Fix removes custom event allocation and dispatch entirely. Atomic stop flag is checked
by the existing 20ms-bounded SDL loop. User SDL_QUIT remains separate; no pending event
survives to poison the next session. Cold standalone regression and full Mac Qt18/18
pass. Native Windows fixed package verification in progress; do not reuse 1b610d1.


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


## Mouse-mode implementation pending server contract agreement

Client-only work adds persisted input/mouseMode (0 absolute default, 1 relative).
Settings offers desktop/direct-touch absolute or relative touch-trackpad. CtrlAltShiftC
only toggles local visibility, never changes mouse/touch mode. Absolute starts with a
visible local cursor. Relative uses existing touch gestures and relative HID deltas.
Pinned input overlays clamp to video last pixel (width-1/height-1), reject black-bar
presses and touch-downs, track accepted touch IDs, and retain releases for active drags.
SDL events/window size share coordinate units; no extra Qt devicePixelRatio multiplier.
HiDPI and non-16:9 synthetic mapping tests use the renderer aspect-fit rounding.

PROPOSED wire contract (not yet independently acknowledged by server agent): authenticated
serverinfo RKMoonMouseModes comma-list relative,absolute; launch and resume each send
rkmoonMouseMode=absolute|relative before input lease construction. Client refuses chosen
mode if not advertised; no inferred mode from first movement, no silent downgrade.
Absolute touch uses upstream native touch only if host advertises LI_FF_PEN_TOUCH_EVENTS;
otherwise directly mapped mouse emulation. Server must not advertise native touch unless
implemented. Mode selection occurs only while idle and applies to the next connection.
No Windows package or hardware mutation in this mouse-mode change yet.

Touch policy correction: dedicated client always emulates absolute mouse for direct
touch, even if upstream host flags claim native pen/touch. Relative touch remains
trackpad gestures. No native touch protocol support is claimed.


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


## Current delivery requirement (supersedes ZIP-only delivery)

Final Windows delivery is the unpacked runnable SMB Target/RKMoon-Windows-x64 folder.
Use a unique staging folder, verify every regular file by size/SHA256 read back from
SMB against a manifest, then promote safely to the stable path. Internal ZIP transfer
is allowed but must not leave a final ZIP artifact in Target. Preserve the previous
stable folder until new staged contents are completely verified. Parent coordinates
old RKMoon client ZIP/sidecar/version cleanup after successful delivery; child must
not independently delete other versions or user files.


## Common-library wake regression / main-window selector

A second disconnect cause was confirmed: pinned common Connection.c unconditionally
sent relative +1,+1/-1,-1 after input stream startup, revoking absolute HID leases.
Overlay now removes this exact block (both 10ms waits included), verifies nested commit
8599b6042a4ba27749b0f94134dd614b4328a9bc and hashes transformed source. Actual nested
file regression rejects any remaining LiSendMouseMoveEvent call; Python4/4 passed.
Main-window Mouse mode selector now sits above Connect; persists immediately and is
locked during connect/poll/stream/cleanup, including programmatic change guard.
Settings again contains only bitrate/codec. Qt22/22 passed locally. Windows snapshot
0fb281698fd2385854b43e3888ef328c61064df0 builds in NEW rkmoon-common-fix root; new
Connection.obj and moonlight-common-c.lib verified; final native results follow below.


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


## Active investigation after 0fb delivery (not a new delivered fix)

User reported playback later returning to GUI and View HDMI retries failing. Parent
observed T6 original stream continuously encoding with no disconnected/HID error;
RTSP tag failures on later requests are not proof the original session stopped.
Client audit: m_streaming guards queued runStream until readyForDeletion; constructor
is side-effect free. Unexpected upstream termination skips quitApp/cancel by design,
and a refreshed nonzero currentGameId selects resume. Single metadata poll error
previously stopped playback; successful polls could overwrite displayed errors.
Unbuilt diagnostic draft in kvmwindow/kvmhost and rkmoon_diagnostics.h retains error
text, records only fixed lifecycle labels/numbers, tolerates two transient transport
failures while streaming (auth/protocol failures still immediate). Python4/4 and diff
check pass; draft has NOT been Qt compiled/tested or packaged and is not root-cause proof.
Current delivered executable remains 0fb281698fd2.

VM9006 reproduction attempt: QGA verified no logged-on interactive user/no explorer,
Microsoft Basic Display Adapter10.0.26100.1 only. Dedicated client forces hardware
decode and checks capability before launch/resume. No real GUI playback launched,
no T6 HTTP session created, no source/server reset; cannot claim reproduction.
Shutdown returned timeout, subsequent status running and QGA unavailable. Next PVE
SSH query rejected saved authentication; retries stopped. Current controller lease
client-live-repro-20260924 and deletion-hold=true remain intentionally in place until
PVE access/status recovered. No SSH fallback opened for this attempt. Parent informed.
Prepared /tmp/rkmoon-live-finish.py may finalize only after authoritative stopped proof;
it marks this attempt cancelled/not-produced and preserves successful prior delivery.


## Flexible source modes and lifecycle diagnostics

Client follows any even source dimensions up to 3840x2160 and fpsX100100..12010 (120Hz clock tolerance),
without an EDID/preset whitelist or local-monitor refresh cap. Server owns the
pixel-rate budget and capture compatibility. Integer upstream request FPS rounds
source fpsX100 (7550 ->76); server must map this back to its actual input timing.
Settings remain bitrate/codec; main-window input mode is unchanged.
Lifecycle diagnostic log uses QStandardPaths AppLocalDataLocation/client-lifecycle.log
(Windows normally %LOCALAPPDATA%/RKMoon/RKMoon HDMI KVM/client-lifecycle.log), with
512KiB rotation to .previous. Only explicit event labels/numeric stages, exit codes,
mode dimensions/rates and state flags are written. No general upstream log capture,
URLs, authorization, passwords, input contents, or session keys.
Two transient transport poll failures are tolerated during active playback; third
failure stops, while authentication/identity/protocol rejection stops immediately.
Error text is retained across successful metadata polls. This improves diagnosis and
transient behavior; it is NOT a confirmed fix for the previously reported disconnect.
Windows delivery/tests for this revision pending until the following evidence entry.


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

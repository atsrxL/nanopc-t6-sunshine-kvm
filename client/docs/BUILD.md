# Reproducible client build

## Fixed sources

From the project root, into a **new independent checkout**:

```sh
python3 client/tools/fetch_client_sources.py
python3 client/tools/apply_moonlight.py vendor/moonlight-qt --patch-output /tmp/client.patch
python3 client/tools/apply_moonlight.py vendor/moonlight-qt --apply
python3 client/tools/test_client.py
```

`fetch_client_sources.py --destination` and `RKMOON_MOONLIGHT_DIR` (read by
`test_client.py`) place that checkout elsewhere; the pinned commit and submodule
assertions still decide whether it is acceptable. `app/rkmoon.qrc` references the font
through the default `vendor/moonlight-qt` layout, so the resource step needs that path
to exist even when `MOONLIGHT_DIR` points somewhere else.

The nested common-c/enet revision is explicitly pinned (the inherited skeleton
omitted it). Existing source directories are never reset. Overlay verification can
read an exactly patched checkout; `--apply` requires all original source files.
Unknown edits and duplicate/missing anchors stop the operation. Preserve earlier
build checkout/logs instead of replacing them to apply a new overlay revision.

The overlay removes QML preferences binding, controller initialization, Discord and
SVG icon dependencies; adds post-Opus attenuation; disables absolute-mouse toggle;
removes HTTP query/body logging that otherwise includes launch encryption data; removes
the upstream client-certificate installation; adds one call in `NvHTTP::openConnection`
that pins redirects to manual and attaches the password credential to the bound host
only; and points the upstream "HTTPS" base URL at the same plaintext HTTP base port, so
`applist`, `appasset`, `launch`, `resume` and `cancel` cannot land on a TLS port even
though `NvComputer` still substitutes a default for the `HttpsPort=0` the server reports.
RTSP/RTP/FEC, video decoder, input packet encryption and the audio renderer/recovery
implementation remain upstream. Two upstream units are not patched out but simply not
listed in the app project, so they are never compiled: `nvpairingmanager` (this server
has no `/pair` route) and `identitymanager` (no TLS listener asks for a client
certificate, so no client identity key is generated or stored). `nogamepad.cpp` is a
deliberate link adapter for **removed** controls, not simulated transport/video/audio.

## Windows x64

Current HTTP/password package: VM9005, Qt **6.8.3 msvc2022_64**, VS2022 v143,
Windows 11 LTSC x64, input commit b4367616e16909642e77878abddea64b61798c69.
The 10/10 QtTest suite, audio gain assertions and packaged clean-PATH offscreen
startup passed; Target readback is recorded in ACCEPTANCE.md.

Tested initial toolchain: Qt **6.8.3 msvc2022_64**, VS2022 **v143 14.44.35207**, Win11
LTSC x64. Use the existing build-node environment wrapper and task-local Qt if needed;
do not install toolchains on the Mac or modify VM200. Scripts assume the default
`vendor/moonlight-qt` layout (the font resource uses this path).

After exact source checkout, fetch and apply the current overlay above. Run inside
VS2022 x64 environment:

```powershell
pwsh -File client/tools/build_windows.ps1 -QtRoot <Qt/6.8.3/msvc2022_64> -BuildRoot <new-work-parent/client-build>
pwsh -File client/tools/package_windows.ps1 -QtRoot <Qt/6.8.3/msvc2022_64> -BuildRoot <new-work-parent/client-build> -OutputDirectory <new-artifact-directory>
```

Qmake mirrors upstream subprojects into `../vendor/moonlight-qt` beside the build
root. Use a **fresh parent work directory**, not merely a differently named build
subdirectory under an old parent, to avoid mixing dependency makefiles.

Scripts require committed client sources and preserve revision, file SHA256, overlay
hash and binary hash in the build manifest. Package names contain the commit prefix;
no existing package is overwritten. MSVC CRT DLLs and required Qt plugins are included.
This is internal packaging, not a complete public redistribution/source-license bundle.

Offline GUI tests (same VS environment; no network or GPU required):

```powershell
mkdir <new-test-directory>
cd <new-test-directory>
<Qt>/bin/qmake.exe <project>/client/app/rkmoon-app.pro CONFIG+=release CONFIG+=rkmoon_tests DEPS_OUT=<new-work-parent/vendor/moonlight-qt>
nmake
$env:QT_QPA_PLATFORM='offscreen'
# Qt/lib DLLs must be on PATH and the Qt offscreen plugin discoverable.
.\release\rkmoon-client-tests.exe
```

These exercise synthetic sensitive log fields, left/right/mixed modifiers, held-key
releases, relative/hardware/audio policy, connect/cleanup UI reentry guards, and the
password credential: target/scheme/port scoping, UTF-8 base64 encoding, the manual
redirect policy and the absence of any persisted password. They do not establish a
GameStream session. `tests/audio_gain.cpp` separately tests real signed-16/float
attenuation using the production implementation (assertions enabled).

`loopbackAuthenticateAndReject` additionally drives `KvmHost` against plain HTTP servers
started inside the test process on 127.0.0.1: the authorized `serverinfo` and `applist`
exchange, the exact `Authorization` header on every request, proof that the upstream
"HTTPS" call sites now reach the single base port, a rejected wrong password, and refusal
of a host that authorizes but does not advertise `RKMoonAuth`. It needs no key material
and no configuration, so it always runs. A loopback pass is evidence about this client's
logic only, not a host interoperability pass.

## Mac lightweight validation

Only already-installed qmake/clang and existing upstream libraries were used. No
Mac toolchain was installed. `qmake client/rkmoon-client.pro CONFIG+=release; make -j4`
from a new shadow directory builds the application. For tests, use the app project
with `CONFIG+=rkmoon_tests`, and pass its dependency output via `DEPS_OUT` as above.
For an unbundled test, set `DYLD_LIBRARY_PATH` and `DYLD_FRAMEWORK_PATH` to upstream
`libs/mac/lib` and `libs/mac/Frameworks`, and `QT_QPA_PLATFORM=offscreen`.

Windows execution, native Mac linking, offline tests and live hardware acceptance
are separate gates. Successful building must not advance the hardware gates.


Automatic-display builds use unique rkmoon_main.cpp to avoid NMAKE selecting upstream
main.cpp. Native Qt tests require Qt6Test.dll on PATH and QT_PLUGIN_PATH=QtRoot/plugins,
QT_QPA_PLATFORM=offscreen. QtTest -o output must be one string ending in ,txt; avoid
PowerShell comma-array syntax. Package smoke uses its bundled qwindows plugin.


## Current delivery requirement (supersedes ZIP-only delivery)

Final Windows delivery is the unpacked runnable SMB Target/RKMoon-Windows-x64 folder.
Use a unique staging folder, verify every regular file by size/SHA256 read back from
SMB against a manifest, then promote safely to the stable path. Internal ZIP transfer
is allowed but must not leave a final ZIP artifact in Target. Preserve the previous
stable folder until new staged contents are completely verified. Parent coordinates
old RKMoon client ZIP/sidecar/version cleanup after successful delivery; child must
not independently delete other versions or user files.

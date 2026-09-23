# Reproducible client build

## Fixed sources

From the project root, into a **new independent checkout**:

```sh
python3 client/tools/fetch_client_sources.py
python3 client/tools/apply_moonlight.py vendor/moonlight-qt --patch-output /tmp/client.patch
python3 client/tools/apply_moonlight.py vendor/moonlight-qt --apply
python3 client/tools/test_client.py
```

The nested common-c/enet revision is explicitly pinned (the inherited skeleton
omitted it). Existing source directories are never reset. Overlay verification can
read an exactly patched checkout; `--apply` requires all original source files.
Unknown edits and duplicate/missing anchors stop the operation. Preserve earlier
build checkout/logs instead of replacing them to apply a new overlay revision.

The overlay removes QML preferences binding, controller initialization, Discord and
SVG icon dependencies; adds post-Opus attenuation; disables absolute-mouse toggle;
and removes HTTP query/body logging that otherwise includes launch encryption data.
Pairing crypto, RTSP/RTP/FEC, video decoder, input packet encryption, and audio
renderer/recovery implementation remain upstream. `nogamepad.cpp` is a deliberate
link adapter for **removed** controls, not simulated transport/video/audio.

## Windows x64

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
releases, relative/hardware/audio policy and pairing/cleanup UI reentry guards. They
do not establish a GameStream session. `tests/audio_gain.cpp` separately tests real
signed-16/float attenuation using the production implementation (assertions enabled).

## Mac lightweight validation

Only already-installed qmake/clang and existing upstream libraries were used. No
Mac toolchain was installed. `qmake client/rkmoon-client.pro CONFIG+=release; make -j4`
from a new shadow directory builds the application. For tests, use the app project
with `CONFIG+=rkmoon_tests`, and pass its dependency output via `DEPS_OUT` as above.
For an unbundled test, set `DYLD_LIBRARY_PATH` and `DYLD_FRAMEWORK_PATH` to upstream
`libs/mac/lib` and `libs/mac/Frameworks`, and `QT_QPA_PLATFORM=offscreen`.

Windows execution, native Mac linking, offline tests and live hardware acceptance
are separate gates. Successful building must not advance the hardware gates.

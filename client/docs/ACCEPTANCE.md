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
| Reviewed-source Windows build / tests / new package | NOT TESTED yet | next step: exact committed revision, same-task VM9004 resume |
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

Initial final cleanup deadline: **2026-09-24 21:32:29 CST**,
`codex-win-clone-9004-cleanup.timer/service`; clone-specific guard `--check` passed.
Any same-build resume must first lock, set active lease/hold, refresh heartbeat/deadline
and revalidate metadata/origins/competing cleanup. Record the renewed final deadline in
the reviewed-build result; never treat this old deadline as current after resume.

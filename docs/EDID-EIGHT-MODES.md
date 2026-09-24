# Eight-mode HDMI input test — 2026-09-24

User-selected preferred mode: 1920×1080 120Hz. Candidate uses five CTA VICs
and three traditional detailed timings; no Type X dependency. 256 bytes,
39 unused CTA bytes. Binary SHA256:
`99f8da378bf0fc3858496ca8e24e924ceb6be1258d21c364cfe26b75f463ea72`.

Installed temporarily through V4L2 S_EDID on T6 and read back byte-for-byte.
Original backed up on T6 at
`/root/agent.backup/rkmoon-edid-eight-20260924/original.bin`.
No boot-time persistence installed.

Source: Znas Tiny11_clone, passed-through NVIDIA RTX 5060 Ti, driver
32.0.16.1692, direct HDMI to T6. Windows enumerated all requested modes;
ChangeDisplaySettingsEx test/apply returned zero for each. No NVAPI
custom-mode creation needed. HDMI receiver QUERY_DV_TIMINGS samples:

| Mode | Measured Hz | Input lock |
|---|---:|---|
| 1920×1080 30 | 29.9992 | Pass |
| 1920×1080 60 | 59.9968 | Pass |
| 1920×1080 120 | 119.9935 | Pass |
| 1920×1280 90 | 89.9600 | Pass |
| 2560×1440 120 | 119.9904 | Pass |
| 2560×1600 120 | 119.9554 | Pass |
| 3840×2160 30 | 29.9980 | Pass |
| 3840×2160 60 | 59.9964 | Pass |

Each requested mode has repeated receiver samples. Mode windows were about
15 seconds; 1080p30 was resampled in a separate 25-second window. Transient
ENOLCK while switching is not counted as stable lock. Windows WMI listed only
the base preferred timing, so it was not used as the complete mode inventory.
An inherited literal in the initial test log says mode=2560x1440x90; requested
JSON fields and receiver samples, not that stale label, identify each mode.

Scope: signal detection only, not complete frame DMA, MPP encoding, long-run
stability, HDMI audio, color-format verification, or client presentation.
Other source devices and external EDID conformance validation remain untested.

After tests the source was set to listed 1080p60 for compatibility with the
current stream admission rules. EDID preferred mode remains 1080p120.
The test scheduled task is removed; private evidence is under
`private/results/local/edid-fit/`.

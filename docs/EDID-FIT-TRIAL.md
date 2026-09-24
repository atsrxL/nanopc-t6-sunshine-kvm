# 17-mode EDID capacity trial

Offline only: no device write and no claim of source recognition or stream support.

`tools/edid_fit_trial.py` packed the exact 17 requested width/height/refresh tuples
in a 256-byte EDID using one CTA extension. Five standard modes use VICs
34, 16, 63, 95 and 97. Twelve remaining modes use CTA extended tag 0x2a,
DisplayID Type X formula-based timings (six bytes each, CVT reduced blanking v1),
in three blocks of four modes.

CTA data budget: 123 bytes. Video Data Block: 6 bytes; audio, speaker and
HDMI vendor blocks: 24 bytes; three Type X blocks: 81 bytes. Total 111 bytes,
12 bytes unused. Base block preserves the input EDID's preferred detailed timing;
this may duplicate or add a mode outside the requested list.

Generated binary size, both EDID checksums, block walk, and exact 17-mode
encode/decode round trip passed. Encoding was checked against v4l-utils
`utils/edid-decode/parse-cta-block.cpp` (`cta_displayid_type_10`) and
`parse-displayid-block.cpp` (`parse_displayid_type_10_timing`). The full external
edid-decode executable has not been run; this is not a full EDID conformance result.

Type X is a newer timing representation and may be ignored by older drivers or
TV boxes. The trial's HDMI/audio capability fields are not validated device
capabilities. Do not install the trial as a production EDID. Current service
admission still accepts approximately 60Hz plus gated 2560x1440 approximately 90Hz.

Private trial artifact: `private/results/local/edid-fit/rkmoon-edid-fit.bin`.

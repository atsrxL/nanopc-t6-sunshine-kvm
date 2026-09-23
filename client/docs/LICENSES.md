# Licensing / redistribution gate

**Internal evaluation only. Public-release redistribution is NOT cleared.**
Our frontend/overlay code is GPL-3.0-or-later; the combined Moonlight derivative is
distributed subject to the pinned upstream GPLv3 terms. No proprietary exception is
assumed. `client/COPYING` retains the upstream GPLv3 text. Source pins are real git
revisions, not mutable tags; complete source/patches must accompany public delivery.

Checked source-tree licenses:

| Component | Pin / license evidence |
|---|---|
| Moonlight Qt v6.1.0 | f786e94c7b2f943e24e65d7d74deb539b827fc84; root LICENSE GPLv3 |
| moonlight-common-c | 8599b6042a4ba27749b0f94134dd614b4328a9bc; LICENSE.txt GPLv3 |
| nested ENet fork | bbf71856bb144729af4ed08af6bc2f5826a96db5; MIT notice in client/licenses |
| h264bitstream | 34f3c58afa3c47b6cf0a49308a68cbf89c5e0bff; LGPL-2.1 text in client/licenses |
| libsoundio | 34bbab80bd4034ba5080921b6ba6d61314126310; MIT notice in client/licenses |
| upstream prebuilt dependency bundle | a27d6a7995ef504963fa9058c69e6ba1b449cc0f; libs/README.md refers to c gutman/moonlight-deps build repository and individual upstream licenses |

Before public release, still required:

- Determine exact build/config/source revision and full notices for the bundled
  FFmpeg, SDL2/SDL2_ttf (including font dependencies), OpenSSL, Opus, dav1d and any
  transitive dependency. A commit of **prebuilt binaries** is not proof of matching
  complete corresponding source or compiler options. Do not infer FFmpeg license
  configuration from the DLL filename. No public source-offer fulfillment is claimed.
- Include Qt 6.8.3 notices and corresponding source/build information under the
  selected LGPL/GPL terms, preserving dynamic-library replacement and required rights.
- Audit the embedded upstream ModeSeven font's redistribution notice and provenance.
- Include/comply with the actual MSVC CRT and Windows SDK redistributable terms.
- Supply the exact client commit, overlay generator and generated patch, lockfile,
  dependency source/build material and packaging recipes matching each shipped binary.
- Complete attribution/UI notice review and license compatibility review, including
  Windows AntiHooking sources included from the pinned Moonlight tree.

No public release/tag or installer has been published. SMB Target packages are for
the user's isolated evaluation and retain explicit unverified boundaries.

# Third-party components

This repository is a fork of Kirikiroid2Yuri (https://github.com/YuriSizuku/Kirikiroid2Yuri),
itself built on the Kirikiri2/TVP2 engine. `LICENSE` at the repository root is
kept unmodified and verified byte-identical to Kirikiroid2Yuri's own
`LICENSE` (a modified-BSD-style licence covering W.Dee and the Kirikiri Z
Project contributors). `enginehost/LICENSES.md` documents the notice for the
bundled Noto Sans CJK JP font that ships in the built bundle; this file
extends that index to the rest of `thirdparty/`.

| Component | Licence | Source | Where in tree |
|---|---|---|---|
| Kirikiroid2Yuri / Kirikiri2 (TVP2) | modified BSD-style (see `LICENSE`) | https://github.com/YuriSizuku/Kirikiroid2Yuri | entire tree (fork) |
| SQLite3 (amalgamation) | public domain | https://www.sqlite.org | `thirdparty/sqlite3` |
| Oniguruma | BSD-2-Clause | https://github.com/kkos/oniguruma | `thirdparty/oniguruma` |
| Opus | BSD-3-Clause | https://opus-codec.org | `thirdparty/opus` |
| cocos2d-x | MIT | https://github.com/cocos2d/cocos2d-x | `thirdparty/cocos2d-x` |
| p7zip | LGPL-2.1 / BSD mix (per-file) | https://sourceforge.net/projects/p7zip | `thirdparty/p7zip` |
| FFmpeg | LGPL or GPL depending on build configuration | https://ffmpeg.org | `thirdparty/ffmpeg` |
| Noto Sans CJK JP | SIL Open Font License 1.1 | see `enginehost/LICENSES.md` | fetched at build time, not committed |

None of `cocos2d-x`, `ffmpeg`, `oniguruma`, `opus`, `p7zip`, or `sqlite3`
carry their own bundled licence file in this tree; the licences above are the
well-known published licences for each named upstream project and are cited
here rather than copied, since no upstream licence text for them exists in
the repository to preserve verbatim.

## Obligations (flagged, not resolved here)

FFmpeg is GPL when built with `--enable-gpl` (for components like x264) and
LGPL otherwise; which applies depends on `thirdparty/ffmpeg`'s actual build
configuration, which this pass did not audit. If the build enables GPL
components, source-offer obligations attach to the whole binary. This is
worth a follow-up pass against the actual FFmpeg build flags used in CI.

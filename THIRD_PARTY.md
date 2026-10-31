# Third-party components

This repository is a fork of Kirikiroid2Yuri (https://github.com/YuriSizuku/Kirikiroid2Yuri),
itself built on the Kirikiri2/TVP2 engine. `LICENSE.kirikiri` is Kirikiroid2Yuri's own `LICENSE`, kept unmodified (byte-identical;
the root `LICENSE` is now the GPL-3.0 text of the combined work) (a modified-BSD-style licence covering W.Dee and the Kirikiri Z
Project contributors). `enginehost/LICENSES.md` documents the notice for the
bundled Noto Sans CJK JP font that ships in the built bundle; this file
extends that index to the rest of `thirdparty/`.

| Component | Licence | Source | Where in tree |
|---|---|---|---|
| Kirikiroid2Yuri / Kirikiri2 (TVP2) | modified BSD-style (see `LICENSE.kirikiri`) | https://github.com/YuriSizuku/Kirikiroid2Yuri | entire tree (fork) |
| SQLite3 (amalgamation) | public domain | https://www.sqlite.org | `thirdparty/sqlite3` |
| Oniguruma | BSD-2-Clause | https://github.com/kkos/oniguruma | `thirdparty/oniguruma` |
| Opus | BSD-3-Clause | https://opus-codec.org | `thirdparty/opus` |
| cocos2d-x | MIT | https://github.com/cocos2d/cocos2d-x | `thirdparty/cocos2d-x` |
| p7zip | LGPL-2.1 / BSD mix (per-file) | https://sourceforge.net/projects/p7zip | `thirdparty/p7zip` |
| FFmpeg 3.3-git (zeas2/ffmpeg 896ab301) | LGPL-2.1-or-later (built without --enable-gpl/--enable-nonfree; audited 2026-10-14) | https://github.com/zeas2/ffmpeg | `thirdparty/port/ffmpeg` (fetched at build time) |
| Noto Sans CJK JP | SIL Open Font License 1.1 | see `enginehost/LICENSES.md` | fetched at build time, not committed |

None of `cocos2d-x`, `ffmpeg`, `oniguruma`, `opus`, `p7zip`, or `sqlite3`
carry their own bundled licence file in this tree; the licences above are the
well-known published licences for each named upstream project and are cited
here rather than copied, since no upstream licence text for them exists in
the repository to preserve verbatim.

## Declared bundle licence

`enginehost/bundle-metadata.json` declares `GPL-3.0` for the combined bundle. Kirikiri's licence is a BSD-3-Clause
style licence (keep the notice with source, carry it in documentation with binaries, no endorsement without
permission), which GPL-3.0 can include; `LICENSE.kirikiri` is kept for that notice. The bundled components above keep
their own licences.

## Obligations

FFmpeg would be GPL if it were built with `--enable-gpl` and unredistributable with
`--enable-nonfree`. Neither is the case for any ABI (audit 2026-10-14, recorded in `NOTICE` and
`enginehost/LICENSES.md`): the arm64-v8a tree is upstream's published build, configured with exactly
the line `script/_android.sh` uses for the x86 and x86_64 trees built here, and every FFmpeg library
reports "LGPL version 2.1 or later". The build workflow checks that string for every ABI and fails
otherwise, so a changed upstream tree cannot slip a GPL-only or non-free FFmpeg into the bundle. The
bundle carries FFmpeg's LGPL-2.1 text (`LICENSE.ffmpeg.txt`); the source is zeas2/ffmpeg at
896ab301a75f47aca086936c033f10afc2f9e6d8.

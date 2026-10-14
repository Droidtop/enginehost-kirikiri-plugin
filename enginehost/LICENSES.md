# KiriKiri2 engine bundle notices

This bundle is built from the Droidtop Kirikiroid2Yuri fork. The engine and
its included components retain their upstream copyright notices and licenses.
The repository's `LICENSE.kirikiri`, `readme.md`, and dependency sources are
authoritative. The combined work is distributed under GPL-3.0 (`LICENSE`); the upstream
notices are kept.

The acceptance game's files and any title-specific decryption material are
not included in this bundle.

## Noto Sans CJK JP (bundled font)

The bundle ships one font, so that a Japanese game has a Japanese face to draw
with on a console and an English patch has proper Latin glyphs. It is the
default face the engine picks when the player has not chosen one of their own,
and the Windows families these games ask for (MS Gothic, MS PGothic, MS
PMincho, Meiryo, Tahoma and the rest of that set) are mapped onto it.

- File in the bundle: `assets/NotoSansCJKjp-Regular.otf`, family "Noto Sans CJK JP".
- Source: https://raw.githubusercontent.com/notofonts/noto-cjk/Sans2.004/Sans/OTF/Japanese/NotoSansCJKjp-Regular.otf
  (the Noto CJK "Sans2.004" release, Noto Sans CJK Version 2.004; the same
  bytes as the file inside that release's `06_NotoSansCJKjp.zip` asset).
- sha256: `68a3fc98800b2a27b371f2fb79991daf3633bd89309d4ffaa6946fd587f375b5`
- Licence: SIL Open Font License, Version 1.1. The licence text ships beside
  the font as `assets/NotoSansCJKjp-LICENSE.txt`, taken from
  https://raw.githubusercontent.com/notofonts/noto-cjk/Sans2.004/LICENSE
  (sha256 `6a73f9541c2de74158c0e7cf6b0a58ef774f5a780bf191f2d7ec9cc53efe2bf2`).
- The font's own name table records `© 2014-2021 Adobe (http://www.adobe.com/).`
  and `Noto is a trademark of Google Inc.`; version `2.004`.

Both files are fetched and hash-checked by the build workflow rather than
committed here; the workflow step carries the same URLs and hashes.

## FFmpeg (linked into the engine library)

- Version: FFmpeg 3.3-git, https://github.com/zeas2/ffmpeg commit
  `896ab301a75f47aca086936c033f10afc2f9e6d8` (the source upstream Kirikiroid2Yuri builds; also inside
  its `deps` release asset `thirdparty_port.tar.gz`, sha256
  `ba188338d8ca4e9b73854523fe39febca276b5cad2b89227010853ca88119d8a`).
- Configuration, every ABI: `--enable-pic --disable-asm --enable-static --enable-shared --enable-small
  --enable-swscale --disable-ffmpeg --disable-ffplay --disable-ffprobe --disable-avdevice
  --disable-programs --disable-doc --enable-stripping` (cross-compile flags aside). No `--enable-gpl`,
  `--enable-version3`, `--enable-nonfree` or external codec library. arm64-v8a is upstream's published
  tree (`thirdparty_build.tar.gz`, sha256
  `99edcfefc9685b14fe8a7cc2a63aa90be2d4f4652ae6843cd189490eb49f9729`), whose `libavutil.a` records
  this configure line; x86 and x86_64 are built by this repository's CI with `script/_android.sh`.
- Licence: GNU Lesser General Public License 2.1 or later; each library reports
  "LGPL version 2.1 or later" and CI fails the build otherwise. The text ships as
  `LICENSE.ffmpeg.txt` (FFmpeg's own `COPYING.LGPLv2.1`). The libraries are linked statically into
  the engine library; the work's complete source (this repository and the FFmpeg source above) is
  published, which is what lets a recipient relink against a modified FFmpeg.

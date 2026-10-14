# KiriKiri2 engine bundle notices

This bundle is built from the Droidtop Kirikiroid2Yuri fork. The engine and
its included components retain their upstream copyright notices and licenses.
The repository's `LICENSE`, `readme.md`, and dependency sources are
authoritative. Enginehost-specific integration changes do not relicense the
upstream work.

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

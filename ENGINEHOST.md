# enginehost KiriKiri2 plugin

This Kirikiroid2Yuri fork is the complete Android plugin repository. There is
no wrapper application. The exported engine activity receives enginehost's
resolved game directory and starts that directory in place through the same
native startup seam used by the engine's other platforms.

`plugin-core` contains the portable enginehost changeset and Android packaging.
Compatibility branches apply that changeset to their selected engine-fork
revision. The first line explicitly advertises KiriKiri2 2.31.2009.825, needed
by *My Girlfriend is the President*, plus 2.32.0. Declared compatibility does
not imply that every title-specific XP3 filter is implemented.

Kirikiroid2Yuri and its upstream components retain their existing licenses and
copyright notices. CI restores the dependency archives published by upstream's
`deps` release rather than checking generated third-party builds into Git.

The `cxdec-osana` archive profile required by the acceptance game remains a
clean-room compatibility task. Research-only implementations with missing or
nonstandard licenses must not be copied into this BSD-3-Clause-distributed fork.

## Links the game opens are offered by Enginehost (2026-10-24)

`System.shellExecute` of an http, https or mailto target (a homepage or credits link), and the
engine's own patch-library button, go to `TVPOfferLink` (engine/stable, AndroidUtils.cpp). That
calls `KR2Activity.offerLink`, which hands the link to Enginehost's
`dev.enginehost.ActivityLinks.offer` by reflection, since this core runs its own Activity and has
no `EngineHost`. Enginehost checks the link, names it, and offers to open or copy it (Enginehost
docs/SPEC.md, "Links a game opens"). Before this, `shellExecute` did nothing on Android and
answered true. Nothing else is executed, as before.

## 32-bit engines only on request, never published (2026-10-30)

Official KiriKiri cores ship arm64-v8a and x86_64 only (Droidtop/tracker#646). For someone who needs a
32-bit engine, the owner kept a way to build one: dispatch `android-plugin.yml` on the line with
`include_32_bit` on. CI then also builds the `androidx32` dependency tree, checks its FFmpeg licence
with the others, and gradle adds x86 (`-PPROP_APP_ABI=arm64-v8a:x86_64:x86`). The build ends as the
unsigned bundle inputs artifact and is never signed or published. Enginehost's signing gate refuses
32-bit libraries in any case. armeabi-v7a is not built: upstream's armeabi-v7a dependency tree has no
FFmpeg.

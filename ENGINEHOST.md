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
nonstandard licenses must not be copied into this GPL-distributed fork.

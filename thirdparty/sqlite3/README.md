# SQLite amalgamation

Unmodified upstream source, used by `src/plugins/sqlite3plugin.cpp` to provide
the `Sqlite` / `SqliteStatement` classes that `sqlite3.dll` gives KiriKiri
scripts.

    version:  3.53.4 (sqlite-amalgamation-3530400.zip)
    from:     https://sqlite.org/2026/sqlite-amalgamation-3530400.zip
    sha3-256: 628a44cfe82c66aed1ccbbe85a562d2e33ebe64b3288981ed76285612227934e
    files:    sqlite3.c, sqlite3.h, sqlite3ext.h (shell.c is not used)

SQLite is in the public domain.

To update: download the new amalgamation, verify the SHA3-256 published on
sqlite.org/download.html, and replace the three files. Nothing here is
patched, so an update is a straight copy.

# SQLite, built as its own C static library.
#
# It cannot join the krkr2plugin glob: that target compiles everything with
# -std=c++14, which clang rejects for a .c file.
cmake_minimum_required(VERSION 3.6)

add_library(krkr2sqlite3 STATIC ${CMAKE_CURRENT_SOURCE_DIR}/sqlite3.c)

target_include_directories(krkr2sqlite3 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

set_target_properties(krkr2sqlite3 PROPERTIES
    C_STANDARD 11
    POSITION_INDEPENDENT_CODE ON
)

target_compile_definitions(krkr2sqlite3 PRIVATE
    # Databases inside an XP3 archive are read through a KiriKiri VFS
    # (src/plugins/sqlite3plugin.cpp) that has no writable side, and Android
    # gives a game no writable /tmp at all. Keeping every temporary b-tree --
    # the scene queries UNION and ORDER BY, so there are some -- in memory
    # means SQLite never asks the VFS for a scratch file.
    SQLITE_TEMP_STORE=3
    # Nothing loads a SQLite extension here, and the entry point would be an
    # arbitrary dlopen driven by SQL text.
    SQLITE_OMIT_LOAD_EXTENSION=1
    SQLITE_DEFAULT_MEMSTATUS=0
    SQLITE_DEFAULT_FOREIGN_KEYS=0
)

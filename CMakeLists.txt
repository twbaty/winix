cmake_minimum_required(VERSION 3.20)
project(Winix C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 17)

# Default install location — override with cmake --install build --prefix <path>
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "C:/Winix" CACHE PATH "Winix install root" FORCE)
endif()

include_directories(src/include src/common src/shell)

# ------------------------------------------------------------
# winixcommon (shared C library)
# ------------------------------------------------------------
add_library(winixcommon STATIC
    src/common/argparser.c
    src/common/fileops.c
    src/common/nowildcard.c
)

# ------------------------------------------------------------
# Winix Shell (fully wired — FIXED)
# ------------------------------------------------------------
add_executable(winix
    src/shell/main.cpp
    src/shell/aliases.cpp
    src/shell/completion.cpp
    src/shell/line_editor.cpp
)
target_link_libraries(winix winixcommon)
target_link_options(winix PRIVATE -static-libgcc -static-libstdc++
    -Wl,-Bstatic -lpthread -Wl,-Bdynamic)

# ------------------------------------------------------------
# Coreutils (your existing config preserved)
# ------------------------------------------------------------
add_executable(echo src/coreutils/echo.c)
target_link_libraries(echo winixcommon)

add_executable(pwd src/coreutils/pwd.c)
target_link_libraries(pwd winixcommon)

add_executable(ls src/coreutils/ls.c)
target_link_libraries(ls winixcommon)

add_executable(cat src/coreutils/cat.c)
target_link_libraries(cat winixcommon)

add_executable(mv src/coreutils/mv.c)
target_link_libraries(mv winixcommon)

add_executable(rm src/coreutils/rm.c)
target_link_libraries(rm winixcommon)

add_executable(mkdir src/coreutils/mkdir.c)
target_link_libraries(mkdir winixcommon)

add_executable(rmdir src/coreutils/rmdir.c)
target_link_libraries(rmdir winixcommon)

add_executable(touch src/coreutils/touch.c)
target_link_libraries(touch winixcommon)

add_executable(head src/coreutils/head.c)
target_link_libraries(head winixcommon)

add_executable(tail src/coreutils/tail.c)
target_link_libraries(tail winixcommon)

add_executable(date src/coreutils/date.c)
target_link_libraries(date winixcommon)

add_executable(whoami src/coreutils/whoami.c)
target_link_libraries(whoami winixcommon)

add_executable(sleep src/coreutils/sleep.c)
target_link_libraries(sleep winixcommon)

add_executable(cp src/coreutils/cp.c)
target_link_libraries(cp winixcommon)

add_executable(chmod src/coreutils/chmod.c)
target_link_libraries(chmod winixcommon)

add_executable(chown src/coreutils/chown.c)
target_link_libraries(chown winixcommon advapi32)

add_executable(more src/coreutils/more.c)
target_link_libraries(more winixcommon)

add_executable(less src/coreutils/less.c)
target_link_libraries(less PRIVATE winixcommon)

add_executable(nix src/coreutils/nix.c)
target_link_libraries(nix winixcommon)

# Tier 1–4 coreutils
foreach(cmd stat wc du df sort uniq grep tee alias export history kill uname uptime env ver cut tr tac rev nl seq yes hostname paste comm base64 shuf)
    add_executable(${cmd} src/coreutils/${cmd}.c)
    target_link_libraries(${cmd} winixcommon)
endforeach()

# test — CMake reserves the name "test" as a built-in target, so we name the
# CMake target "test_cmd" and set the output binary name to "test".
add_executable(test_cmd src/coreutils/test.c)
set_target_properties(test_cmd PROPERTIES OUTPUT_NAME "test")
target_link_libraries(test_cmd winixcommon)

# [ — second binary from the same source; "[.exe" is valid on Windows.
add_executable(lbracket src/coreutils/test.c)
set_target_properties(lbracket PROPERTIES OUTPUT_NAME "[")
target_link_libraries(lbracket winixcommon)

add_executable(id src/coreutils/id.c)
target_link_libraries(id winixcommon advapi32)

add_executable(timeout src/coreutils/timeout.c)
target_link_libraries(timeout winixcommon)

add_executable(ln src/coreutils/ln.c)
target_link_libraries(ln winixcommon)

add_executable(ps src/coreutils/ps.c)
target_link_libraries(ps winixcommon psapi)

add_executable(printf src/coreutils/printf.c)
add_executable(basename src/coreutils/basename.c)
add_executable(dirname src/coreutils/dirname.c)
add_executable(which src/coreutils/which.c)
add_executable(true src/coreutils/true.c)
add_executable(false src/coreutils/false.c)
add_executable(clear src/coreutils/clear.c)

add_executable(find src/coreutils/find.c)
target_link_libraries(find winixcommon)

add_executable(diff src/coreutils/diff.c)
target_link_libraries(diff winixcommon)

add_executable(sed src/coreutils/sed.c)
target_link_libraries(sed winixcommon)

add_executable(xargs src/coreutils/xargs.c)
target_link_libraries(xargs winixcommon)

# Install all targets to C:\Winix\bin (or --prefix override)/bin
install(TARGETS
    winix
    echo pwd ls cat mv rm mkdir rmdir touch head tail
    date whoami sleep cp chmod chown more less clear
    stat wc du df sort uniq grep tee alias export history
    ps kill uname uptime env ver cut tr
    printf basename dirname which true false
    nix find diff sed xargs
    tac rev nl id timeout ln
    seq yes hostname
    paste comm base64 shuf
    test_cmd lbracket
    DESTINATION bin
)

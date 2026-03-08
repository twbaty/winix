cmake_minimum_required(VERSION 3.20)
project(Winix C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 17)

# Single source of truth for version — edit VERSION file only
file(READ "${CMAKE_SOURCE_DIR}/VERSION" WINIX_VERSION)
string(STRIP "${WINIX_VERSION}" WINIX_VERSION)
add_compile_definitions(WINIX_VERSION="${WINIX_VERSION}")

# Default install location — override with cmake --install build --prefix <path>
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "C:/Winix" CACHE PATH "Winix install root" FORCE)
endif()

include_directories(src/include src/common src/shell src/third_party/zlib)

# ------------------------------------------------------------
# zlib (bundled, static — used by gzip/gunzip)
# ------------------------------------------------------------
add_library(winix_zlib STATIC
    src/third_party/zlib/adler32.c
    src/third_party/zlib/compress.c
    src/third_party/zlib/crc32.c
    src/third_party/zlib/deflate.c
    src/third_party/zlib/gzclose.c
    src/third_party/zlib/gzlib.c
    src/third_party/zlib/gzread.c
    src/third_party/zlib/gzwrite.c
    src/third_party/zlib/infback.c
    src/third_party/zlib/inffast.c
    src/third_party/zlib/inflate.c
    src/third_party/zlib/inftrees.c
    src/third_party/zlib/trees.c
    src/third_party/zlib/uncompr.c
    src/third_party/zlib/zutil.c
)
target_compile_options(winix_zlib PRIVATE -w) # suppress zlib's own warnings

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
foreach(cmd stat wc du df sort uniq tee alias export history kill uname uptime env ver cut tr tac rev nl seq yes hostname paste comm base64 shuf md5sum sha256sum hexdump)
    add_executable(${cmd} src/coreutils/${cmd}.c)
    target_link_libraries(${cmd} winixcommon)
endforeach()

# grep is C++ (uses std::regex)
add_executable(grep src/coreutils/grep.cpp)
target_link_libraries(grep winixcommon)

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

add_executable(mktemp   src/coreutils/mktemp.c)
add_executable(realpath src/coreutils/realpath.c)
add_executable(cmp      src/coreutils/cmp.c)
add_executable(fold     src/coreutils/fold.c)
add_executable(expand   src/coreutils/expand.c)
add_executable(unexpand src/coreutils/unexpand.c)
add_executable(column   src/coreutils/column.c)
add_executable(time     src/coreutils/time.c)
add_executable(watch    src/coreutils/watch.c)
add_executable(bc       src/coreutils/bc.c)
target_link_libraries(bc m)

add_executable(awk      src/coreutils/awk.c)
target_link_libraries(awk m)

add_executable(wlint    src/coreutils/wlint.c)
target_link_libraries(wlint winixcommon bcrypt)

add_executable(wsim     src/coreutils/wsim.c)
target_link_libraries(wsim winixcommon)

add_executable(apropos  src/coreutils/apropos.c)

add_executable(strings  src/coreutils/strings.c)
add_executable(xxd      src/coreutils/xxd.c)
add_executable(od       src/coreutils/od.c)
add_executable(split    src/coreutils/split.c)

add_executable(expr     src/coreutils/expr.c)
add_executable(nproc    src/coreutils/nproc.c)
add_executable(truncate src/coreutils/truncate.c)
target_link_libraries(truncate winixcommon)
add_executable(numfmt   src/coreutils/numfmt.c)
target_link_libraries(numfmt m)
add_executable(readlink  src/coreutils/readlink.c)
add_executable(cksum     src/coreutils/cksum.c)
add_executable(factor    src/coreutils/factor.c)
add_executable(csplit    src/coreutils/csplit.c)
add_executable(pr        src/coreutils/pr.c)
add_executable(stdbuf    src/coreutils/stdbuf.c)
add_executable(b2sum     src/coreutils/b2sum.c)
add_executable(sha1sum   src/coreutils/sha1sum.c)
add_executable(sha512sum src/coreutils/sha512sum.c)
add_executable(sha224sum src/coreutils/sha224sum.c)
add_executable(sha384sum src/coreutils/sha384sum.c)
add_executable(join      src/coreutils/join.c)
add_executable(tsort     src/coreutils/tsort.c)
add_executable(tty       src/coreutils/tty.c)
add_executable(logname   src/coreutils/logname.c)
add_executable(printenv  src/coreutils/printenv.c)
add_executable(fmt       src/coreutils/fmt.c)
add_executable(unlink    src/coreutils/unlink.c)
add_executable(link      src/coreutils/link.c)
add_executable(sync      src/coreutils/sync.c)
add_executable(pathchk   src/coreutils/pathchk.c)
add_executable(base32    src/coreutils/base32.c)
add_executable(shred     src/coreutils/shred.c)
add_executable(dd        src/coreutils/dd.c)
add_executable(nice      src/coreutils/nice.c)
add_executable(nohup     src/coreutils/nohup.c)
add_executable(groups    src/coreutils/groups.c)
target_link_libraries(groups netapi32 advapi32)
add_executable(who       src/coreutils/who.c)
target_link_libraries(who wtsapi32)
add_executable(users     src/coreutils/users.c)
target_link_libraries(users wtsapi32)
add_executable(chgrp     src/coreutils/chgrp.c)
target_link_libraries(chgrp advapi32)
add_executable(install_cmd src/coreutils/install.c)
set_target_properties(install_cmd PROPERTIES OUTPUT_NAME "install")
target_link_libraries(install_cmd advapi32)
add_executable(gzip      src/coreutils/gzip.c)
target_link_libraries(gzip winix_zlib)
add_executable(gunzip    src/coreutils/gzip.c)   # same source, argv[0] detects gunzip
target_link_libraries(gunzip winix_zlib)

# winix shell goes to the root of the install prefix (C:\Winix\winix.exe)
install(TARGETS winix DESTINATION .)

# All coreutils go to C:\Winix\bin
install(TARGETS
    echo pwd ls cat mv rm mkdir rmdir touch head tail
    date whoami sleep cp chmod chown more less clear
    stat wc du df sort uniq grep tee alias export history
    ps kill uname uptime env ver cut tr
    printf basename dirname which true false
    nix find diff sed xargs
    tac rev nl id timeout ln
    seq yes hostname
    paste comm base64 shuf
    md5sum sha256sum hexdump
    test_cmd lbracket
    mktemp realpath cmp fold expand unexpand
    column time watch bc awk wlint wsim apropos
    strings xxd od split
    expr nproc truncate numfmt readlink cksum factor
    sha1sum sha512sum sha224sum sha384sum
    join tsort tty logname printenv fmt
    unlink link sync pathchk base32 shred dd nice nohup
    groups who users
    csplit pr stdbuf b2sum
    chgrp install_cmd
    gzip gunzip
    DESTINATION bin
)

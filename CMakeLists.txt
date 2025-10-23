cmake_minimum_required(VERSION 3.20)
project(Winix C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 17)

include_directories(src/include src/common)

add_library(winixcommon STATIC src/common/argparser.c src/common/fileops.c)

# Example utilities
add_executable(echo src/coreutils/echo.c)
target_link_libraries(echo winixcommon)

add_executable(pwd src/coreutils/pwd.c)
target_link_libraries(pwd winixcommon)

add_executable(ls src/coreutils/ls.c)
target_link_libraries(ls winixcommon)

# The shell (future)
add_executable(winix src/shell/main.cpp)
target_link_libraries(winix winixcommon)

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
target_link_libraries(chown winixcommon)

add_executable(more src/coreutils/more.c)
target_link_libraries(more winixcommon)

add_executable(less src/coreutils/less.c)
target_link_libraries(less winixcommon)

# Tier 1–4 coreutils
foreach(cmd stat wc du df sort uniq grep tee alias export history ps kill uname uptime env ver)
    add_executable(${cmd} src/coreutils/${cmd}.c)
    target_link_libraries(${cmd} winixcommon)
endforeach()

add_executable(printf src/coreutils/printf.c)
add_executable(basename src/coreutils/basename.c)
add_executable(dirname src/coreutils/dirname.c)
add_executable(which src/coreutils/which.c)
add_executable(true src/coreutils/true.c)
add_executable(false src/coreutils/false.c)

install(TARGETS echo pwd winix DESTINATION usr/bin)

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

install(TARGETS echo pwd winix DESTINATION usr/bin)

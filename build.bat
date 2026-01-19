@echo off

clang.exe -Wno-deprecated-declarations -Wall -Wextra -O2 ccalc.c -o ccalc.exe -DBUILD_DIR=$(pwd)

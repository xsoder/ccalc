@echo off

clang.exe -std=c99 -Wno-deprecated-declarations -Wall -Wextra -O2 aoxim.c -o aoxim.exe -DBUILD_DIR=%%pwd%

@echo off

clang.exe -std=c99 -Wno-deprecated-declarations -Wall -Wextra -O2 aoxim.c -o aoxim.exe -ldl -lm -DBUILD_DIR=%%pwd%

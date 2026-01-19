@echo off

clang.exe -std=c99 -Wall -Wextra -O2 ccalc.c -o ccalc -lm -DBUILD_DIR=$(pwd)

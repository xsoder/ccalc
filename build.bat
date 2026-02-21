@echo off

mkdir aoxim-dist

clang-format.exe -i aoxim.c
clang.exe -std=c99 -Wno-deprecated-declarations -Wall -Wextra -O2 aoxim.c -o .\aoxim-dist\aoxim.exe

xcopy .\stdlib .\aoxim-dist\stdlib /s /e /i /Y

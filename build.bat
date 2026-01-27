@echo off

mkdir aoxim-dist

clang-format.exe -style=Google -i aoxim.c
clang.exe -std=c99 -Wno-deprecated-declarations -Wall -Wextra -O2 aoxim.c -o aoxim.exe

copy /y .\aoxim.exe .\aoxim-dist
xcopy .\stdlib .\aoxim-dist\stdlib /s /e /i

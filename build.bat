@echo off

<<<<<<< HEAD
clang.exe -std=c99 -Wno-deprecated-declarations -Wall -Wextra -O2 aoxim.c -o aoxim.exe -ldl -lm -DBUILD_DIR=%%pwd%
clang-format.exe -style=Google -i aoxim.c
=======
clang.exe -std=c99 -Wno-deprecated-declarations -Wall -Wextra -O2 aoxim.c -o aoxim.exe -DBUILD_DIR=%%pwd%
>>>>>>> origin/master

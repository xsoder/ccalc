#!/bin/sh

set -xe

clang-format -style=Google -i aoxim.c
cc -std=c99 -Wall -Wextra -O2 aoxim.c -o aoxim -ldl -lm -DBUILD_DIR=$(pwd)

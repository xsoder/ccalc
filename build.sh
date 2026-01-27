#!/bin/sh

set -xe

mkdir -p aoxim-dist/

clang-format -style=Google -i aoxim.c
cc -std=c99 -Wall -Wextra -O2 aoxim.c -o ./aoxim-dist/aoxim -ldl -lm -DBUILD_DIR=$(pwd)

cp -r ./stdlib/ ./aoxim-dist/

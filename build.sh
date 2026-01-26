#!/bin/sh

set -xe

cc -std=c99 -Wall -Wextra -O2 aoxim.c -o aoxim -ldl -lm -DBUILD_DIR=$(pwd)

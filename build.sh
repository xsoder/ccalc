#!/bin/sh

set -xe

cc -std=c99 -Wall -Wextra -O2 ccalc.c -o ccalc -ldl -lm -DBUILD_DIR=$(pwd)

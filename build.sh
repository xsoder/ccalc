#!/bin/sh

set -xe

cc -std=c99 -Wall -Wextra -O2 ccalc.c -o ccalc -lm -ldl -DBUILD_DIR=$(pwd)

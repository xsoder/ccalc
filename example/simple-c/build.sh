#!/bin/sh

set -xe

mkdir -p build

cc -Wall -Wextra -shared -fPIC -o build/add.so add.c



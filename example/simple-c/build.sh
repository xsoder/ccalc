#!/bin/sh

set -xe

if [ ! -d "build/"];then
    mkdir -p build
fi

cc -Wall -Wextra -shared -fPIC -o build/add.dll add.c



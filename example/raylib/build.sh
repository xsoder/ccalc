#!/bin/sh

set -xe

INCLUDE="-I/home/xsoder/opt/raylib-5.5_linux_amd64/include"
LIB_PATH="/home/xsoder/opt/raylib-5.5_linux_amd64/lib/"
LIBS="-L/home/xsoder/opt/raylib-5.5_linux_amd64/lib/"

cc $INCLUDE -shared rl-binds.c -o librlbinds.so -fPIC $LIBS -lraylib -Wl,-rpath,"$LIB_PATH"

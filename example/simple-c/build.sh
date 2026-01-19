#!/bin/sh

set -xe

cc -shared -fPIC -o add.so add.c

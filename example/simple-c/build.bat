@echo off

mkdir build

clang.exe -shared -o build/add.dll add.c

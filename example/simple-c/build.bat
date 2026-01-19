@echo off

mkdir build

clang.exe -shared -fPIC -o build/add.dll add.c

echo "Usage run the ./ccalc.exe add.calc"

import os
import platform
import subprocess

LLVM_PATH = "llvm-21.1.8-Release"
LLVM_TAR = "llvm-21.1.8-Release.tar.gz"
LLVM_CHK = "chk/llvm_chk"

COMPILER_PATH = f"{LLVM_PATH}/bin/clang"

WIN_CONTENT = f"""@echo off
"{COMPILER_PATH}.exe" -Wno-deprecated-declarations -Wall -Wextra -O2 ccalc.c -o ccalc.exe -DBUILD_DIR=%pwd%
"""

POSIX_CONTENT = f"""#!/bin/sh
set -xe
LLVM_INCLUDE=$("{LLVM_PATH}/bin/llvm-config --includedir")
LLVM_LIBS=$("{LLVM_PATH}/bin/llvm-config --libs")
{COMPILER_PATH} -std=c99 -Wall -Wextra -O2 ccalc.c -o ccalc -lm -ldl -DBUILD_DIR=$(pwd) $LLVM_LIBS
"""

def detect():
    os_name = platform.system()
    if os.path.isdir(LLVM_PATH):
        return
    else:
        if os.path.exists(LLVM_TAR):
            extract(os_name)
        elif not os.path.exists(LLVM_CHK):
            exec_make(os_name)

def exec_make(os_name):
    if os_name == "Windows" :
        program = "make.exe"
    else:
        program = "make"
    args = [program]
    try:
        result = subprocess.run(args, check=True)
        print(f"{program} executed successfully.")
    except subprocess.CalledProcessError as e:
        print(f"Error occurred while executing {program}: {e}")

def extract(os_name):
    program = "tar.exe" if os_name == "Windows" else "tar"
    args = [program, "-xvf", LLVM_TAR]
    print(arg)
    try:
        result = subprocess.run(args, check=True)
        print(f"{program} executed successfully.")
    except subprocess.CalledProcessError as e:
        print(f"Error occurred while executing {program}: {e}")
        
def generate_build():
    os_name = platform.system()
    if os_name == "Windows":
        build = "build.bat"
        content = WIN_CONTENT
    else:
        build = "build.sh"
        content = POSIX_CONTENT
    
    with open(build, "w") as f:
        f.write(content)
            
if __name__ == "__main__":
    detect()
    generate_build()

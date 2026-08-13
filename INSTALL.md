# Installing openwatcom2irc

## Quick Start (Linux x86_64)

```bash
# Clone
git clone https://github.com/verta1878/openwatcom2irc.git
cd openwatcom2irc

# Set environment
export OWROOT=$(pwd)
export OWTOOLS=GCC
export OWOBJDIR=binbuild
source cmnvars.sh

# Bootstrap (builds the compiler using GCC)
cd bld/builder/binbuild
wmake -h -f ../binmake bootstrap=1
cd $OWROOT
builder rel

# Verify
export PATH=$OWROOT/build/binbuild:$PATH
export WATCOM=$OWROOT/rel

# DOS target
echo "int main(void){return 0;}" > test.c
bwcc386 test.c -fo=test.obj -i=$WATCOM/h -bt=dos
file test.obj  # → 8086 relocatable (Microsoft)

# x86_64 target
bwcc386 test.c -fo=test.o -i=$WATCOM/h -bt=linux64
file test.o    # → ELF 64-bit LSB relocatable, x86-64
```

## Requirements

- Linux x86_64 host
- GCC 13 or later
- GNU Make
- ~2GB disk space for full build

## What Gets Built

- `bwcc` — C compiler (16-bit targets)
- `bwcc386` — C compiler (32-bit + 64-bit targets)
- `bwpp386` — C++ compiler
- `bwcl386` — Compiler driver
- `wlink` — Linker
- `wlib` — Librarian
- `wasm` — Assembler
- 1,106 runtime libraries
- 105 Win32 API import libraries

## Verifying the Build

```bash
cd tests
bash test_suite.sh
# Expected: 21/21 passed, 0 failed
```

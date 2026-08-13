# openwatcom2irc — OpenWatcom 2.0 Fork with x86_64 ELF64 Backend

**First OpenWatcom-family compiler to produce x86_64 ELF64 objects.**

## Status

| Feature | Status |
|---------|--------|
| ELF64 object output | ✅ Working |
| Symbol table (main exported) | ✅ Working |
| GCC linking | ✅ Working |
| OWL sections (.text/.rodata/.data/.bss) | ✅ Working |
| DOS/Win32/OS2 targets | ✅ No regression (21/21) |
| x86_64 instruction encoding | ⬜ In progress |
| External call relocations | ⬜ In progress |
| Code/data separation | ⬜ In progress |

## What This Is

A fork of [open-watcom/open-watcom-v2](https://github.com/open-watcom/open-watcom-v2) (commit `d44c56f4`) that adds x86_64 Linux ELF64 as a compiler target. OpenWatcom v2 can **run** on x86_64 hosts but can only **target** 16-bit and 32-bit x86. This fork adds the missing x64 backend.

```
bwcc386 hello.c -fo=hello.o -bt=linux64
→ ELF 64-bit LSB relocatable, x86-64, version 1 (SYSV), not stripped
```

## Architecture

```
Frontend (cmdlnx86.c)
  → -bt=linux64 sets CGSW_GEN_OBJ_ELF flag
  → Defines __X86_64__, __LP64__, __amd64__, __LINUX__, __UNIX__

Code Generator (x86enc.c)
  → REX prefix accumulator (SetRexW/R/X/B)
  → TransferIns inserts REX before opcode
  → LayReg/LayRegRM detect R8-R15, set REX.R/B
  → INC/DEC remapped (0x40-0x4F → FF /0, FF /1)

Object Output (x64dispatch.c → x64obj.c → OWL)
  → X64CheckDispatch detects ELF flag
  → X64ObjInit creates OWL handle + ELF64 sections
  → OutDBytes redirects to OWLEmitData
  → OutLabel emits OWL symbols (FEName + OWLEmitLabel)
  → X64ObjFini finalizes ELF64 via OWLFileFini

OWL Library (owelf.c)
  → ELFFileEmit64 writes Elf64_Ehdr + sections
  → x86_64 relocation tables in owreloc.c
```

## Build

### Prerequisites
- GCC 13+ (bootstrap compiler)
- Linux x86_64 host

### Bootstrap
```bash
git clone https://github.com/verta1878/openwatcom2irc.git
cd openwatcom2irc
export OWROOT=$(pwd)
export OWTOOLS=GCC
. ./cmnvars.sh

# Build full toolchain
cd bld/builder/binbuild && wmake -h -f ../binmake bootstrap=1
cd $OWROOT && builder rel

# Test x64 output
echo "int main(void){return 42;}" > test.c
bwcc386 test.c -fo=test.o -i=$WATCOM/h -bt=linux64
file test.o
# → ELF 64-bit LSB relocatable, x86-64
```

### Targets
| Target | Flag | Output |
|--------|------|--------|
| DOS 16-bit | `-bt=dos` | MZ/EXE |
| DOS 32-bit | `-bt=dos4g` | LE (dos4g) |
| Win32 | `-bt=nt` | PE32 |
| OS/2 | `-bt=os2` | LX |
| **Linux x64** | **`-bt=linux64`** | **ELF64** |

## Files Changed from Upstream

### Upstream Patches (15 files)
| File | Change |
|------|--------|
| `bld/watcom/h/banner.h` | Version stamp: "Open Watcom-irc" |
| `bld/owl/c/owelf.c` | Added ELFFileEmit64() — 208 lines |
| `bld/owl/c/owreloc.c` | x86_64 relocation tables |
| `bld/owl/c/owfile.c` | ELF64 dispatch in OWLFileFini |
| `bld/cc/c/cmdlnx86.c` | `-bt=linux64` + 5 predefined macros |
| `bld/cg/intel/h/ctypes.h` | TS_LINUX64 enum |
| `bld/cg/intel/c/x86obj.c` | X64 dispatch hooks + Out* redirects |
| `bld/cg/intel/c/x86enc.c` | REX accumulator + Lay* patches |
| `bld/cg/intel/master.mif` | x64 objects + OWL include path |
| `bld/cg/client.mif` | `use_owl_lib_386 = 1` |

### New x64 Target Files (11 files, ~1,600 lines)
| File | Lines | Purpose |
|------|-------|---------|
| `x64/x64dispatch.c` | 27 | ELF flag check, dispatch control |
| `x64/x64enc.c` | 242 | REX encoding, register detection |
| `x64/x64obj.c` | 240 | OWL-based ELF64 output |
| `x64/x64enc.h` | 135 | REX constants, MODRM macros |
| `x64/cgx64reg.h` | 84 | R8-R15, XMM0-15 register defs |
| `x64/regindex.h` | 94 | QWORD register class |
| `x64/x64sysv.h` | 476 | SysV ABI calling convention |
| `x64/cgtargsw.h` | 19 | Target switches |
| `x64/x64obj.h` | 20 | Object output declarations |
| `x64/INTEGRATION.md` | 130 | Integration guide |
| `x64/X86ENC-PATCHES.md` | 161 | Encoder patch specification |

## Test Results

21/21 pass across 5 categories:
- Frontend: 7/7 (macros, target acceptance)
- Backend: 7/7 (object generation, ELF64 output)
- SysV ABI: 4/4 (7 args, callee-save, varargs)
- Cross-target: 2/2 (DOS + linux64 from same source)
- PCBoard regression: 1/1 (74/222 baseline)

## Related Projects

- [pcbirc](https://github.com/verta1878/pcbirc) — PCBoard 15.4 Revival (built with openwatcom2irc)
- [fpc264irc](https://github.com/verta1878/fpc264irc) — Free Pascal 2.6.4 fork
- [netmodem2irc](https://github.com/verta1878/netmodem2irc) — FOSSIL/TCP bridge
- [mystic-bbs-irc](https://github.com/verta1878/mystic-bbs-irc) — Mystic BBS fork

## Crew

| Handle | Role |
|--------|------|
| verta1878 | Project lead |
| sysop/0 | Compiler engineer |
| hexadecimal | PCBoard port |
| wrench | Transport layer, FOSSIL testing |
| evga | Display, Mystic |
| kiddo | Protocols, RIPscrip |

## License

Sybase Open Watcom Public License (same as upstream).

# Changelog

## r0.3.0 — 2026-08-12

**MILESTONE: First runnable x86_64 binary from OpenWatcom-family compiler.**

```
$ bwcc386 hello.c -fo=hello.o -bt=linux64 -ox
$ gcc hello.o -o hello -no-pie
$ ./hello; echo $?
42
```

### Added
- x86_64 ELF64 object output via OWL (Object Writer Library)
- `-bt=linux64` target: `__X86_64__`, `__LP64__`, `__amd64__`, `__LINUX__`, `__UNIX__`
- REX prefix accumulator in x86 instruction encoder
- OWL ELF64 emitter (ELFFileEmit64, x86_64 relocation tables)
- SysV x86_64 ABI specification (476 lines)
- Parallel OWL+OMF initialization (no cg refactoring needed)
- Symbol table export (main visible via FEName + OWLEmitLabel)
- OWL library linked into 386 cg (use_owl_lib_386 = 1)
- 11 new x64 target files (1,330 lines)

### Changed
- x86obj.c: Out* functions redirect to OWL when X64IsActive()
- x86enc.c: REX accumulator, LayReg/LayRM extended register detection
- x86enc.c: INC/DEC remapping (0x40-0x4F → FF /0, FF /1)
- intel/master.mif: x64 objects, include paths, OWL headers
- cg/client.mif: OWL linked for 386 target
- bwpp386: fmtsym.obj bootstrap fix (C++ compiler working)

### Fixed
- hw_reg_set member access: `._1` not `.u.word[1]`
- TransferIns REX insertion after legacy prefixes
- OutDataByte routing to .text for code bytes (RET)

### Upstream
- Base: open-watcom/open-watcom-v2 @ d44c56f4
- 15 files patched, zero upstream regressions
- 21/21 test suite pass

## r0.2.0 — 2026-08-12 (earlier)
- ELF64 container output (valid but not runnable)
- Symbol table (main exported)
- GCC linking (successful)

## r0.1.0 — 2026-08-05
- Frontend: -bt=linux64 accepted
- Predefined macros working
- OWL ELF64 sections created
- REX encoding validated in standalone tests
- SysV ABI validated (7 args, callee-save, varargs)

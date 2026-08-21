# openwatcom2irc — Known Issues

Status as of the r0.6.0 additions/fixes (2026-08-20). The x86-64 backend
passes **61/61** runtime tests (`tests/x64/run_tests.sh`).

## Resolved since the early r0.6.0 snapshot

The following were open in the first x64 cut and are now **fixed** — each
has a passing test in `tests/x64/cases/`:

- **Loops** (was ISSUE-1). The INC/DEC-vs-REX opcode collision and the
  backward-jump-into-prologue were both real. Fixed by adding a proper x86
  instruction-length decoder to the relocation pass (`x64obj.c`) and
  correcting frame-register handling (`x86proc.c`). Tests: `breakloop`,
  `bubblesort`, and the loop-bearing cases.
- **Recursion / multi-function objects** (was ISSUE-2). Function
  boundaries are recovered correctly; `fib`/`factorial`/`gcd` link and
  run. The old "hardcoded main at offset 0" assumption is gone.
- **Global variables** (was ISSUE-3). Globals, file-scope statics, BSS,
  and pointer initialisers all work. Tests cover each.
- **DGInteger overflow.** The constant-emit buffer was byte[6] and
  overflowed on 8-byte pointer constants; sized correctly. Found with ASan.

## Open / limitations

- **Struct pointer fields at 4-byte offsets.** Some struct layouts still
  assume `WORD_SIZE` (4) rather than 8 for pointer members, which can
  affect dynamically-built linked lists. Most code is unaffected; flagged
  for the next codegen pass.
- **Pointer-through-function above 4 GB.** The Watcom EAX-return path can
  truncate a pointer above 4 GB; mitigated by allocating x64 test memory
  with `MAP_32BIT`. Not an issue for DOS or normal freestanding use.
- **LEDATA enumerated offset** (LATENT). The post-processor concatenates
  LEDATA chunks in file order and ignores the enumerated-data-offset
  field. Every object tested reports offset 0, so nothing is broken today,
  but a sparse/out-of-order emitter would need this handled.

## Scope

Straight-line code, loops, recursion, globals, structs, function
pointers, unions, long-long shifts, and multi-file linking all work
(61/61). The x86-64 backend is suitable for real code — it has been
validated against a large real-world driver codebase (kept in a separate
repo). DOS targets (16-bit `wcc`, 32-bit `wcc386`) are unchanged from
upstream and verified.

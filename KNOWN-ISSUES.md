# openwatcom2irc — Known Issues

## Working (verified by running the binary)

| Test | Flags | Result |
|------|-------|--------|
| return 42 | -ox / -ox -s / -od -s / -od | exit 42 |
| puts("Hello…") | -ox -s / -od | prints, exit 0 |
| 3x puts | -ox -s | all 3 strings print |
| arithmetic | -ox -s | exit 42 |
| switch/case | -ox -s | exit 42 |

All 32-bit targets (dos16, dos32, dos4g, nt, os2) compile clean, no REX
contamination in their OMF output.

---

## ISSUE-1: Loops segfault — OPEN

`for(i=1;i<=10;i++) sum+=i;` segfaults at all optimization levels.

Two independent causes identified:

**1a. INC/DEC opcode collision.** Single-byte `INC reg` (0x40-0x47) and
`DEC reg` (0x48-0x4F) are REX prefixes in 64-bit mode. The loop emits
`40 83 f8 0a`, which x86-64 decodes as `REX + CMP` instead of
`INC EAX; CMP EAX,10`. The counter never increments.

The two-byte form (`FF C0`) is the correct encoding, but substituting it
grows the instruction by one byte, which shifts every following offset.
The jump-fixup pass handles that, but a naive byte scan cannot
distinguish an opcode 0x40 from a 0x40 appearing as a ModR/M or
immediate byte inside another instruction. A real length-decoder is
required before this can be done safely.

**1b. Backward jump targets function entry.** The `JLE` at the bottom of
the loop has displacement -20, resolving to offset 0 — the function
prologue — rather than the loop body. Re-entering the prologue resets the
accumulator and re-pushes a register each iteration.

Important: compiling the same source with `-bt=dos` produces
byte-identical code with the same displacement. The DOS object never
passes through our post-processor, so **this displacement is not
introduced by our code.** Whether it is correct-but-misread by us, or an
upstream codegen artifact exposed by the TS_DOS override, is unresolved.
Next step: assemble the same loop with wasm and diff, or single-step the
DOS build under DOSBox.

## ISSUE-2: Internal function calls unresolved — OPEN

Recursion (`fib` calling `fib`) links with `undefined reference to fib`.
The compiler emits `fib` in EXTDEF (external) and never emits a PUBDEF
for it, so the post-processor marks it `SHN_UNDEF`.

Compounding this: the post-processor hardcodes `main` at .text offset 0.
In a multi-function object that is wrong — in the fib test, `fib` occupies
0x00-0x59 and `main` starts at 0x5A. Function boundaries must be
recovered (likely from LINNUM or by treating each EXTDEF name that also
appears as a local definition) before multi-function objects can link.

## ISSUE-3: Global variables broken — OPEN

`MOV EAX, moffs32` (opcode A1) is `MOV EAX, moffs64` in 64-bit mode — it
reads 8 bytes of address instead of 4, so it consumes following
instruction bytes. Any program with a global variable miscompiles.
Needs rewriting to RIP-relative addressing.

## ISSUE-4: LEDATA enumerated offset ignored — LATENT

The post-processor concatenates LEDATA chunks in file order and ignores
the record's 4-byte enumerated-data-offset field. Every object tested so
far reports offset 0, so nothing is broken today, but an object emitting
out-of-order or sparse chunks would be assembled incorrectly. Should
place each chunk at its stated offset.

---

## Scope

Straight-line code and calls to external functions work. Loops,
recursion, and globals do not. Suitable for smoke-testing the toolchain;
not yet suitable for building pcbirc or the Cyclades driver.

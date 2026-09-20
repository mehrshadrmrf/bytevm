# ByteVM

**Version:** 0.3 (Draft - supersedes 0.2)

**Language:** C17


**Status:** v1.0. All ten are implemented against this
spec - the full ISA, the `.bvm` loader, a two-pass assembler, a
byte-identical-round-trip disassembler, an interactive debugger, golden
regression tests, a fuzz harness, and CI all exist and pass. CLI:
`bytevm run` / `asm` / `dis` / `debug`.

---

## 0. Revision Notes

### 0.1 → 0.2

v0.1 was reviewed before any implementation existed. The review found one
memory-corruption bug, one flag-semantics contradiction, and several
underspecified areas that would have produced inconsistent behavior across
phases if implemented as written. All are fixed below; no implementation
code had been written yet, so none of this required touching source files -
only the spec.

| # | Section | Problem | Fix |
|---|---------|---------|-----|
| 1 | §6 | `SP` initialized to `0xFFFF`; `PUSH`/`CALL` write *then* decrement, so the very first stack write touches `mem16[0xFFFF]`, which wraps to address `0x0000` - corrupting the first byte of the code section on the first `PUSH` or `CALL` any program executes. | `SP` now initializes to `0xFFFE`. `0xFFFF` is still written (it holds the high byte of the very first pushed value) but is never the value `SP` holds *before* a write, so the top-of-address-space wraparound is now structurally impossible - see §6 (and the 0.2→0.3 notes below for a wording correction to this same point, which described `0xFFFF` inaccurately as "unused"). |
| 2 | §5, §8.2 | Design text said `MUL` sets `C`/`O` "on truncation," but gave no formula distinguishing unsigned truncation from signed overflow - and the two are different bit patterns, not the same check. | Added an explicit, separate formula for `MUL`'s `C` and `O` (§8.2). It is *not* the same computation as `ADD`/`SUB`'s overflow check, and not the same as the `Z`/`S`-only logic-op flags. |
| 3 | §7.1, §8.7 | `SYS`'s operand was described as "IMM8, stored as the low byte of an IMM16 operand" - but `IMM8` was never a real addressing mode, so the phrase referred to something that didn't exist in the encoding table. | Added a real `IMM8` addressing mode (`0x5`). `SYS` now encodes its code as one byte instead of two. |
| 4 | §7.1, §8.1 | `REG_INDIRECT` (mode `0x4`) was defined in the encoding table but never made legal for any instruction. Result: no program can index an array or dereference a runtime pointer - every memory address had to be a compile-time literal. (`examples/sort.asm` in the original scaffold was a placeholder for exactly this reason.) | `REG_INDIRECT` is now an explicitly legal mode for `LOAD`'s and `STORE`'s address operand. See the worked examples in §7.2. |
| 5 | §8.5 | Only signed conditional jumps existed (`JG`/`JGE`/`JL`/`JLE`, derived from `S`/`O`). Addresses and `DIV`/`MOD` are unsigned, but there was no way to branch on an unsigned comparison - `JL`/`JG` silently give wrong answers once a value crosses `0x8000`. | Added `JB`/`JAE`/`JA`/`JBE` (unsigned below / above-or-equal / above / below-or-equal), derived from `C` and `Z`, named after the x86 convention on purpose. |
| 6 | §7 (§7.3) | Nothing specified which addressing modes were legal for which operand of which instruction. The encoding scheme could express nonsense (e.g. an immediate as `ADD`'s destination) with no stated decode-time or assemble-time behavior. | Added §7.3, a full legality table. Both the assembler and `instruction_decode` must reject illegal combinations with `VM_ERR_INVALID_MODE`. |
| 7 | §8.3 | Shift-count behavior for `n == 0` and `n >= 16` was unspecified. A naive C implementation (`dst << n` on a promoted `int`) does not reproduce the intended 16-bit semantics and invites shift-related UB for large `n`. | Added exact formulas for every range of `n`, so no implementation ever executes a native C shift with an out-of-range count. |
| 8 | §9 | `SYS` with an unrecognized code had no defined error. The stub behavior was to silently do nothing, which conflicts with the project's own "no silent failures" error philosophy. | Added `VM_ERR_INVALID_SYSCALL`. |
| 9 | §10 | The on-disk header was implicitly assumed readable via a raw `fread` into `BvmHeader`. C struct padding/alignment is implementation-defined, so that is not a portable way to parse a fixed wire format. Also missing: validation that `entry_point` actually lands inside the code blob, and that code/data regions don't overlap. | §10 now requires field-by-field parsing with the same little-endian helpers used for memory, and adds both missing validations. |
| 10 | §1.2, §7.3 | Two things looked like bugs but were actually undecided scope: signed division, and computed/indirect jump targets. | Both are now explicit non-goals (§1.2), so nobody re-discovers them as "missing" mid-implementation. |
| 11 | §14 | `tests/unit/test_*.c` each defined their own `main`. Linking more than one into a single test binary - which the build already does - fails with "multiple definition of `main`." (Verified: this is not hypothetical, it failed on a real build.) | §14 now specifies the harness shape that avoids this: exactly one `main`, in `tests/test_main.c`. |
| - | build | `CMakeLists.txt` didn't pass `-Werror`, so the two build paths (`make` vs `cmake`) enforced different strictness. Separately, both `Makefile` and `CMakeLists.txt`'s *test*-binary rule only globbed `tests/unit/*.c` and never included `tests/test_main.c` - the file the harness fix above requires. | Fixed directly in the build files (not a design-doc concern, noted here for completeness). Caught and fixed during Phase 1, before any test file existed to trigger the link failure. |

Opcode renumbering caused by fix #5: `CALL`, `RET`, `SYS` moved from
`0x19`/`0x1A`/`0x1B` to `0x1D`/`0x1E`/`0x1F` to make room for the four new
jump opcodes. Nothing else changed number.

### 0.2 → 0.3

A second review, done after Phase 1 landed, found 22 further
specification gaps - none of them affect the already-implemented
`Memory`/`CPU`/`VM` core (verified below, row by row), so no Phase 1 code
changed. All are gaps in *unwritten* phases (2 and later) and in the spec's
internal consistency. One additional issue (a stale cross-reference) was
found while making these edits and is included for completeness.

| # | Section | Problem | Fix |
|---|---------|---------|-----|
| 1 | §4, §6 | `SP` was described as pointing "at the last pushed value," but `PUSH`/`CALL` write at the current `SP` and *only then* decrement it - so after a push, `SP` points at the next free slot, not at what was just written. The prose and the semantics disagreed. | §4 and §6 now describe this correctly as an **empty-descending** stack: `SP` always holds the address of the next free slot; the most recently pushed value lives at `SP + 2`. |
| 2 | §6 | The memory diagram labeled `0xFFFF` a "permanently unused guard byte" in the same section whose own prose says it holds the high byte of the first pushed value - a direct self-contradiction introduced while fixing 0.1's wraparound bug. | Diagram caption corrected: `0xFFFF` is written to (by the first push) but is never the value `SP` holds *before* a write, which is the actual property that matters. |
| 3 | §6 | "Stack overflow: SP would go below the top of the data section" had no exact boundary formula - unclear whether the check was `SP == limit` or `SP < limit`, checked before or after the decrement. | §6 now gives an exact formula, checked *before* the write: halt if `SP < 2` (the decrement itself would wrap past `0x0000`) or `SP - 2 < stack_limit`. |
| 4 | §6, §10 | The loader validates that code and data don't overlap *each other*, but no region was ever reserved against the stack growing into either one - and the fix for #3 needed a boundary value anyway. | `stack_limit` (fix #3) is defined as `max(code_addr + code_size, data_addr + data_size)` - the top of *whichever* loaded region is highest, not just the data section, since the header doesn't guarantee data sits above code. |
| 5 | §2 (new §2.1) | `CALL`'s stored return address depends on exactly when `PC` advances relative to fetching `CALL`'s own operand bytes, and nothing defined that ordering. | Added §2.1, a formal fetch/decode/execute cycle: `PC` advances past every consumed byte *during* fetch, before execute runs. `CALL` therefore always pushes the address of the instruction immediately following it. |
| 6 | §2.1, §3 | Undefined whether an instruction whose encoded bytes would extend past `0xFFFF` wraps to `0x0000` or errors. | §2.1 states it wraps, for consistency with §3's existing memory-wraparound rule - no new error code for this case. |
| 7 | §10 | Nothing required the physical `.bvm` file to actually be as long as its own header claims (`code_size`/`data_size`) - a truncated file could cause an out-of-bounds read while loading. | Added to §10's rejection list: file length must be `>= 16 + code_size + data_size`. |
| 8 | §10 | The header's `reserved` byte is specified as "must be 0," but the loader's validation list never said what happens if it isn't. | Added to §10's rejection list: non-zero `reserved` is rejected. |
| 9 | §7.1 | A `REG`/`REG_INDIRECT` operand's index byte is encoded as a full byte (for byte-alignment), but only `0`-`7` are architecturally valid registers; `8`-`255` were never addressed. | §7.1 now states these must decode as `VM_ERR_INVALID_MODE`, the same code already used for unknown mode nibbles and illegal mode/operand combinations. |
| 10 | §11 (new §11.4) | No defined range or signedness rule for assembler immediates - unclear whether `-1`, `-32768`, `65535`, or `65536` are valid, or how negatives are encoded. | Added §11.4: `IMM16` accepts `[-32768, 65535]` (both signed and unsigned readings share the 16-bit encoding space by design); `IMM8` (only used by `SYS`) accepts `[0, 255]`, unsigned only. |
| 11 | §8.7 | `READ_INT` had no defined behavior for invalid input, EOF, whitespace, or out-of-range values. | §8.7 now specifies: skip leading whitespace, parse an optional sign and decimal digits, truncate the magnitude modulo 65536, and - since malformed/absent input is an environment condition, not a VM fault - set `R0 = 0` and continue (no halt) if no digits are found. |
| 12 | §8.7 | `R0` is a full 16-bit value; nothing mapped it to a host process exit status, which is conventionally 8 bits. | §8.7 now defines the host exit code as `R0 & 0xFF`. |
| 13 | §8.2 | `ADD`/`SUB`/`CMP`'s `C`/`O` flags were described only informally ("operand signs imply a result sign that's impossible") - precise enough for a human, not for two independent implementations to agree on edge cases like `0xFFFF + 1` or `0x8000 - 1`. | §8.2 now gives the same kind of explicit bit-formula for `ADD` and `SUB`/`CMP` that `MUL` already had. |
| 14 | §11.2, §12 | The disassembler emits a `.byte` fallback for undecodable bytes (§12), but the assembler's directive table never defined `.byte` - so `bytecode → disassembler → assembler` could not always round-trip, contradicting §12's/§14's own round-trip requirement. | Added `.byte N` to §11.2. |
| 15 | §11.2 | `.org`'s interaction with `.data`, multiple `.org`s, and section boundaries was unstated - different assemblers could lay out the same source differently. | §11.2 now defines exactly two ordered sections (code, then data via a `.data` that may appear once and isn't reversible) and requires `.org` within a section to be non-decreasing, rejecting anything that would overlap already-emitted bytes. |
| 16 | §11.2 | `.string`'s escape-sequence support was never listed. | §11.2 now lists the supported set: `\n \r \t \\ \" \0 \xNN`; anything else is an assembler error. |
| 17 | §12 | Distinguishing genuine code from data embedded in the code region is ambiguous for a purely linear disassembler. | Acknowledged explicitly in §12 as an inherent limitation (shared by real-world disassemblers) rather than something this design solves - the `.byte` fallback is mechanical (fires on decode failure), not semantic, and that's sufficient for the round-trip guarantee this section actually promises. |
| 18 | §13 | Breakpoint timing (before/after fetch, before/after execute) was unstated, which affects observable debugger behavior. | §13 now states a breakpoint fires before the instruction at that address executes (matching conventional debugger semantics, e.g. GDB). |
| 19 | §9 | Calling the execute-step function on an already-halted VM had no defined behavior. | §9 now defines it as a no-op that returns the current `last_error` (or `VM_OK`) without touching any state. |
| 20 | §4 | `SP`'s initial value was explicit (`0xFFFE`, §6), but `R0`-`R7`/`PC`/`FLAGS`'s initial values were only implied, not stated as a formal contract. | §4 now states the full initial-state contract explicitly, and clarifies that loading a program then overwrites `PC` with `entry_point`. |
| 21 | §13 | `reset`'s exact effect (reload from disk vs. just re-zero registers; whether it clears memory; whether it preserves breakpoints) was unstated. | §13 now defines `reset` precisely: full reload from the original `.bvm` (discarding runtime/self-modifying-code changes), full CPU reset, `PC = entry_point`, error state cleared, breakpoints preserved (they're debugger-session state, not VM state). |
| 22 | §14 | The fuzz-robustness requirement had no step budget, so a fuzzed program containing an infinite loop (e.g. `JMP` to itself) could hang the test harness forever. | §14 now requires a fixed step budget per fuzz iteration; hitting it counts as a pass (memory safety and determinism are the goal, not termination, which can't be guaranteed for arbitrary bytecode). |
| - | §7.2 | Stale cross-reference: "makes the disassembler (§11) a meaningful, round-trip-able component" - the disassembler is §12; §11 is the assembler. This predates 0.2 and was carried over by mistake. | Fixed the reference. |

None of the 22 numbered rows above touch `Memory`, `CPU`, or `VM` as
implemented in Phase 1: #1-#4 and #20 affect only the *prose description*
of state Phase 1 already initializes correctly (verified against
`cpu_init`/`mem_init`); everything else belongs to Phases 2, 4, 6, 7, 9, or
10, none of which have been implemented yet. `stack_limit` (#3/#4) is a new
piece of required VM-level state, but it's needed starting in Phase 4
(`PUSH`/`POP`), not Phase 1 - see the updated §15.

---

## 1. Overview

ByteVM is a 16-bit register-based virtual machine, implemented from scratch
in C17. It consists of five independently testable components:

1. **CPU + Memory core** - the emulated hardware
2. **Instruction Set Architecture (ISA)** - the machine language
3. **Assembler** - text (`.asm`) → bytecode (`.bvm`)
4. **Disassembler** - bytecode (`.bvm`) → text
5. **Debugger** - interactive inspection and step execution

This document is the single source of truth for the ISA, memory model,
encoding format, and file formats. All implementation phases must conform to
it. If an implementation detail forces a change here, update this document
first, then the code.

### 1.1 Goals

- A flat, deterministic, fully-specified 16-bit machine.
- A real (if small) instruction encoding scheme with addressing modes -
  not just "one opcode = one enum value copied into memory."
- Bytecode that is a real binary file format with a header, not a raw dump.
- Enough instructions to write actual programs: arithmetic, bitwise ops,
  conditional branching, function calls, array/pointer access, and console
  I/O.
- Every component independently unit-testable.

### 1.2 Non-goals (explicitly out of scope for v1.0)

- Virtual memory / paging / memory protection.
- Multi-threading or interrupts beyond simple synchronous syscalls.
- Floating point.
- Dynamic linking, multiple compilation units at the bytecode level.
- A general-purpose optimizing compiler front end (only a straightforward
  two-pass assembler).
- **Signed integer division/modulo.** `DIV`/`MOD` operate on unsigned 16-bit
  values only (§8.2). A signed variant is a possible v1.1 addition, not a
  v1.0 gap.
- **Computed or indirect jump targets.** Every `JMP`/`Jcc`/`CALL` operand
  must be a compile-time-resolvable absolute address (`MEM_DIRECT` only,
  §7.3). Jump tables / function pointers are out of scope for v1.0.
- **Guaranteed termination.** Nothing prevents a program (crafted, fuzzed,
  or buggy) from looping forever; the VM's job is to behave deterministically
  and never corrupt memory or crash the host, not to detect infinite loops
  (§14).

---

## 2. Architecture Overview

```
┌─────────────────────────────────────────────┐
│                     VM                       │
│  ┌───────────────┐        ┌────────────────┐ │
│  │      CPU       │        │     Memory     │ │
│  │  R0..R7  (16b) │◄──────►│  64 KB, flat   │ │
│  │  PC, SP        │        │  byte-addressed│ │
│  │  FLAGS         │        └────────────────┘ │
│  └───────────────┘                            │
│         │ fetch/decode/execute loop           │
└─────────┼─────────────────────────────────────┘
          ▼
     halted: bool
```

The VM owns one CPU and one Memory instance. There is exactly one program
counter and one call/data stack; there is no notion of processes or threads.

### 2.1 Instruction Cycle

Every step of the fetch/decode/execute loop (Phase 2) follows the same
three-part cycle, and the exact ordering matters for anything that reads
`PC` as part of an instruction's own semantics (namely `CALL`, §8.6):

1. **Fetch:** read the opcode byte, then the mode byte, then every operand
   byte the mode byte specifies (§7) - in that order. `PC` advances by one
   for *each* byte read as it is read. By the time fetch finishes, `PC`
   already holds the address of the instruction that follows the one just
   fetched, not the one just fetched.
2. **Decode:** interpret the fetched bytes per §7's encoding rules,
   validating the opcode, the mode nibbles (§7.1, including the `0-7`
   register-index range, and §7.3's legality table) before execute runs.
   A decode failure halts the VM with the appropriate `VmResult` (§9)
   without executing anything.
3. **Execute:** perform the semantics in §8 using the *already-advanced*
   `PC` from step 1 as "the next instruction" wherever semantics refer to
   it. This is what `CALL` pushes as its return address - always the
   address of the instruction immediately following the `CALL`, regardless
   of `CALL`'s own encoded length - and what `JMP`/`Jcc` overwrite.

If an instruction's encoded bytes would extend past address `0xFFFF`,
fetching wraps to `0x0000`, consistent with §3's general address-wraparound
rule; there is no special-cased fetch error for crossing the top of memory.
(In practice this only happens if a jump or call target deliberately places
`PC` near the top of memory - a normally-assembled, sequentially-executing
program won't reach there on its own.)

---

## 3. Data Types & Endianness

- All registers are `uint16_t`.
- Memory is a flat array of `uint8_t[65536]`.
- **All multi-byte values (16-bit immediates, addresses) are stored and read
  in little-endian order.** Low byte at the lower address.
- Signed arithmetic uses two's-complement interpretation of the same 16-bit
  value (no separate signed register type).

```c
uint16_t mem_read16(const Memory *m, uint16_t addr) {
    return (uint16_t)(m->data[addr] | (m->data[(uint16_t)(addr + 1)] << 8));
}

void mem_write16(Memory *m, uint16_t addr, uint16_t value) {
    m->data[addr]                    = (uint8_t)(value & 0xFF);
    m->data[(uint16_t)(addr + 1)]    = (uint8_t)(value >> 8);
}
```

Address arithmetic wraps modulo 65536 (16-bit address space) rather than
being treated as undefined behavior. This is intentional and load-bearing
for `mem_read16`/`mem_write16` above - but see §6 for why the stack's
initial `SP` value is chosen specifically to make this wraparound
unreachable during normal stack use.

---

## 4. Register Set

| Register | Width | Purpose                                   |
|----------|-------|--------------------------------------------|
| R0–R7    | 16b   | General purpose. R0 also holds syscall args/return values. |
| PC       | 16b   | Program Counter - address of the next instruction to fetch (see §2.1 for exactly when it advances). |
| SP       | 16b   | Stack Pointer - address of the *next free* stack slot (empty-descending; stack grows down). The most recently pushed value lives at `SP + 2`, not at `SP` - see §6. |
| FLAGS    | 16b   | Status flags (see §5). Only the low 4 bits are defined; the rest are reserved and must read as 0. |

```c
typedef struct {
    uint16_t r[8];
    uint16_t pc;
    uint16_t sp;
    uint16_t flags;
} CPU;
```

**Initial state** (immediately after `cpu_init`/`vm_init`, before any
program is loaded): `R0`-`R7` = `0`, `PC` = `0`, `SP` = `0xFFFE` (§6),
`FLAGS` = `0`. Loading a program (§10) subsequently overwrites `PC` with
the header's `entry_point` - the `PC = 0` above is a reset value, not the
address execution actually begins at once a program is loaded. (Verified
against the Phase 1 implementation: `cpu_init` already does exactly this.)

---

## 5. FLAGS Register

| Bit | Name | Meaning                                              |
|-----|------|-------------------------------------------------------|
| 0   | Z    | Zero - set if the result of the last flag-affecting op was 0 |
| 1   | C    | Carry - set on unsigned overflow (`ADD`) / borrow (`SUB`, `CMP`) / unsigned truncation (`MUL`), or the bit shifted out (`SHL`/`SHR`) |
| 2   | S    | Sign - set to the high bit (bit 15) of the result (negative in two's-complement) |
| 3   | O    | Overflow - set on signed overflow. `ADD`, `SUB`/`CMP`, and `MUL` each have their own precise formula in §8.2 - they are three different computations, not variations on one rule. |

Instructions that **set** flags: `ADD, SUB, MUL, DIV, MOD, AND, OR, XOR, NOT,
SHL, SHR, CMP`.
Instructions that **do not touch** flags: `MOV, LOAD, STORE, PUSH, POP, JMP,
J*, CALL, RET, SYS, NOP, HALT`.

`DIV`/`MOD` set `Z` and `S` only; `C` and `O` are left at whatever value they
already held (not cleared, not recomputed) - see §8.2.

Conditional jumps derive their condition from FLAGS. Signed comparisons use
`S`/`O` (meaningful because `CMP` computed a two's-complement subtraction);
unsigned comparisons use `C` alone (meaningful because `CMP`'s borrow is a
pure unsigned relation). The unsigned mnemonics intentionally mirror x86's
`JB`/`JAE`/`JA`/`JBE` naming rather than inventing new terminology:

| Mnemonic | Condition             | Meaning (after `CMP a, b`) |
|----------|------------------------|------------------------------|
| JZ       | Z == 1                 | a == b                       |
| JNZ      | Z == 0                 | a != b                       |
| JG       | Z == 0 and S == O      | a > b, signed                |
| JGE      | S == O                 | a >= b, signed                |
| JL       | S != O                 | a < b, signed                |
| JLE      | Z == 1 or S != O       | a <= b, signed                |
| JB       | C == 1                 | a < b, unsigned                |
| JAE      | C == 0                 | a >= b, unsigned               |
| JA       | C == 0 and Z == 0      | a > b, unsigned                |
| JBE      | C == 1 or Z == 1       | a <= b, unsigned               |

---

## 6. Memory Model

```
0x0000 ─────────────────────  Code section (loaded from .bvm)
       ...
       ─────────────────────  Data section (loaded from .bvm)
       ...
       ─────────────────────  Free / heap-like scratch space
       ...
0xFFFE ─────────────────────  Stack top (SP starts here, grows downward)
0xFFFF ─────────────────────  Written by the first push; see below
```

- There is no fixed boundary between code/data/stack; the loader places code
  and data at the offsets given in the `.bvm` header (§10), and `SP` is
  initialized to **`0xFFFE`** at startup.
- **This is an empty-descending stack:** `SP` always holds the address of
  the *next free* slot, not the address of the most recently pushed value.
  `PUSH`/`CALL` (§8.1, §8.6) write a 16-bit value at the current `SP` and
  only decrement `SP` afterward - so once anything has been pushed, the
  top live value sits at `SP + 2`, not at `SP`. `POP`/`RET` are the mirror
  image: increment `SP` first, then read.
- **Why `0xFFFE` and not `0xFFFF`:** if `SP` started at `0xFFFF`, the very
  first `PUSH`/`CALL` would write at `0xFFFF` and `0x10000 mod 65536 =
  0x0000` - silently overwriting the first byte of the code section.
  Starting at `0xFFFE` means every stack write for the lifetime of the
  program stays at or below `0xFFFE..0xFFFF` and only ever decreases from
  there; the top-of-address-space wraparound described in §3 is never
  reached by stack operations. `0xFFFF` itself *is* written - by the very
  first push, as the high byte of that value - it is simply never the
  value `SP` holds *before* a write, which is the property that actually
  matters.
- Array and pointer-style access into the data/scratch region use
  `LOAD`/`STORE` with `REG_INDIRECT` addressing (§7.1, §8.1) - a register
  holds the address, so it can be computed at runtime. The VM additionally
  does **not** prevent self-modifying code (writing into the code region);
  this remains allowed by design, but is no longer the only way to get
  runtime-computed memory access, and no example relies on it.
- **Bounds checking:** every memory access is masked into the valid 16-bit
  range implicitly (since the address type is `uint16_t`, it cannot exceed
  the array), but stack operations must explicitly check for:
  - **Stack overflow.** Let `stack_limit = max(code_addr + code_size,
    data_addr + data_size)` - the address one past the end of whichever
    loaded region (code or data) sits highest in memory (`0` if no program
    has been loaded yet, i.e. no region is reserved). Before performing a
    `PUSH` or `CALL` (both of which write 2 bytes at the current `SP` and
    then decrement `SP` by 2), the VM halts with `VM_ERR_STACK_OVERFLOW` -
    performing neither the write nor the decrement - if either:
    - `SP < 2` (the decrement itself would wrap past address `0x0000`), or
    - `SP - 2 < stack_limit` (computed only once the above rules out
      wraparound) - the stack has grown down into the code or data region.

    `stack_limit` is derived from *both* loaded regions, not just data,
    since the loader does not guarantee data sits above code in memory.
  - **Stack underflow.** `POP`/`RET` attempted when `SP == 0xFFFE` (the
    empty-stack sentinel - this is `SP`'s value whenever nothing is
    currently pushed, per the empty-descending convention above, not the
    address of a pushed value).

  On either condition, the VM sets `halted = true` and reports
  `VM_ERR_STACK_OVERFLOW` / `VM_ERR_STACK_UNDERFLOW` rather than corrupting
  memory silently.

  `stack_limit` is new required VM-level state, introduced by this
  boundary check - it isn't part of `CPU` or `Memory` as implemented in
  Phase 1, and isn't needed until Phase 4 (`PUSH`/`POP`). Before a program
  is loaded (i.e. before Phase 6's loader runs), or in Phase 4's own unit
  tests written before Phase 6 exists, it defaults to `0`.

---

## 7. Instruction Encoding

Every instruction is **variable-length**, built from:

```
[ opcode: 1 byte ][ mode byte: 1 byte ][ operand bytes: 0-4 bytes ]
```

### 7.1 Mode byte

The mode byte encodes the addressing mode of up to two operands as two
4-bit nibbles:

```
 bit:   7 6 5 4   3 2 1 0
        └──┬──┘   └──┬──┘
       operand 1   operand 2
```

| Mode value | Name           | Meaning                                   | Bytes consumed |
|------------|----------------|---------------------------------------------|----------------|
| 0x0        | NONE           | Operand not used by this instruction        | 0              |
| 0x1        | REG            | Register index (0–7)                        | 1              |
| 0x2        | IMM16          | 16-bit immediate constant, little-endian    | 2              |
| 0x3        | MEM_DIRECT     | 16-bit absolute memory address, little-endian | 2            |
| 0x4        | REG_INDIRECT   | 1-byte register index; the register's *value* is used as the memory address | 1 |
| 0x5        | IMM8           | 8-bit immediate constant (no sign/zero extension implied - consumers interpret the byte per their own instruction, e.g. `SYS`'s syscall code) | 1 |

Values `0x6`–`0xF` are reserved and undefined. `instruction_decode` must
reject any operand whose mode nibble is one of these with
`VM_ERR_INVALID_MODE` - the same error used when a mode is well-formed but
not legal for that particular instruction/operand (§7.3). There is
deliberately no separate error code for "syntactically unknown mode" vs.
"semantically illegal mode here": both mean the same thing to a caller -
this bytecode cannot be executed as-is.

`REG` and `REG_INDIRECT` each consume one full byte for the register index
even though only 3 bits (`0-7`) are architecturally meaningful - the
encoding is byte-aligned throughout for simplicity, so values `8`-`255` are
representable in the bitstream but not valid. `instruction_decode` must
reject an out-of-range register index with `VM_ERR_INVALID_MODE` as well -
the same code, for the same underlying reason: encodable-but-meaningless
bytecode.

Operand bytes appear in order: operand 1's bytes first, then operand 2's.

### 7.2 Worked examples

```
MOV R0, 10          ; opcode=0x01, mode=0x12 (op1=REG, op2=IMM16)
  → 01 12 00 0A 00
     │  │  │  └──┴─ 10 (little-endian imm16)
     │  │  └──────── R0
     │  └─────────── mode byte (0x1 << 4 | 0x2)
     └────────────── MOV opcode

ADD R0, R1          ; opcode=0x02, mode=0x11 (both REG)
  → 02 11 00 01

LOAD R2, [0x2000]    ; opcode=0x0D, mode=0x13 (op1=REG, op2=MEM_DIRECT)
  → 0D 13 02 00 20

LOAD R0, [R1]        ; opcode=0x0D, mode=0x14 (op1=REG, op2=REG_INDIRECT)
  → 0D 14 00 01
     │  │  │  └───── R1 (address comes from the value in R1 at run time)
     │  │  └──────── R0 (destination)
     │  └─────────── mode byte (0x1 << 4 | 0x4)
     └────────────── LOAD opcode
  ; this is the array/pointer-indexing primitive: R1 holds a
  ; runtime-computed address (e.g. a base plus a loop index).

STORE [R1], R0       ; opcode=0x0E, mode=0x41 (op1=REG_INDIRECT, op2=REG)
  → 0E 41 01 00

SYS PRINT_INT        ; opcode=0x1F, mode=0x50 (op1=IMM8, op2=NONE)
  → 1F 50 01
     │  │  └───────── syscall code 0x01 (PRINT_INT), ONE byte, not two
     │  └──────────── mode byte (0x5 << 4 | 0x0)
     └─────────────── SYS opcode (renumbered from 0x1B, see §0)

HALT                 ; opcode=0xFF, mode=0x00, no operands
  → FF 00
```

This scheme is a small, honest analog of real ISA design (compare to x86's
ModRM byte) and is what makes the disassembler (§12) a meaningful,
round-trip-able component rather than a trivial lookup.

### 7.3 Addressing mode legality by instruction

Encoding-scheme flexibility does not mean every mode makes sense for every
operand - `ADD`'s destination being an immediate is nonsensical (nothing to
write the result into), even though the mode byte can represent it. The
table below is normative: it is the complete list of legal `(op1, op2)`
mode combinations per instruction. Both the assembler (statically, at
assemble time) and `instruction_decode` (dynamically, against arbitrary
bytecode) must reject anything not listed here with `VM_ERR_INVALID_MODE`.

| Mnemonic(s) | op1 legal modes | op2 legal modes |
|---|---|---|
| `NOP`, `RET`, `HALT` | NONE | NONE |
| `MOV` | REG | REG, IMM16 |
| `ADD`, `SUB`, `MUL`, `DIV`, `MOD`, `AND`, `OR`, `XOR`, `SHL`, `SHR` | REG | REG, IMM16 |
| `NOT` | REG | NONE |
| `LOAD` | REG | MEM_DIRECT, REG_INDIRECT |
| `STORE` | MEM_DIRECT, REG_INDIRECT | REG |
| `PUSH` | REG, IMM16 | NONE |
| `POP` | REG | NONE |
| `CMP` | REG, IMM16 | REG, IMM16 |
| `JMP`, `JZ`, `JNZ`, `JG`, `JGE`, `JL`, `JLE`, `JB`, `JAE`, `JA`, `JBE`, `CALL` | MEM_DIRECT | NONE |
| `SYS` | IMM8 | NONE |

Rationale for the two entries most likely to be questioned:
- `STORE`'s source is `REG`-only (no `STORE [addr], 42`-style immediate
  store) - this matches the original v0.1 intent and keeps the legality
  table symmetric with `LOAD`; it can be relaxed in a later version without
  breaking anything already built against v1.0.
- `JMP`/`Jcc`/`CALL` are `MEM_DIRECT`-only, not `REG_INDIRECT` - per the
  non-goal in §1.2, computed jumps are deliberately deferred.

---

## 8. Instruction Set Reference

Opcodes are grouped by category. "Flags" column lists flags **set**;
`-` means unaffected.

### 8.1 Data movement

| Mnemonic | Opcode | Operands       | Semantics                | Flags |
|----------|--------|----------------|---------------------------|-------|
| NOP      | 0x00   | -              | No operation               | -     |
| MOV      | 0x01   | dst, src       | dst = src                  | -     |
| LOAD     | 0x0D   | dst(REG), addr | dst = mem16[addr]. `addr` may be `MEM_DIRECT` (a literal/label) or `REG_INDIRECT` (a register holding the address) - see §7.2/§7.3. | -     |
| STORE    | 0x0E   | addr, src(REG) | mem16[addr] = src. `addr` may be `MEM_DIRECT` or `REG_INDIRECT`, same as `LOAD`. | -     |
| PUSH     | 0x0F   | src            | mem16[SP] = src; SP -= 2 (overflow-checked first - see §6) | -     |
| POP      | 0x10   | dst(REG)       | SP += 2; dst = mem16[SP] (underflow-checked first - see §6) | -     |

### 8.2 Arithmetic

| Mnemonic | Opcode | Operands  | Semantics            | Flags        |
|----------|--------|-----------|------------------------|--------------|
| ADD      | 0x02   | dst, src  | dst = dst + src         | Z, C, S, O   |
| SUB      | 0x03   | dst, src  | dst = dst - src         | Z, C, S, O   |
| MUL      | 0x04   | dst, src  | dst = dst * src (low 16 bits kept) - see the dedicated flag formula below | Z, C, S, O |
| DIV      | 0x05   | dst, src  | dst = dst / src (unsigned). `src == 0` → `VM_ERR_DIV_BY_ZERO`, VM halts | Z, S |
| MOD      | 0x06   | dst, src  | dst = dst % src. Same div-by-zero rule as DIV | Z, S |

**`ADD` flag formula:**
```
sum    = (uint32_t)dst + (uint32_t)src   ; unsigned 32-bit, no truncation yet
result = (uint16_t)sum
C      = (sum >> 16) != 0                        ; unsigned carry out
O      = ((dst ^ result) & (src ^ result) & 0x8000) != 0
         ; both operands share a sign bit the result doesn't
Z      = (result == 0)
S      = bit 15 of result
```

**`SUB`/`CMP` flag formula** (`CMP a, b` computes exactly this and discards
`result`; `SUB dst, src` is `a = dst, b = src`):
```
result = (uint16_t)(a - b)                       ; wrapping
C      = a < b                                    ; unsigned borrow
O      = ((a ^ b) & (a ^ result) & 0x8000) != 0
         ; operands differ in sign, and result's sign doesn't match a's
Z      = (result == 0)
S      = bit 15 of result
```

**`MUL` flag formula** - this is deliberately its own computation, not a
reuse of `ADD`/`SUB`'s overflow check (different math: a product overflowing
16 bits is not the same condition as a sum overflowing 16 bits) and not a
reuse of the plain `Z`/`S`-only logic-op flags (`MUL` also defines `C`/`O`,
unlike `AND`/`OR`/`XOR`/`NOT`):

```
product         = (uint32_t)dst * (uint32_t)src        ; unsigned 32-bit
result          = (uint16_t)(product & 0xFFFF)          ; low 16 bits kept
C               = (product >> 16) != 0                   ; unsigned truncation occurred
signed_product  = (int32_t)(int16_t)dst * (int32_t)(int16_t)src
O               = signed_product < INT16_MIN or signed_product > INT16_MAX
Z               = (result == 0)
S               = bit 15 of result
```

**`DIV`/`MOD`:** unsigned only (see §1.2 non-goal). `Z` and `S` are
recomputed from the result as usual; `C` and `O` are **left unmodified** -
not cleared, not recomputed - since neither has a meaningful definition for
unsigned division.

### 8.3 Bitwise

| Mnemonic | Opcode | Operands  | Semantics       | Flags  |
|----------|--------|-----------|-------------------|--------|
| AND      | 0x07   | dst, src  | dst = dst & src    | Z, S   |
| OR       | 0x08   | dst, src  | dst = dst \| src   | Z, S   |
| XOR      | 0x09   | dst, src  | dst = dst ^ src    | Z, S   |
| NOT      | 0x0A   | dst       | dst = ~dst         | Z, S   |
| SHL      | 0x0B   | dst, src  | see shift-count table below | Z, C, S |
| SHR      | 0x0C   | dst, src  | see shift-count table below (logical, not arithmetic) | Z, C, S |

`src` is the shift count `n`. Implementations must branch on the range of
`n` explicitly below rather than executing a native C `<<`/`>>` with an
unvalidated count (which risks shift-count-exceeds-width UB for `n >= 16`):

| Range of `n` | SHL result | SHL sets C to | SHR result | SHR sets C to |
|---|---|---|---|---|
| `n == 0` | dst (unchanged) | 0 | dst (unchanged) | 0 |
| `1 <= n <= 16` | `n==16 ? 0 : (dst << n)`, truncated to 16 bits | bit `(16-n)` of the *original* dst | `n==16 ? 0 : (dst >> n)` | bit `(n-1)` of the *original* dst |
| `n > 16`     | 0 | 0 | 0 | 0 |

`Z`/`S` are always recomputed from whatever `result` the table above
produces (e.g. `n > 16` gives `result = 0`, so `Z = 1, S = 0`).

### 8.4 Comparison

| Mnemonic | Opcode | Operands | Semantics                        | Flags        |
|----------|--------|----------|-------------------------------------|--------------|
| CMP      | 0x11   | a, b     | computes a - b, discards the result, sets flags as SUB would (§8.2) | Z, C, S, O |

### 8.5 Control flow

| Mnemonic | Opcode | Operands | Semantics                          |
|----------|--------|----------|--------------------------------------|
| JMP      | 0x12   | addr     | PC = addr                            |
| JZ       | 0x13   | addr     | if Z: PC = addr                      |
| JNZ      | 0x14   | addr     | if !Z: PC = addr                     |
| JG       | 0x15   | addr     | if !Z and S==O: PC = addr (signed >) |
| JGE      | 0x16   | addr     | if S==O: PC = addr (signed >=)       |
| JL       | 0x17   | addr     | if S!=O: PC = addr (signed <)        |
| JLE      | 0x18   | addr     | if Z or S!=O: PC = addr (signed <=)  |
| JB       | 0x19   | addr     | if C: PC = addr (unsigned <)         |
| JAE      | 0x1A   | addr     | if !C: PC = addr (unsigned >=)       |
| JA       | 0x1B   | addr     | if !C and !Z: PC = addr (unsigned >) |
| JBE      | 0x1C   | addr     | if C or Z: PC = addr (unsigned <=)   |

### 8.6 Functions

| Mnemonic | Opcode | Operands | Semantics                                          |
|----------|--------|----------|--------------------------------------------------------|
| CALL     | 0x1D   | addr     | mem16[SP] = PC; SP -= 2 (overflow-checked, §6); PC = addr |
| RET      | 0x1E   | -        | SP += 2 (underflow-checked, §6); PC = mem16[SP]         |

`PC` in `CALL`'s semantics is the value already advanced past `CALL`'s own
encoded bytes by the fetch step (§2.1) - i.e. always the address of the
instruction immediately following the `CALL`, regardless of how many bytes
`CALL`'s own `addr` operand took to encode.

(`CALL`/`RET` moved from `0x19`/`0x1A` to make room for `JB`/`JAE`/`JA`/`JBE`
- see §0.)

**Calling convention:** arguments are passed in `R0`–`R3` (caller-saved,
i.e. the callee may freely clobber them); the return value is placed in
`R0`. There is no automatic register save/restore - callers must `PUSH` any
register they need preserved across a `CALL`.

### 8.7 System calls

| Mnemonic | Opcode | Operands   | Semantics |
|----------|--------|------------|-----------|
| SYS      | 0x1F   | code(IMM8) | Invokes a syscall, listed below. Encoded as a single `IMM8` byte (§7.1) - not a 2-byte `IMM16` with a wasted high byte. |

(`SYS` moved from `0x1B` - see §0.)

| Code | Name        | Behavior                                  |
|------|-------------|---------------------------------------------|
| 0x01 | PRINT_INT   | Print `R0` as a signed decimal integer       |
| 0x02 | PRINT_CHAR  | Print the low byte of `R0` as an ASCII char  |
| 0x03 | READ_INT    | Read a decimal integer from stdin into `R0` - see below for the exact parsing rule |
| 0x04 | EXIT        | Halt the VM; `R0` is the VM-level exit code - see below for the host mapping |

**`READ_INT`:** skip leading whitespace, then read an optional leading `-`
sign followed by one or more decimal digits, stopping at the first
character that is not a digit (that character is left on the stream
unconsumed). If no digits are found before a non-digit character or EOF,
`R0` is set to `0` and execution continues normally - malformed or absent
input is an environment condition, not a VM fault, so it does not halt the
VM. If the parsed magnitude does not fit in 16 bits (signed or unsigned),
it is truncated modulo 65536, consistent with every other 16-bit
wraparound rule in this document (§3).

**`EXIT`:** `R0` is the exit code within the VM's own 16-bit world; the
**host** process's exit status is `R0 & 0xFF` (the low 8 bits), since
POSIX exit statuses are themselves 8 bits wide - this is a truncation of
the full 16-bit `R0` for the host's benefit, not a VM-level truncation.

Any `code` not in the table above is a runtime error: the VM sets
`halted = true` and reports `VM_ERR_INVALID_SYSCALL` (§9) rather than
silently doing nothing.

### 8.8 Machine control

| Mnemonic | Opcode | Operands | Semantics            |
|----------|--------|----------|------------------------|
| HALT     | 0xFF   | -        | Sets `halted = true`   |

---

## 9. Error Model

The VM does not crash the host process on a malformed program. Every
execute-step function returns a `VmResult` enum; on any error the VM sets
`halted = true` and records the error code + the PC where it occurred:

**`error_pc` is always the address of the instruction that faulted** - the
address `PC` held *before* that instruction's fetch began, not any address
`PC` may have advanced to while decoding it. This is unambiguous by
construction: a failed decode (§2.1) never commits its `PC` advance (it
operates on a local copy of the address counter and only writes back to
`PC` on success), so `PC` is still at the faulting instruction's address
when a decode error is discovered. A failed *execute* is different - by
that point `PC` has already been advanced past the faulting instruction
(fetch always finishes before execute begins) - so `error_pc` is recorded
by the caller of decode/execute (the fetch/decode/execute driver, §2.1)
from the value `PC` held at the *start* of the step, before either ran.

```c
typedef enum {
    VM_OK = 0,
    VM_ERR_INVALID_OPCODE,
    VM_ERR_INVALID_MODE,
    VM_ERR_DIV_BY_ZERO,
    VM_ERR_STACK_OVERFLOW,
    VM_ERR_STACK_UNDERFLOW,
    VM_ERR_BAD_BYTECODE_HEADER,
    VM_ERR_INVALID_SYSCALL,
} VmResult;
```

`VM_ERR_INVALID_MODE` covers a syntactically unknown mode nibble
(`0x6`-`0xF`, §7.1), an out-of-range register index (`8`-`255`, §7.1), and a
syntactically valid mode that is not legal for the given instruction/operand
(§7.3) - all three mean the bytecode cannot be executed, and a caller
doesn't need to distinguish them.

**Stepping an already-halted VM** is a defined no-op: if `halted` is
already `true`, the execute-step function performs no fetch, no decode, no
execute, does not advance `PC`, and simply returns whatever `last_error`
currently holds (`VM_OK` if the VM halted via `HALT`/`SYS EXIT` rather than
an error). This lets the debugger's `step`/`continue` (§13) call it
repeatedly after a program finishes without separately tracking "is it
still running" themselves.

The CLI (`bytevm run`) prints a human-readable message and a non-zero exit
code when a `VmResult != VM_OK` is produced.

---

## 10. Bytecode File Format (`.bvm`)

A `.bvm` file is a fixed 16-byte header followed by a code blob and a data
blob.

| Offset | Size | Field         | Notes                                    |
|--------|------|---------------|--------------------------------------------|
| 0x00   | 4    | magic         | ASCII `"BVM1"` (0x42 0x56 0x4D 0x31)        |
| 0x04   | 1    | version       | Format version, currently `1`               |
| 0x05   | 1    | reserved      | Must be `0`                                 |
| 0x06   | 2    | entry_point   | uint16 LE - initial value of PC             |
| 0x08   | 2    | code_size     | uint16 LE - length of the code blob         |
| 0x0A   | 2    | code_addr     | uint16 LE - memory address to load code at  |
| 0x0C   | 2    | data_size     | uint16 LE - length of the data blob         |
| 0x0E   | 2    | data_addr     | uint16 LE - memory address to load data at  |
| 0x10   | ...  | code[]        | `code_size` bytes                           |
| ...    | ...  | data[]        | `data_size` bytes                           |

**Parsing requirement:** the header must be read field-by-field using the
same little-endian helpers used for ordinary memory access (§3) - e.g. read
the raw bytes into a buffer and call the equivalent of `mem_read16` at each
offset - rather than `fread`-ing directly into a `BvmHeader` C struct.
Struct padding and member alignment are implementation-defined in C17, so a
raw struct read is not guaranteed to match this byte-for-byte layout across
compilers/platforms. `BvmHeader` (below) is the *parsed, in-memory*
representation; it is never the wire format itself.

The loader rejects a file, returning `VM_ERR_BAD_BYTECODE_HEADER`, if any of
the following hold - checked before any code/data bytes are copied into VM
memory:
- `magic` or `version` don't match.
- `reserved` is non-zero.
- The file's total length is less than `16 + code_size + data_size` (the
  header claims more code/data than the file actually contains).
- `code_addr + code_size` or `data_addr + data_size` would overflow the
  64 KB address space.
- `entry_point` does not satisfy `code_addr <= entry_point < code_addr +
  code_size` (a program can't start executing outside its own code blob).
- The code range `[code_addr, code_addr + code_size)` and the data range
  `[data_addr, data_addr + data_size)` overlap.

Once a file passes all checks, the loader also computes `stack_limit =
max(code_addr + code_size, data_addr + data_size)` (§6) as part of setting
up VM state - this is not a rejection condition, just a value the loader is
responsible for deriving. Note that `code_addr + code_size` (or the data
equivalent) can legitimately equal `0x10000` - a region reaching exactly
the top of the address space, which the bounds check above allows. Since
`stack_limit` is a `uint16_t`, a naive truncation of `0x10000` wraps to
`0`, which would silently disable the overflow check entirely (§6) rather
than correctly making every `PUSH`/`CALL` overflow immediately (the
correct behavior, since no address is actually free for the stack in that
case). Implementations must clamp to `0xFFFF` instead of truncating.

---

## 11. Assembler Design

Two-pass design:

- **Pass 1 (scan):** tokenize the source, walk through it computing the
  address of every instruction and directive, and build a symbol table of
  `label → address`. No bytecode is emitted yet.
- **Pass 2 (emit):** walk through again, this time resolving every label
  reference against the symbol table built in pass 1, and emit real
  instruction bytes per §7, rejecting any operand/mode combination not
  listed in §7.3, or any immediate out of the ranges in §11.4, with a
  compile-time error (don't wait for the VM to catch it at decode time).

### 11.1 Syntax

```asm
; a comment runs to end of line
.org 0x0000            ; set the current address
start:
    MOV R0, 5
    MOV R1, 10
    ADD R0, R1
    CALL print_result
    HALT

print_result:
    SYS PRINT_INT       ; symbolic syscall names allowed
    RET

.data
message:
    .string "done\n"
count:
    .word 42
```

Square brackets denote a memory operand; what's *inside* the brackets
decides the addressing mode:

```asm
LOAD R0, [0x2000]      ; MEM_DIRECT -- a numeric literal
LOAD R0, [message]     ; MEM_DIRECT -- a label, resolved by pass 2
LOAD R0, [R1]           ; REG_INDIRECT -- a register name: address = value in R1
STORE [R1], R0          ; REG_INDIRECT destination
```

### 11.2 Directives

| Directive  | Meaning                                          |
|------------|-----------------------------------------------------|
| `.org N`   | Set the *current section's* address counter to `N` (see "Section model" below) |
| `.data`    | Switch subsequent items into the data section        |
| `.string`  | Emit a NUL-terminated ASCII string (see escape sequences below) |
| `.word N`  | Emit a single 16-bit little-endian value              |
| `.byte N`  | Emit a single raw byte, `N` in `0-255`                |

`.byte` exists specifically so the assembler can consume the disassembler's
own `.byte` fallback output (§12) - without it, `bytecode → disassembler →
assembler` cannot always reproduce the original bytes, contradicting the
round-trip requirement in §12/§14.

**Section model:** there are exactly two ordered sections - *code* (active
from the start of the file) and *data* (active after `.data`, which may
appear at most once and cannot be reversed - there is no `.code` directive
to switch back). Each section has its own address counter. The code
counter starts at `0x0000`; **the data counter starts at whatever address
the code section had reached when `.data` was encountered**, so data is
laid out immediately after code by default. (Without this rule the data
counter would independently start at `0x0000` and overlap the code - which
§10's loader rejects as overlapping regions, or which silently corrupts
the program when the sizes happen to line up. This was a real bug caught
during Phase 7 implementation.) An explicit `.org` after `.data` can still
move the data section further forward.

`.org N` repositions the address counter of whichever section is currently
active; within one section, `N` must be greater than or equal to that
section's current counter. An `.org` that would move the counter backward
- and thus overlap bytes already emitted in that section - is an assembler
error, not silently allowed.

**`.string` escape sequences:** `\n`, `\r`, `\t`, `\\`, `\"`, `\0`, and
`\xNN` (a two-hex-digit byte literal) are supported. Any other backslash
sequence is an assembler error.

### 11.3 Error reporting

Every syntax or semantic error (unknown mnemonic, undefined label,
register out of range, immediate out of range, illegal addressing mode for
an operand per §7.3, a backward `.org`, an unrecognized `.string` escape)
is reported as `file:line: error: <message>` and aborts assembly with a
non-zero exit code - no partial `.bvm` is written on error.

### 11.4 Immediate ranges and signedness

An `IMM16` literal is accepted if its value fits `[-32768, 65535]` when
read as *either* a signed two's-complement 16-bit integer or an unsigned
16-bit integer. These ranges overlap in their bit patterns by design -
`-1` and `65535` both assemble to the same bytes, `0xFFFF` - since the VM
itself has no separate signed register type (§3) and interprets every
16-bit value however the consuming instruction needs to. A literal outside
`[-32768, 65535]` (e.g. `65536`, `-32769`) is an assembler error.

An `IMM8` literal - currently only used by `SYS`'s syscall code (§7.1,
§8.7) - is unsigned-only: `[0, 255]`. Negative literals are not accepted
for `IMM8` operands, since no `SYS` code has a signed interpretation.

---

## 12. Disassembler Design

Given a `.bvm` file, the disassembler:

1. Validates and parses the header (§10).
2. Walks the code blob starting at `entry_point`, decoding one instruction
   at a time using the exact inverse of §7's encoding rules (including the
   `IMM8` mode and `REG_INDIRECT` on `LOAD`/`STORE`).
3. Prints `<address>: <mnemonic> <operands>` per instruction, and falls
   back to printing a raw byte with a `.byte` marker (§11.2) for any
   address it cannot decode as a valid instruction (e.g. inline data mixed
   into code), advancing by exactly one byte before retrying.

Distinguishing genuine code from data embedded in the code region is, in
general, undecidable for a purely linear disassembler without extra
metadata - real-world disassemblers have the same limitation, and this
design does not attempt to solve it. The `.byte` fallback above is a
mechanical decision (it fires whenever decode fails), not a semantic one
(it never tries to guess "this looks like data"). That is sufficient for
the round-trip guarantee this section actually promises:

The disassembler and assembler are tested against each other directly:
every example program is round-tripped (`asm → bvm → asm → bvm`) and the
two `.bvm` outputs must be byte-identical.

One limit on that guarantee, found while implementing Phase 8: the
assembler always emits `entry_point == code_addr`, because no source
syntax exists for setting them independently. A hand-built image whose
`entry_point` points elsewhere in the code blob is perfectly legal per
§10, but has no `.asm` form - so rather than emitting text that silently
reassembles into a *different* image, the disassembler rejects such an
image with an explicit error. (A future `.entry` directive would close
this gap; it isn't needed for v1.0.) This only requires the
disassembly to be *self-consistent* (reassembling it reproduces the same
bytes) - not that it's semantically meaningful for embedded data.

---

## 13. Debugger Design

An interactive REPL wrapping the same `VM` struct used by `bytevm run`,
stepping the fetch/decode/execute loop one instruction at a time instead
of running to completion.

| Command             | Effect                                            |
|----------------------|------------------------------------------------------|
| `registers`          | Print R0–R7, PC, SP, FLAGS                           |
| `step [n]`           | Execute `n` instructions (default 1)                  |
| `continue`           | Run until a breakpoint or HALT                        |
| `break <addr>`       | Set a breakpoint at `addr`                             |
| `delete <addr>`      | Remove a breakpoint                                    |
| `memory <addr> [n]`  | Hex-dump `n` bytes (default 16) starting at `addr`     |
| `stack`              | Dump memory from `SP` to `0xFFFF` (the empty-descending convention means `SP` itself is the next free slot, not the top live value - see §4/§6) |
| `disassemble <addr> [n]` | Disassemble `n` instructions from `addr`           |
| `reset`              | Reload the program and reset CPU state (see below)     |
| `quit`               | Exit the debugger                                      |

**`continue` from a breakpoint:** stepping is unconditional for the first
instruction of a `continue`, and only then does breakpoint checking
resume. Without that, `continue` while already stopped at a breakpoint
would re-trigger the same breakpoint immediately and never make progress.

**Breakpoint timing:** a breakpoint fires *before* the instruction at that
address executes - `continue`/`step` stop with `PC` equal to the
breakpoint address and that instruction not yet fetched, the same point at
which a fresh `step` would begin (matching conventional debugger semantics,
e.g. GDB). A breakpoint on a `CALL`'s own address stops before the call
happens, not after it returns.

**`reset` semantics:** `reset` re-runs the full load sequence (§10) against
the *original* `.bvm` file - memory is zeroed and the code/data blobs are
re-copied to their header-specified addresses exactly as at first load,
discarding any runtime writes (including self-modifying code and anything
pushed to the stack). The CPU is reset to the Initial State in §4, and then
`PC` is set to `entry_point`, exactly as a fresh load would. `halted` /
`last_error` / `error_pc` are cleared. Breakpoints are debugger-session
state, not VM state, and are **not** cleared by `reset`.

---

## 14. Testing Strategy

- **Unit tests** (`tests/unit/test_*.c`), one file per module, using a
  minimal header-only assert macro (no external test framework dependency).
  Each arithmetic/bitwise instruction gets at least one test that checks
  both the result *and* the resulting FLAGS value, including the edge
  cases called out explicitly in this document: `ADD`/`SUB`/`CMP`'s
  boundary cases (`0xFFFF + 1`, `0x8000 + 0x8000`, `0x0000 - 1`, `0x8000 -
  1`), `MUL`'s truncation vs. signed-overflow formula (§8.2), and
  `SHL`/`SHR` at `n == 0` and `n >= 16` (§8.3).
- **Test harness shape (avoids a duplicate-`main` link error):** exactly
  one file, `tests/test_main.c`, may define `main`. Every
  `tests/unit/test_<module>.c` instead exposes a plain function - e.g.
  `int test_<module>(void)` returning a failure count - that
  `tests/test_main.c` calls and aggregates. All `test_*.c` files and
  `tests/test_main.c` are linked into a single `run_tests` binary; if any
  test file under `tests/unit/` defines its own `main`, that's a bug in the
  test file, not something the build should route around.
- **Golden-file / regression tests:** each program under `examples/` has an
  expected stdout and expected exit code checked in next to it; a test
  runner assembles, runs, and diffs the output. Once `LOAD`/`STORE` with
  `REG_INDIRECT` land (§7.3), this is where an actual array-indexing
  example (e.g. bubble sort) belongs - it was a known placeholder in the
  original scaffold specifically because the addressing mode it needs
  wasn't wired up yet.
- **Round-trip tests:** disassembling every example's compiled `.bvm` and
  reassembling it must reproduce the same bytes (see §12), including at
  least one example that deliberately embeds a non-decodable byte to
  exercise `.byte` (§11.2) on both sides.
- **Fuzz-ish robustness test:** feed the VM loader random byte sequences
  and assert it never crashes - only ever returns a `VmResult` error or
  runs to `HALT`, **within a fixed step budget** (e.g. 1,000,000
  fetch/decode/execute steps) per fuzz iteration. Exceeding the budget
  counts as a pass, not a failure: the goal is memory safety and
  determinism, not termination, and nothing about this VM - or arbitrary
  bytecode fed to it - guarantees a program halts (see the non-goal in
  §1.2).

---

## 15. Project Phases

| Phase | Deliverable |
|-------|-------------|
| 0 | This design document (now v0.3) + `docs/instruction-set.md` reference table |
| 1 | *(done)* CPU + Memory structs, 8/16-bit read/write helpers, `vm_init` (SP = `0xFFFE`, full initial-state contract per §4) |
| 2 | *(done)* Fetch/decode/execute loop implementing the instruction cycle in §2.1 (including PC-advances-during-fetch and end-of-memory wraparound); `instruction_decode` enforcing §7.3's legality table and the `0-7` register-index check (§7.1); the already-halted-VM no-op contract (§9); MOV, ADD, SUB, LOAD, STORE, HALT via hand-written bytecode arrays |
| 3 | *(done)* Full arithmetic/bitwise instruction set + FLAGS semantics, including the explicit `ADD`/`SUB`/`MUL` formulas (§8.2) and the shift-count table (§8.3). Also implemented `CMP` (§8.4) here rather than in a later phase - it reuses `SUB`'s exact flag formula with no register write-back, so it was effectively free once `SUB` existed, and Phase 4's conditional jumps need it to be testable without a workaround |
| 4 | *(done)* Control flow (`JMP` + all eight conditional jumps, signed and unsigned) and stack (`PUSH`/`POP`) with the exact overflow/underflow boundary formula in §6 - this introduces `stack_limit` as new VM-level state, defaulting to `0` before Phase 6's loader exists |
| 5 | *(done)* `CALL`/`RET` calling convention (return address per §2.1's instruction-cycle definition) + `SYS` syscalls for I/O, including `VM_ERR_INVALID_SYSCALL`, `READ_INT`'s parsing rule, and `EXIT`'s host-exit-code mapping (§8.7). This completes the full ISA - every opcode now has real execution semantics |
| 6 | *(done)* `.bvm` file format + loader with full header validation (magic/version/reserved/file-length/bounds/entry-point-in-range/no-overlap, §10), parsed field-by-field; loader also computes and sets `stack_limit`, clamped to `0xFFFF` rather than wrapped when a loaded region reaches exactly the top of the address space (§10) |
| 7 | *(done)* Assembler (lexer, two-pass parser, directives including `.byte`, labels, `[Rn]` vs `[addr]` operand syntax, static §7.3/§11.4 rejection, the §11.2 section/`.org` model, `.string` escapes). Codegen validates operand modes by calling into the *same* §7.3 table `instruction_decode` uses (`instruction_opcode_modes`), so the rules live in exactly one place. Also delivered the `bytevm run` / `bytevm asm` CLI and three real example programs |
| 8 | *(done)* Disassembler + round-trip tests against the assembler (§12). Decoding reuses `instruction_decode` verbatim against a `Memory` image rather than reimplementing §7's encoding rules, so the two can't drift. Two output rules matter for the round trip: `MEM_DIRECT` is bracketed only where `REG_INDIRECT` is also legal (i.e. `LOAD`/`STORE`, the one place brackets carry information - jump/`CALL` targets are bare), and data blobs are emitted as raw `.byte` rather than guessed back into `.string`/`.word` |
| 9 | *(done)* Interactive debugger (REPL: step/break/continue/memory/stack), implementing the before-execution breakpoint timing and full `reset` contract in §13. Command execution is split from the input loop (`debugger_execute` takes its output stream as a parameter) so the whole command set is unit-testable without a terminal. `reset` is `vm_load` against the retained original image, so it discards self-modifying-code changes exactly as a fresh load would. The `disassemble` command shares the Phase 8 formatter via `disassemble_one`, so debugger output and `bytevm dis` output can't drift apart |
| 10 | *(done)* Full test suite (13 modules, single-`main` harness, fuzz step budget from §14), a GitHub Actions CI pipeline (`make`/`cmake`/`clang-format` matrix), golden-file regression tests for all four `examples/` programs, a fourth example (`sort.asm`, bubble sort - the array-indexing case §14 asks for, and the exact placeholder the original v0.1 scaffold left empty before `REG_INDIRECT` existed), and README polish |

---

## 16. Appendix - Full Opcode Table

| Hex  | Mnemonic |
|------|----------|
| 0x00 | NOP      |
| 0x01 | MOV      |
| 0x02 | ADD      |
| 0x03 | SUB      |
| 0x04 | MUL      |
| 0x05 | DIV      |
| 0x06 | MOD      |
| 0x07 | AND      |
| 0x08 | OR       |
| 0x09 | XOR      |
| 0x0A | NOT      |
| 0x0B | SHL      |
| 0x0C | SHR      |
| 0x0D | LOAD     |
| 0x0E | STORE    |
| 0x0F | PUSH     |
| 0x10 | POP      |
| 0x11 | CMP      |
| 0x12 | JMP      |
| 0x13 | JZ       |
| 0x14 | JNZ      |
| 0x15 | JG       |
| 0x16 | JGE      |
| 0x17 | JL       |
| 0x18 | JLE      |
| 0x19 | JB       |
| 0x1A | JAE      |
| 0x1B | JA       |
| 0x1C | JBE      |
| 0x1D | CALL     |
| 0x1E | RET      |
| 0x1F | SYS      |
| 0xFF | HALT     |

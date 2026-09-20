# Changelog

All notable changes to this project are documented here.
Format loosely follows [Keep a Changelog](https://keepachangelog.com/).

## [Unreleased]
- **Phase 10 - v1.0.** The last phase per `docs/DESIGN.md` §15:
  - Golden-file regression tests (`tests/golden/*.expected` + `*.exit`,
    `tests/unit/test_golden.c`): each `examples/*.asm` program is
    assembled, run, and diffed against a captured-known-good stdout and
    exit code, per §14.
  - A fourth example, `examples/sort.asm` - bubble sort on a 6-element
    array. This is, deliberately, the exact placeholder the *original*
    v0.1 scaffold left as an empty `sort.asm` because `REG_INDIRECT`
    wasn't wired up to `LOAD`/`STORE` yet (see `DESIGN.md` §0, revision
    0.1 → 0.2, item 4) - it closes a loop that started at the very first
    design review of this project.
  - A fuzz harness (`tests/unit/test_fuzz.c`) with the fixed step budget
    §14 requires: 2000 pure-random buffers (exercising the loader's
    rejection paths), 500 structurally-valid-header-but-random-body
    buffers (exercising `instruction_decode`/`instruction_execute`
    against arbitrary bytecode once a random header happens to pass
    validation), and one deterministic regression for the exact hang
    scenario named in the second design review: a `JMP`-to-itself program
    now provably stops after exactly `FUZZ_STEP_BUDGET` steps instead of
    looping forever.
  - CI (`.github/workflows/ci.yml`): `make build`/`make test` on gcc and
    clang, a `cmake`/`ctest` build, a `clang-format --check` job, and a
    run of every example through the CLI.
  - Fixed a real bug the CI design surfaced before ever running in CI:
    `Makefile`'s `CC := cc` used simple assignment, which unconditionally
    overrides any environment `CC` - so `CC=clang make build` silently
    kept using `cc` regardless. Changed to `CC ?= cc` so the compiler
    matrix in CI (and any local override) actually takes effect. Verified
    by building with the override before and after the fix.
  - Ran `make format` (`clang-format`) across the entire codebase for the
    first time - it had been hand-formatted to the same conventions
    throughout, so this mostly confirmed consistency rather than
    rewriting anything; verified `make build`/`make test` and all four
    examples still pass byte-for-byte identically afterward.
  - README rewritten to reflect v1.0: usage, examples table, full layout,
    and CI status.

  `make build` and `make test` verified passing (13 test modules,
  0 failures, all four examples verified against their golden files).
- Phase 9 implemented per `docs/DESIGN.md` §15: an interactive debugger
  (`include/bytevm/debugger.h`, `src/debugger.c`) with all ten §13
  commands plus `help`, and `bytevm debug <file>` in the CLI.
- Command execution is deliberately split from the input loop:
  `debugger_execute(dbg, line, out)` takes its output stream as a
  parameter, so the entire command set is unit-testable by driving it
  with strings and a `tmpfile()` - no terminal, no stdout hijacking.
- `reset` calls `vm_load` against the retained original image rather than
  approximating a reset, so it discards runtime writes (self-modifying
  code included) exactly as a fresh load would, and deliberately keeps
  breakpoints, which are debugger-session state rather than VM state.
- `continue` steps once unconditionally before it resumes checking
  breakpoints. Without that, continuing while already stopped *at* a
  breakpoint would re-trigger the same breakpoint immediately and never
  make progress. Added to §13.
- Refactored the Phase 8 disassembler to expose `disassemble_one()`, and
  rewrote `disassemble()` to use the same extracted `format_instruction`
  helper. The debugger's `disassemble` command and `bytevm dis` therefore
  share one formatter and cannot drift apart.
- New test file `test_debugger.c`: every command, argument validation,
  unknown-command handling, and two timing properties that are easy to
  get subtly wrong - a breakpoint on `SUB R1, 1` must leave `R1`
  *unchanged* when it fires (proving it stopped before execution, not
  after), and two consecutive `continue`s must land on successive loop
  iterations (proving `continue` escapes the breakpoint it starts on).
  Also covers `reset` preserving breakpoints and stepping past `HALT`
  being a safe no-op. `make build` and `make test` verified passing
  (11 test modules, 0 failures).
- Phase 8 implemented per `docs/DESIGN.md` §15: a disassembler
  (`include/bytevm/disassembler.h`, `src/disassembler.c`) turning a `.bvm`
  image back into assembly text, plus `bytevm dis <file>` in the CLI.
  Decoding reuses `instruction_decode` verbatim -- the exact decoder the
  VM runs -- against a `Memory` image, rather than reimplementing §7's
  encoding rules a second time where the two could drift apart.
- Two output rules turned out to be load-bearing for the round-trip
  guarantee, and are now documented in §15's phase row: (1) `MEM_DIRECT`
  is emitted in brackets *only* where `REG_INDIRECT` is also legal for
  that operand position -- i.e. `LOAD`/`STORE`, the one place brackets
  disambiguate anything. Jump and `CALL` targets are bare addresses;
  bracketing them produces text the assembler rejects. (2) Data blobs are
  emitted as raw `.byte` rather than guessed back into `.string`/`.word`,
  which would only reproduce the original bytes for data that happens to
  match those shapes.
- Found a limit on the round-trip guarantee while implementing this and
  recorded it in §12: the assembler always emits
  `entry_point == code_addr` since no source syntax sets them
  independently, so a hand-built image whose `entry_point` sits elsewhere
  has no `.asm` form. The disassembler now rejects such an image
  explicitly instead of emitting text that silently reassembles into a
  *different* image.
- New test file `test_disassembler.c`: 21 `asm → bvm → asm → bvm`
  byte-identical round-trip cases covering every addressing mode, all
  eight conditional jumps, bracket-vs-bare `MEM_DIRECT`, immediates at
  both ends of §11.4's range, `.string`/`.word`/`.org` data sections, and
  a complete array-summing program. Also: the `.byte` fallback for
  undecodable bytes, a truncated trailing instruction (whose operand
  bytes lie past the end of the code blob and must *not* be consumed),
  and rejection of a non-BVM1 image. All three `examples/` programs were
  additionally verified round-tripping through the CLI byte-for-byte and
  still producing correct output after reassembly. `make build` and
  `make test` verified passing (10 test modules, 0 failures).
- Phase 7 implemented per `docs/DESIGN.md` §15 - a complete two-pass
  assembler plus a real CLI. New: `src/assembler/lexer.{h,c}` (tokenizer,
  including `.string` escape processing for `\n \r \t \\ \" \0 \xNN`),
  `src/assembler/symtab.{h,c}` (label table, rejecting duplicate
  definitions), `src/assembler/parser.{h,c}` (statement IR + both passes),
  `src/assembler.{h,c}` (top level: lex → assemble → serialize a `.bvm`
  image). Codegen validates operand modes by calling the newly-exported
  `instruction_opcode_modes()` - the *same* §7.3 legality table
  `instruction_decode` enforces - so the assembler and the VM can never
  disagree about what's legal. `main.c` became a real CLI: `bytevm run
  <file.asm|file.bvm>` (assembles if needed, executes, exits with
  `R0 & 0xFF` per §8.7) and `bytevm asm <in.asm> <out.bvm>`.
- Found and fixed a genuine bug during Phase 7: the data section's address
  counter started at `0x0000` independently of the code counter, so `.data`
  content overlapped the code section - either rejected by the loader as
  overlapping regions, or silently overwriting code when sizes lined up.
  (One test initially "passed" only because the data blob happened to
  overwrite the very code bytes it then read back.) The data counter now
  defaults to wherever the code counter had reached; added to `DESIGN.md`
  §11.2 and covered by a dedicated regression test.
- Added three real example programs under `examples/`: `hello.asm`
  (string iteration via `[Rn]` indirect loads, prints "Hello!"),
  `factorial.asm` (`CALL`/`RET` + `PUSH`/`POP` register preservation,
  prints 120), and `sum.asm` (array indexing - the program that was
  literally impossible to write before `REG_INDIRECT` was wired to
  `LOAD`/`STORE` back in the v0.1→v0.2 design review; prints 150). All
  three verified running end to end through the CLI.
- New test file `test_assembler.c`: the §11.1 example program, backward
  and forward label references, `.word`/`.string`/`.byte`/`.org`/`.data`,
  `[Rn]` addressing, the `-1 == 65535` immediate equivalence (§11.4),
  the code/data non-overlap regression, and 13 error cases (unknown
  mnemonic, undefined label, out-of-range register and immediate, illegal
  addressing mode, duplicate label, backward `.org`, double `.data`, bad
  string escape, unterminated string, wrong operand counts, and `LOAD`
  without brackets). `make build` and `make test` verified passing
  (9 test modules, 0 failures).
- Phase 6 implemented per `docs/DESIGN.md` §15: `vm_load` (`loader.h`/
  `loader.c`), parsing the `.bvm` header field-by-field (not a raw struct
  `fread`, per §10) and enforcing every rejection condition - bad
  magic/version, non-zero `reserved`, a file shorter than
  `code_size`+`data_size` claim, code/data overflowing the 64KB address
  space, `entry_point` outside the code blob, and code/data overlap. Every
  check runs before any state is touched, so a rejected load leaves the VM
  completely untouched (verified with a sentinel-register test). On
  success, `vm_load` resets CPU/memory, copies code/data to their
  header-specified addresses, sets `PC = entry_point`, and computes
  `stack_limit`. Found and fixed one new edge case while implementing
  this: `code_addr + code_size` (or the data equivalent) can legitimately
  equal `0x10000` (a region reaching exactly the top of memory) - naively
  truncating that to `uint16_t` for `stack_limit` wraps to `0`, silently
  disabling the overflow check; `stack_limit` now clamps to `0xFFFF`
  instead, added to `DESIGN.md` §10 as well as the code. `vm_load` also
  doubles as the `reset` implementation from §13 (reloading the same bytes
  reproduces the exact as-loaded state) - verified directly in the new
  test file. New test file `test_loader.c`: a valid load, all six
  rejection conditions, the `0x10000` clamping edge case, and the
  reload-as-reset behavior. `make build` and `make test` verified passing
  (8 test modules, 0 failures).
- Phase 5 implemented per `docs/DESIGN.md` §15 - **this completes the full
  ISA**; every opcode now has real execution semantics, and
  `instruction_execute`'s switch no longer has an "unimplemented" fallback
  group. `CALL`/`RET` mirror `PUSH`/`POP`'s exact overflow/underflow rules
  (same `stack_limit`/`SP < 2`/empty-sentinel checks), pushing/popping `PC`
  instead of a register value; `PC` is already advanced past `CALL`'s own
  bytes by the fetch step (§2.1), so the pushed return address is always
  the instruction right after the `CALL`. `SYS` implements all four
  syscalls: `PRINT_INT`/`PRINT_CHAR` via `stdio`, `READ_INT` with the exact
  parsing rule from §8.7 (skip whitespace, optional sign, digits, leave
  the first non-digit unconsumed via `ungetc`, truncate modulo 65536 using
  unsigned accumulation so arbitrarily many digits never triggers signed-
  overflow UB), and `EXIT` (halts; host exit code `R0 & 0xFF` is the
  caller's responsibility, not the VM's). Unknown syscall codes report
  `VM_ERR_INVALID_SYSCALL`. New test file `test_functions.c`: `CALL`/`RET`
  round trip plus their overflow/underflow, and `SYS` tests that redirect
  `stdout`/`stdin` through `tmpfile()` to actually verify printed output
  and parsed input (including the unconsumed-trailing-character case).
  `main.c`'s demo now prints its result via `SYS PRINT_INT` instead of
  just inspecting `R0` in the C harness. `make build` and `make test`
  verified passing (7 test modules, 0 failures).
- Phase 4 implemented per `docs/DESIGN.md` §15: `JMP` and all eight
  conditional jumps (`JZ`/`JNZ`/`JG`/`JGE`/`JL`/`JLE` signed, `JB`/`JAE`/
  `JA`/`JBE` unsigned - each condition matches §5's table exactly), and
  `PUSH`/`POP` with the precise overflow/underflow formula from §6: PUSH
  checks `SP < 2` (the wraparound guard) *before* `SP - 2 < stack_limit`,
  via short-circuit evaluation so the second check never runs on a value
  that would itself have wrapped; POP checks `SP == CPU_INITIAL_SP` (the
  empty-stack sentinel). Added `stack_limit` as new `VM`-level state
  (`vm.h`), defaulting to `0` until Phase 6's loader sets it. New test file
  `test_control.c`: `JMP` unconditional, every conditional jump's taken/
  not-taken branches (grouped by shared flag setups so each block exercises
  all four jumps in a family against one flag state), `PUSH`/`POP` round
  trip with both `REG` and `IMM16` sources, the overflow boundary, the
  `SP < 2` wraparound guard specifically (a case `stack_limit` alone can't
  catch), and underflow. `main.c`'s demo program is now a real loop (sums
  5+4+3+2+1 via `JNZ`) instead of straight-line code, proving jumps
  actually loop rather than just branching once. `make build` and
  `make test` verified passing (6 test modules, 0 failures).
- Phase 3 implemented per `docs/DESIGN.md` §15: `MUL`/`DIV`/`MOD` (with the
  explicit truncation-vs-signed-overflow formula for `MUL`'s `C`/`O` from
  §8.2, and the "leaves `C`/`O` unmodified" rule for `DIV`/`MOD`),
  `AND`/`OR`/`XOR`/`NOT` (`Z`/`S` only), `SHL`/`SHR` (the full §8.3
  shift-count table - `n == 0`, `1 <= n <= 16`, `n > 16` - implemented as
  explicit branches, never a native C shift with an unvalidated count),
  and `CMP` (§8.4, reusing `SUB`'s exact flag formula with no register
  write-back). `CMP` wasn't explicitly listed under Phase 3 in the phase
  table, but was added here anyway and the phase table updated to say so
  - it's essentially free once `SUB`'s formula exists, and Phase 4's
  conditional jumps need something to set flags without a workaround.
  `vm.c`'s execute dispatch was refactored into one `exec_*` function per
  opcode (previously inlined in the switch) for readability now that it
  covers 15 real opcodes instead of 6. New test file `test_alu.c`: `MUL`'s
  three-way truncation/overflow split (normal, truncation-without-overflow,
  overflow-without-truncation - proving `C` and `O` are genuinely
  independent), `DIV`/`MOD` including div-by-zero and the
  `C`/`O`-left-unmodified contract, `AND`/`OR`/`XOR`/`NOT`, every range of
  `SHL`/`SHR`'s shift-count table, and `CMP` including a case where signed
  and unsigned comparisons deliberately disagree. `make build` and
  `make test` verified passing (5 test modules, 0 failures).
- Phase 2 implemented per `docs/DESIGN.md` §15: `opcode.h` (full `Opcode`
  and `AddrMode` enums), `instruction.h`/`instruction.c`
  (`instruction_decode`, enforcing the complete §7.3 legality table, the
  `0-7` register-index check, and unknown-mode-nibble rejection), and
  `vm.c`'s `vm_step` (the fetch/decode/execute cycle from §2.1, the
  already-halted no-op contract from §9, and real execution semantics for
  `NOP`/`MOV`/`ADD`/`SUB`/`LOAD`/`STORE`/`HALT` - including the exact
  `ADD`/`SUB` flag formulas from §8.2). Every other valid opcode decodes
  successfully but reports `VM_ERR_INVALID_OPCODE` at execute time until
  its own phase lands; the execute switch lists every `Opcode` value
  explicitly (no `default`) so adding a new one without handling it here
  is a compiler warning, not a silent gap. `main.c` now runs a hand-written
  demo program (`MOV R0,5; MOV R1,10; ADD R0,R1; HALT`) end to end instead
  of just printing initial state. New tests: `test_instruction.c` (legal
  decode, illegal modes/opcodes/register-indices, and the `0xFFFF`/`0x0000`
  end-of-memory wraparound case) and `test_vm.c` (the demo program, the
  `ADD`/`SUB` flag boundary cases named in `DESIGN.md` §14, `LOAD`/`STORE`
  via both addressing modes, and a decode-failure/no-op-after-halt case).
  Also added `error_pc` semantics to `DESIGN.md` §9 (always the faulting
  instruction's address, never a partially-advanced `PC`) - a gap noticed
  while implementing this phase, not from either review pass.
  `make build` and `make test` verified passing.
- `docs/DESIGN.md` bumped to v0.3 after a second review (post-Phase-1) found
  22 further spec gaps: an SP-semantics/prose contradiction (empty- vs
  full-descending stack), a self-contradictory `0xFFFF` "guard byte" claim,
  an unformalized stack-overflow boundary (now `SP < 2 or SP-2 <
  stack_limit`, with `stack_limit` covering both code and data - not just
  data as before), an undefined fetch/PC-advance order for `CALL`'s return
  address (now §2.1, a formal instruction cycle), undefined end-of-memory
  instruction fetch, missing loader file-length and `reserved`-field
  checks, unbounded register-index encoding, unspecified immediate
  ranges/signedness, incomplete `READ_INT`/`EXIT` host-interaction
  semantics, informal (not bit-exact) `ADD`/`SUB`/`CMP` flag formulas, a
  missing `.byte` assembler directive that broke the disassembler's own
  round-trip promise, incomplete `.org`/`.string` assembler semantics, an
  acknowledged code/data disassembly ambiguity, undefined debugger
  breakpoint timing and `reset` semantics, an undefined already-halted
  `vm_step` contract, an incomplete initial-register-state contract, and an
  unbounded fuzz test that could hang forever. Plus one unrelated stale
  cross-reference (assembler/disassembler section numbers were swapped in
  one sentence since v0.1). Full list with rationale: `docs/DESIGN.md` §0.
  None of it required changing Phase 1 code - verified by rebuilding and
  rerunning the full test suite after the doc changes (still 0 failures).
- Phase 1 implemented per `docs/DESIGN.md` §15: `Memory` (64 KB flat array,
  `mem_init`/`mem_read8`/`mem_write8`/`mem_read16`/`mem_write16`), `CPU`
  (register file, `cpu_init` with `SP = 0xFFFE`, `FLAGS` bit accessors),
  and `VM` (wraps both + halted/error state, `vm_init`). Also fixed the
  test-binary build rule in `Makefile`/`CMakeLists.txt`, which omitted
  `tests/test_main.c` from the link - the same class of bug flagged in the
  earlier review, now closed before any test file existed to trigger it.
  `make build` and `make test` both verified passing.
- `docs/DESIGN.md` bumped to v0.2 after a design review. Fixed: a stack-
  pointer wraparound bug that corrupted address `0x0000` on the first
  `PUSH`/`CALL`, a `MUL` flags contradiction, a missing array/pointer
  addressing story (`REG_INDIRECT` was defined but never wired to
  `LOAD`/`STORE`), a missing unsigned-comparison jump family, no
  addressing-mode legality table, unspecified shift-count edge cases, a
  missing `VM_ERR_INVALID_SYSCALL`, and a non-portable bytecode-header
  parsing assumption. Full list with rationale: `docs/DESIGN.md` §0.
- Removed the entire prior scaffold's source, header, test, and example
  files (all were TODO stubs written against the pre-fix design). Directory
  layout kept via `.gitkeep`; implementation restarts clean from
  `docs/DESIGN.md` v0.2, Phase 1.
- Project scaffold created: directory layout, headers, build files, CI.
- Full technical design (`docs/DESIGN.md`) written - ISA, encoding,
  bytecode format, calling convention, assembler grammar, debugger
  commands, and the 11-phase implementation plan.
- No functional code yet - see docs/DESIGN.md section 15 for phase order.

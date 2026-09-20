# ByteVM

A 16-bit register-based virtual machine, built from scratch in C17: a
CPU/memory core, a custom instruction set, a two-pass assembler, a
disassembler with a byte-identical round trip, and an interactive
debugger.

**Status: v1.0.** All ten phases in the design plan are implemented,
tested, and wired into one CLI. The design went through two review passes
before and during implementation (a memory-corruption bug, a flag-
semantics contradiction, 22 further specification gaps, and a handful
found while building) - all fixed and recorded in `docs/DESIGN.md`, which
remains the single source of truth for the ISA, memory model, encoding,
and file formats. If an implementation detail ever forces a change,
update that document first, then the code.

➡️ **[docs/DESIGN.md](docs/DESIGN.md)** (v0.3) - architecture, instruction
cycle, register set, memory model, instruction encoding, addressing-mode
legality, full ISA reference, FLAGS semantics, `.bvm` bytecode file
format, calling convention, assembler grammar, disassembler design,
debugger commands, and the phase-by-phase build log (§0, §15).

## Build

```sh
make build     # builds build/bytevm
make test      # builds and runs the 13-module unit test suite
make format    # clang-format everything
```

A `CMakeLists.txt` is included as an alternative to the Makefile; both
enforce `-Wall -Wextra -Wpedantic -Werror` identically, and both are
exercised in CI (`.github/workflows/ci.yml`) alongside a clang-format
check, on both gcc and clang.

## Usage

```sh
./build/bytevm run examples/hello.asm        # assemble and execute
./build/bytevm run program.bvm               # execute an existing image
./build/bytevm asm examples/sum.asm out.bvm  # assemble only
./build/bytevm dis out.bvm                   # disassemble back to .asm
./build/bytevm debug examples/sort.asm       # interactive debugger
```

The process exit status is the program's `R0 & 0xFF` (`DESIGN.md` §8.7).

In the debugger: `registers`, `step [n]`, `continue`, `break <addr>`,
`delete <addr>`, `memory <addr> [n]`, `stack`, `disassemble [addr] [n]`,
`reset`, `quit` - type `help` for the list.

## Examples

| File | Demonstrates | Output |
|---|---|---|
| `examples/hello.asm` | `.string` data, `[Rn]` indirect loads, a character loop | `Hello!` |
| `examples/factorial.asm` | `CALL`/`RET`, `PUSH`/`POP` register preservation | `120` |
| `examples/sum.asm` | array indexing via runtime-computed addresses | `150` |
| `examples/sort.asm` | bubble sort - the case that needed `REG_INDIRECT` to exist at all (see `DESIGN.md` §0) | `1 2 3 5 8 9` |

Each has a golden fixture under `tests/golden/` (expected stdout + exit
code), checked by `tests/unit/test_golden.c` on every `make test`. Every
example is also verified `asm → bvm → asm → bvm` byte-identical by
`tests/unit/test_disassembler.c`.

## Layout

```
include/bytevm/     Public headers: common.h, cpu.h, memory.h, vm.h, opcode.h,
                     instruction.h, loader.h, assembler.h, disassembler.h,
                     debugger.h
src/                Core VM: common.c, cpu.c, memory.c, instruction.c,
                     vm.c (fetch/decode/execute + full ALU + control flow/
                     stack + CALL/RET/SYS), loader.c, assembler.c,
                     disassembler.c, debugger.c, main.c (the CLI)
src/assembler/      Lexer, symbol table, two-pass parser/codegen
tests/unit/         13 test modules, one per src/ concern plus golden and fuzz
tests/test_main.c   Single test-runner entry point (DESIGN.md §14 - the only
                     file allowed to define main() in the test binary)
tests/golden/       Expected stdout/exit-code fixtures for examples/*.asm
examples/           hello.asm, factorial.asm, sum.asm, sort.asm
tools/              Reserved for future auxiliary tools; currently empty
docs/               DESIGN.md - single source of truth for the ISA/formats
.github/workflows/  CI: make (gcc+clang) / cmake+ctest / clang-format check
```

## Project history

`docs/DESIGN.md` §0 keeps a full log of what was wrong at each review pass
and why the fix is what it is - not just a changelog of features added.
Worth reading if you're extending this: several of the ISA's less obvious
rules (why `SP` starts at `0xFFFE`, why `MUL`'s overflow flag is a
different computation from `ADD`'s, why jump targets and `LOAD`/`STORE`
addresses use different bracket conventions) exist because an earlier,
more "obvious" version of the rule was wrong in a specific, documented way.

## License

MIT - see `LICENSE`.

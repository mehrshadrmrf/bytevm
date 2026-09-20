# Contributing to ByteVM

## Ground rules

1. `docs/DESIGN.md` is the single source of truth for the ISA, encoding,
   memory model, and file formats. If your change conflicts with it,
   update the design doc first, in its own commit, then implement.
2. Follow the phase order in `docs/DESIGN.md` section 15 - don't start
   the assembler before the core fetch/decode/execute loop is tested.
3. Every new instruction needs: an entry in the opcode table, a unit
   test that checks both its result and its FLAGS effect, and a line
   in `docs/DESIGN.md` section 8's instruction reference.
4. Format code with `clang-format` (config included) before committing.
5. `make test` must pass before opening a PR.

## Project layout

See `README.md` for the directory map.

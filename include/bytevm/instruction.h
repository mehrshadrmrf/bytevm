#ifndef BYTEVM_INSTRUCTION_H
#define BYTEVM_INSTRUCTION_H

#include <stdbool.h>
#include <stdint.h>

#include "bytevm/common.h"
#include "bytevm/memory.h"
#include "bytevm/opcode.h"

/* A single decoded operand. `mode` is always one of the six defined
 * AddrMode values -- instruction_decode never produces anything else.
 * `value`'s meaning depends on mode:
 *   MODE_NONE          -- unused, always 0
 *   MODE_REG           -- register index, 0-7
 *   MODE_REG_INDIRECT  -- register index, 0-7 (holds the address at
 *                         execute time, not the address itself)
 *   MODE_IMM16         -- the 16-bit immediate value
 *   MODE_MEM_DIRECT    -- the 16-bit absolute address
 *   MODE_IMM8          -- the 8-bit immediate value (0-255) */
typedef struct {
    AddrMode mode;
    uint16_t value;
} Operand;

typedef struct {
    Opcode opcode;
    Operand op1;
    Operand op2;
} Instruction;

/* Fetches and decodes one instruction starting at *pc (DESIGN.md section
 * 2.1, "Fetch" + "Decode"). On success, *pc is advanced past every byte
 * consumed (opcode + mode byte + operand bytes) and *out holds the
 * decoded instruction; *pc is left untouched on failure.
 *
 * Address arithmetic wraps modulo 65536 as everywhere else in this
 * project (DESIGN.md section 3) -- an instruction whose bytes would
 * extend past 0xFFFF simply continues from 0x0000 (section 2.1).
 *
 * Returns:
 *   VM_ERR_INVALID_OPCODE -- the opcode byte isn't any defined mnemonic
 *   VM_ERR_INVALID_MODE   -- an unknown mode nibble (0x6-0xF), a mode
 *                            not legal for this opcode/operand (section
 *                            7.3), or a REG/REG_INDIRECT index outside
 *                            0-7 (section 7.1) */
VmResult instruction_decode(const Memory *mem, uint16_t *pc, Instruction *out);

/* Exposes DESIGN.md section 7.3's legality table (the same one
 * instruction_decode enforces) so other code -- currently just the
 * assembler's codegen -- can validate an operand's mode against a given
 * opcode without duplicating the rules. Writes the op1/op2 legal-mode
 * bitmasks (bit i set means AddrMode value i is legal) and returns true
 * if opcode is a real mnemonic; returns false (leaving the output
 * pointers untouched) otherwise. */
bool instruction_opcode_modes(Opcode opcode, uint8_t *op1_modes, uint8_t *op2_modes);

#endif /* BYTEVM_INSTRUCTION_H */

#ifndef BYTEVM_OPCODE_H
#define BYTEVM_OPCODE_H

#include <stdint.h>

/* Full opcode set (DESIGN.md section 16). Values are fixed by the ISA
 * encoding, not by declaration order -- do not renumber without updating
 * DESIGN.md first. Only NOP, MOV, ADD, SUB, LOAD, STORE, and HALT have
 * real execution semantics as of Phase 2; the rest are valid encodings
 * that instruction_decode already accepts, but instruction_execute
 * (src/vm.c) reports VM_ERR_INVALID_OPCODE for them until their phase
 * lands (DESIGN.md section 15). */
typedef enum {
    OP_NOP = 0x00,
    OP_MOV = 0x01,
    OP_ADD = 0x02,
    OP_SUB = 0x03,
    OP_MUL = 0x04,
    OP_DIV = 0x05,
    OP_MOD = 0x06,
    OP_AND = 0x07,
    OP_OR = 0x08,
    OP_XOR = 0x09,
    OP_NOT = 0x0A,
    OP_SHL = 0x0B,
    OP_SHR = 0x0C,
    OP_LOAD = 0x0D,
    OP_STORE = 0x0E,
    OP_PUSH = 0x0F,
    OP_POP = 0x10,
    OP_CMP = 0x11,
    OP_JMP = 0x12,
    OP_JZ = 0x13,
    OP_JNZ = 0x14,
    OP_JG = 0x15,
    OP_JGE = 0x16,
    OP_JL = 0x17,
    OP_JLE = 0x18,
    OP_JB = 0x19,
    OP_JAE = 0x1A,
    OP_JA = 0x1B,
    OP_JBE = 0x1C,
    OP_CALL = 0x1D,
    OP_RET = 0x1E,
    OP_SYS = 0x1F,
    OP_HALT = 0xFF,
} Opcode;

/* Addressing modes (DESIGN.md section 7.1). Values are the mode-byte
 * nibble values -- fixed by the encoding, not declaration order. */
typedef enum {
    MODE_NONE = 0x0,
    MODE_REG = 0x1,
    MODE_IMM16 = 0x2,
    MODE_MEM_DIRECT = 0x3,
    MODE_REG_INDIRECT = 0x4,
    MODE_IMM8 = 0x5,
} AddrMode;

#endif /* BYTEVM_OPCODE_H */

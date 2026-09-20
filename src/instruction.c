#include "bytevm/instruction.h"

#include <stdbool.h>

/* Bitmask over the 6 defined AddrMode values (bit i set == mode i legal). */
typedef uint8_t ModeMask;

#define MODE_BIT(m) ((ModeMask)(1u << (m)))

#define MODES_NONE MODE_BIT(MODE_NONE)
#define MODES_REG MODE_BIT(MODE_REG)
#define MODES_REG_IMM16 (ModeMask)(MODE_BIT(MODE_REG) | MODE_BIT(MODE_IMM16))
#define MODES_MEM (ModeMask)(MODE_BIT(MODE_MEM_DIRECT) | MODE_BIT(MODE_REG_INDIRECT))
#define MODES_MEM_DIRECT MODE_BIT(MODE_MEM_DIRECT)
#define MODES_IMM8 MODE_BIT(MODE_IMM8)

typedef struct {
    bool valid;
    ModeMask op1_modes;
    ModeMask op2_modes;
} OpcodeInfo;

/* DESIGN.md section 7.3, verbatim. Indices not listed here default to
 * {0, 0, 0} (valid == false), which instruction_decode treats as
 * VM_ERR_INVALID_OPCODE. */
static const OpcodeInfo OPCODE_TABLE[256] = {
    [OP_NOP] = {true, MODES_NONE, MODES_NONE},
    [OP_MOV] = {true, MODES_REG, MODES_REG_IMM16},
    [OP_ADD] = {true, MODES_REG, MODES_REG_IMM16},
    [OP_SUB] = {true, MODES_REG, MODES_REG_IMM16},
    [OP_MUL] = {true, MODES_REG, MODES_REG_IMM16},
    [OP_DIV] = {true, MODES_REG, MODES_REG_IMM16},
    [OP_MOD] = {true, MODES_REG, MODES_REG_IMM16},
    [OP_AND] = {true, MODES_REG, MODES_REG_IMM16},
    [OP_OR] = {true, MODES_REG, MODES_REG_IMM16},
    [OP_XOR] = {true, MODES_REG, MODES_REG_IMM16},
    [OP_NOT] = {true, MODES_REG, MODES_NONE},
    [OP_SHL] = {true, MODES_REG, MODES_REG_IMM16},
    [OP_SHR] = {true, MODES_REG, MODES_REG_IMM16},
    [OP_LOAD] = {true, MODES_REG, MODES_MEM},
    [OP_STORE] = {true, MODES_MEM, MODES_REG},
    [OP_PUSH] = {true, MODES_REG_IMM16, MODES_NONE},
    [OP_POP] = {true, MODES_REG, MODES_NONE},
    [OP_CMP] = {true, MODES_REG_IMM16, MODES_REG_IMM16},
    [OP_JMP] = {true, MODES_MEM_DIRECT, MODES_NONE},
    [OP_JZ] = {true, MODES_MEM_DIRECT, MODES_NONE},
    [OP_JNZ] = {true, MODES_MEM_DIRECT, MODES_NONE},
    [OP_JG] = {true, MODES_MEM_DIRECT, MODES_NONE},
    [OP_JGE] = {true, MODES_MEM_DIRECT, MODES_NONE},
    [OP_JL] = {true, MODES_MEM_DIRECT, MODES_NONE},
    [OP_JLE] = {true, MODES_MEM_DIRECT, MODES_NONE},
    [OP_JB] = {true, MODES_MEM_DIRECT, MODES_NONE},
    [OP_JAE] = {true, MODES_MEM_DIRECT, MODES_NONE},
    [OP_JA] = {true, MODES_MEM_DIRECT, MODES_NONE},
    [OP_JBE] = {true, MODES_MEM_DIRECT, MODES_NONE},
    [OP_CALL] = {true, MODES_MEM_DIRECT, MODES_NONE},
    [OP_RET] = {true, MODES_NONE, MODES_NONE},
    [OP_SYS] = {true, MODES_IMM8, MODES_NONE},
    [OP_HALT] = {true, MODES_NONE, MODES_NONE},
};

/* True for the 6 mode nibble values this ISA defines (0x0-0x5); false for
 * the reserved/undefined nibbles 0x6-0xF (DESIGN.md section 7.1). */
static bool mode_is_known(AddrMode mode)
{
    return mode <= MODE_IMM8;
}

static bool mode_is_legal(AddrMode mode, ModeMask legal)
{
    if (!mode_is_known(mode)) {
        return false;
    }
    return (legal & MODE_BIT(mode)) != 0;
}

/* Decodes one operand at *addr, advancing *addr past whatever bytes that
 * mode consumes (DESIGN.md section 7.1's "bytes consumed" column). */
static VmResult decode_operand(const Memory *mem, uint16_t *addr, AddrMode mode, Operand *out)
{
    out->mode = mode;

    switch (mode) {
    case MODE_NONE:
        out->value = 0;
        return VM_OK;

    case MODE_REG:
    case MODE_REG_INDIRECT: {
        uint8_t idx = mem_read8(mem, *addr);
        *addr = (uint16_t)(*addr + 1);
        if (idx > 7) {
            /* Encodable in the bitstream (a full byte), but not an
             * architectural register -- section 7.1. */
            return VM_ERR_INVALID_MODE;
        }
        out->value = idx;
        return VM_OK;
    }

    case MODE_IMM8:
        out->value = mem_read8(mem, *addr);
        *addr = (uint16_t)(*addr + 1);
        return VM_OK;

    case MODE_IMM16:
    case MODE_MEM_DIRECT:
        out->value = mem_read16(mem, *addr);
        *addr = (uint16_t)(*addr + 2);
        return VM_OK;
    }

    return VM_ERR_INVALID_MODE; /* unreachable if mode_is_legal was checked first */
}

/* Exposes the section 7.3 legality table for reuse outside decode --
 * the assembler's codegen (Phase 7) validates operand modes against the
 * exact same rules, so there is exactly one place these rules live.
 * Returns false if opcode isn't a valid mnemonic at all. */
bool instruction_opcode_modes(Opcode opcode, uint8_t *op1_modes, uint8_t *op2_modes)
{
    const OpcodeInfo *info = &OPCODE_TABLE[(uint8_t)opcode];
    if (!info->valid) {
        return false;
    }
    *op1_modes = info->op1_modes;
    *op2_modes = info->op2_modes;
    return true;
}

VmResult instruction_decode(const Memory *mem, uint16_t *pc, Instruction *out)
{
    uint16_t addr = *pc;

    uint8_t opcode_byte = mem_read8(mem, addr);
    const OpcodeInfo *info = &OPCODE_TABLE[opcode_byte];
    if (!info->valid) {
        return VM_ERR_INVALID_OPCODE;
    }
    addr = (uint16_t)(addr + 1);

    uint8_t mode_byte = mem_read8(mem, addr);
    AddrMode mode1 = (AddrMode)(mode_byte >> 4);
    AddrMode mode2 = (AddrMode)(mode_byte & 0x0Fu);
    addr = (uint16_t)(addr + 1);

    if (!mode_is_legal(mode1, info->op1_modes)) {
        return VM_ERR_INVALID_MODE;
    }
    if (!mode_is_legal(mode2, info->op2_modes)) {
        return VM_ERR_INVALID_MODE;
    }

    Operand op1;
    Operand op2;
    VmResult result;

    result = decode_operand(mem, &addr, mode1, &op1);
    if (result != VM_OK) {
        return result;
    }
    result = decode_operand(mem, &addr, mode2, &op2);
    if (result != VM_OK) {
        return result;
    }

    out->opcode = (Opcode)opcode_byte;
    out->op1 = op1;
    out->op2 = op2;
    *pc = addr;
    return VM_OK;
}

#include "bytevm/disassembler.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bytevm/instruction.h"
#include "bytevm/loader.h"
#include "bytevm/memory.h"
#include "bytevm/opcode.h"

/* ---- growable output buffer ---- */

typedef struct {
    char *buf;
    size_t len;
    size_t cap;
    bool oom;
} Out;

static void out_init(Out *o)
{
    o->buf = NULL;
    o->len = 0;
    o->cap = 0;
    o->oom = false;
}

static void out_printf(Out *o, const char *fmt, ...)
{
    if (o->oom) {
        return;
    }
    char tmp[256];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, args);
    va_end(args);
    if (n < 0) {
        o->oom = true;
        return;
    }

    size_t need = o->len + (size_t)n + 1;
    if (need > o->cap) {
        size_t new_cap = (o->cap == 0) ? 1024 : o->cap;
        while (new_cap < need) {
            new_cap *= 2;
        }
        char *grown = realloc(o->buf, new_cap);
        if (grown == NULL) {
            o->oom = true;
            return;
        }
        o->buf = grown;
        o->cap = new_cap;
    }
    memcpy(o->buf + o->len, tmp, (size_t)n);
    o->len += (size_t)n;
    o->buf[o->len] = '\0';
}

/* ---- mnemonic lookup ---- */

static const char *mnemonic_for(Opcode op)
{
    switch (op) {
    case OP_NOP:
        return "NOP";
    case OP_MOV:
        return "MOV";
    case OP_ADD:
        return "ADD";
    case OP_SUB:
        return "SUB";
    case OP_MUL:
        return "MUL";
    case OP_DIV:
        return "DIV";
    case OP_MOD:
        return "MOD";
    case OP_AND:
        return "AND";
    case OP_OR:
        return "OR";
    case OP_XOR:
        return "XOR";
    case OP_NOT:
        return "NOT";
    case OP_SHL:
        return "SHL";
    case OP_SHR:
        return "SHR";
    case OP_LOAD:
        return "LOAD";
    case OP_STORE:
        return "STORE";
    case OP_PUSH:
        return "PUSH";
    case OP_POP:
        return "POP";
    case OP_CMP:
        return "CMP";
    case OP_JMP:
        return "JMP";
    case OP_JZ:
        return "JZ";
    case OP_JNZ:
        return "JNZ";
    case OP_JG:
        return "JG";
    case OP_JGE:
        return "JGE";
    case OP_JL:
        return "JL";
    case OP_JLE:
        return "JLE";
    case OP_JB:
        return "JB";
    case OP_JAE:
        return "JAE";
    case OP_JA:
        return "JA";
    case OP_JBE:
        return "JBE";
    case OP_CALL:
        return "CALL";
    case OP_RET:
        return "RET";
    case OP_SYS:
        return "SYS";
    case OP_HALT:
        return "HALT";
    }
    return NULL;
}

static const char *syscall_name_for(uint16_t code)
{
    switch (code) {
    case 0x01:
        return "PRINT_INT";
    case 0x02:
        return "PRINT_CHAR";
    case 0x03:
        return "READ_INT";
    case 0x04:
        return "EXIT";
    default:
        return NULL;
    }
}

/* Formats one operand back into the source syntax the assembler accepts
 * (DESIGN.md section 11.1).
 *
 * MEM_DIRECT is the one mode whose *spelling* depends on context: the
 * assembler only requires bracket syntax where MEM_DIRECT and
 * REG_INDIRECT are both legal for that position (LOAD's/STORE's address
 * operand), because that's the only place the brackets carry
 * information. Everywhere else -- jump and CALL targets -- a bare
 * address is what the assembler expects, so emitting brackets there
 * would produce text that no longer reassembles. */
static void format_operand(Out *o, Opcode opcode, const Operand *op, uint8_t legal_mask)
{
    switch (op->mode) {
    case MODE_NONE:
        break;
    case MODE_REG:
        out_printf(o, "R%u", (unsigned)op->value);
        break;
    case MODE_REG_INDIRECT:
        out_printf(o, "[R%u]", (unsigned)op->value);
        break;
    case MODE_IMM16:
        out_printf(o, "%u", (unsigned)op->value);
        break;
    case MODE_IMM8: {
        const char *name = (opcode == OP_SYS) ? syscall_name_for(op->value) : NULL;
        if (name != NULL) {
            out_printf(o, "%s", name);
        } else {
            out_printf(o, "%u", (unsigned)op->value);
        }
        break;
    }
    case MODE_MEM_DIRECT: {
        bool indirect_also_legal = (legal_mask & (uint8_t)(1u << MODE_REG_INDIRECT)) != 0;
        if (indirect_also_legal) {
            out_printf(o, "[0x%04X]", (unsigned)op->value);
        } else {
            out_printf(o, "0x%04X", (unsigned)op->value);
        }
        break;
    }
    }
}

static uint16_t read_u16_le(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

/* Formats one already-decoded instruction as `MNEMONIC op1, op2` into an
 * Out buffer (no leading indent, no trailing newline -- callers add
 * whatever framing they need). */
static void format_instruction(Out *o, const Instruction *instr)
{
    uint8_t op1_modes = 0;
    uint8_t op2_modes = 0;
    instruction_opcode_modes(instr->opcode, &op1_modes, &op2_modes);

    out_printf(o, "%s", mnemonic_for(instr->opcode));
    if (instr->op1.mode != MODE_NONE) {
        out_printf(o, " ");
        format_operand(o, instr->opcode, &instr->op1, op1_modes);
        if (instr->op2.mode != MODE_NONE) {
            out_printf(o, ", ");
            format_operand(o, instr->opcode, &instr->op2, op2_modes);
        }
    }
}

bool disassemble_one(const Memory *mem, uint16_t addr, char *buf, size_t buf_size,
                     uint16_t *next_addr)
{
    Instruction instr;
    uint16_t next = addr;
    if (instruction_decode(mem, &next, &instr) != VM_OK) {
        snprintf(buf, buf_size, ".byte 0x%02X", (unsigned)mem_read8(mem, addr));
        if (next_addr != NULL) {
            *next_addr = (uint16_t)(addr + 1);
        }
        return false;
    }

    Out o;
    out_init(&o);
    format_instruction(&o, &instr);
    if (o.oom || o.buf == NULL) {
        snprintf(buf, buf_size, "<out of memory>");
    } else {
        snprintf(buf, buf_size, "%s", o.buf);
    }
    free(o.buf);

    if (next_addr != NULL) {
        *next_addr = next;
    }
    return true;
}

void disassemble(const uint8_t *image, size_t image_size, DisassemblyResult *out)
{
    memset(out, 0, sizeof(*out));

    if (image_size < BVM_HEADER_SIZE || memcmp(image, "BVM1", 4) != 0 || image[4] != 1 ||
        image[5] != 0) {
        out->ok = false;
        snprintf(out->error, sizeof(out->error), "not a valid BVM1 image");
        return;
    }

    uint16_t entry_point = read_u16_le(&image[6]);
    uint16_t code_size = read_u16_le(&image[8]);
    uint16_t code_addr = read_u16_le(&image[10]);
    uint16_t data_size = read_u16_le(&image[12]);
    uint16_t data_addr = read_u16_le(&image[14]);

    if (image_size < (size_t)BVM_HEADER_SIZE + code_size + data_size) {
        out->ok = false;
        snprintf(out->error, sizeof(out->error), "image is shorter than its header claims");
        return;
    }

    /* The assembler always emits entry_point == code_addr (the code
     * section's start). An image whose entry_point sits elsewhere is
     * legal per section 10, but there's no source syntax that would
     * reproduce it -- the round-trip guarantee doesn't extend to
     * hand-built images like that, so say so rather than emitting text
     * that silently reassembles to something different. */
    if (entry_point != code_addr) {
        out->ok = false;
        snprintf(out->error, sizeof(out->error),
                 "entry_point (0x%04X) differs from code_addr (0x%04X); no .asm form of this "
                 "exists",
                 entry_point, code_addr);
        return;
    }

    /* Decoding runs against a Memory image so instruction_decode -- the
     * exact same decoder the VM uses -- can be reused verbatim, rather
     * than reimplementing section 7's encoding rules a second time and
     * risking the two drifting apart. */
    Memory *mem = malloc(sizeof(Memory));
    if (mem == NULL) {
        out->ok = false;
        snprintf(out->error, sizeof(out->error), "out of memory");
        return;
    }
    mem_init(mem);

    uint16_t i;
    for (i = 0; i < code_size; i++) {
        mem_write8(mem, (uint16_t)(code_addr + i), image[BVM_HEADER_SIZE + i]);
    }

    Out o;
    out_init(&o);

    out_printf(&o, "; disassembled from a .bvm image\n");
    out_printf(&o, "; code: %u bytes at 0x%04X", (unsigned)code_size, (unsigned)code_addr);
    if (data_size > 0) {
        out_printf(&o, ", data: %u bytes at 0x%04X", (unsigned)data_size, (unsigned)data_addr);
    }
    out_printf(&o, "\n\n");

    if (code_addr != 0) {
        out_printf(&o, ".org 0x%04X\n", (unsigned)code_addr);
    }

    uint16_t pc = code_addr;
    uint16_t end = (uint16_t)(code_addr + code_size);
    while (pc < end) {
        uint16_t next = pc;
        Instruction instr;
        VmResult r = instruction_decode(mem, &next, &instr);

        /* A decode that would run past the end of the code blob is
         * treated as a failure too: its trailing operand bytes aren't
         * actually part of this program, so emitting it as an
         * instruction would invent bytes that don't exist. */
        if (r != VM_OK || next > end || next <= pc) {
            out_printf(&o, "    .byte 0x%02X\n", (unsigned)mem_read8(mem, pc));
            pc = (uint16_t)(pc + 1);
            continue;
        }

        out_printf(&o, "    ");
        format_instruction(&o, &instr);
        out_printf(&o, "\n");
        pc = next;
    }

    if (data_size > 0) {
        out_printf(&o, "\n.data\n");
        /* The assembler starts the data section right after the code
         * section by default (section 11.2); an explicit .org is only
         * needed -- and only correct -- when this image places data
         * somewhere else. */
        if (data_addr != end) {
            out_printf(&o, ".org 0x%04X\n", (unsigned)data_addr);
        }
        /* Data is emitted as raw .byte directives rather than guessed
         * back into .string/.word: those would round-trip identically
         * only for data that happens to match their shape, and the
         * point of this output is that reassembling it reproduces the
         * original bytes exactly. */
        for (i = 0; i < data_size; i++) {
            out_printf(&o, "    .byte 0x%02X\n", (unsigned)image[BVM_HEADER_SIZE + code_size + i]);
        }
    }

    free(mem);

    if (o.oom) {
        free(o.buf);
        out->ok = false;
        snprintf(out->error, sizeof(out->error), "out of memory");
        return;
    }

    out->ok = true;
    out->text = o.buf;
    out->text_len = o.len;
}

void disassembly_result_free(DisassemblyResult *result)
{
    free(result->text);
    result->text = NULL;
    result->text_len = 0;
}

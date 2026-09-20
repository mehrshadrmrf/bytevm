#ifndef BYTEVM_ASM_PARSER_H
#define BYTEVM_ASM_PARSER_H

#include <stdbool.h>
#include <stdint.h>

#include "lexer.h"

typedef struct {
    bool ok;

    /* Valid when ok == true. Two full 64KB scratch regions rather than
     * tightly-sized buffers -- addresses are absolute VM addresses, so
     * slicing [addr, addr+size) out of a zero-initialized 64KB buffer
     * handles forward-.org gaps (DESIGN.md section 11.2) as an emergent
     * property: unwritten bytes are already zero, with no special-case
     * gap-filling logic needed. */
    uint8_t code[65536];
    uint16_t code_addr;
    uint16_t code_size;
    uint8_t data[65536];
    uint16_t data_addr;
    uint16_t data_size;
    uint16_t entry_point;

    /* Valid when ok == false. */
    char error[256];
    int error_line;
} ParseResult;

/* Runs both passes (DESIGN.md section 11) over an already-lexed token
 * list: pass 1 walks every statement computing addresses and the label
 * table (without needing labels resolved yet -- an operand's addressing
 * mode is determined by its syntax and the opcode, not by a label's
 * eventual value); pass 2 re-walks the same parsed statements, resolves
 * every label reference, validates operand modes against
 * instruction_opcode_modes() and immediate ranges against section 11.4,
 * and emits the final bytes. */
void assemble_tokens(const TokenList *tokens, ParseResult *out);

#endif /* BYTEVM_ASM_PARSER_H */

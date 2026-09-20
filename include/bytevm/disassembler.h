#ifndef BYTEVM_DISASSEMBLER_H
#define BYTEVM_DISASSEMBLER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bytevm/memory.h"

/* Result of disassembling a .bvm image back into .asm text
 * (DESIGN.md section 12). */
typedef struct {
    bool ok;

    char *text; /* ok == true: malloc'd NUL-terminated .asm source,
                 * caller frees via disassembly_result_free */
    size_t text_len;

    char error[256]; /* ok == false */
} DisassemblyResult;

/* Disassembles a complete .bvm image (header + blobs) into assembly
 * text. The output is written so that feeding it back through
 * assemble() reproduces the original image byte-for-byte -- that
 * round-trip is the guarantee section 12 actually promises, and it is
 * what tests/unit/test_disassembler.c verifies.
 *
 * Bytes that don't decode as a valid instruction are emitted as `.byte`
 * directives (section 11.2), advancing one byte at a time. That fallback
 * is mechanical: it fires whenever decode fails, and never tries to
 * guess whether a region "looks like" data. */
void disassemble(const uint8_t *image, size_t image_size, DisassemblyResult *out);

/* Formats the single instruction at `addr` in `mem` into `buf`, using
 * the same mnemonic/operand spelling as disassemble() above. Writes the
 * address of the following instruction into *next_addr (unless NULL).
 *
 * Returns true if the bytes decoded as a real instruction; false if they
 * didn't, in which case `buf` holds a `.byte 0xNN` fallback and
 * *next_addr is addr + 1. Used by the debugger's `disassemble` command
 * (DESIGN.md section 13), which works against live VM memory rather than
 * a .bvm image. */
bool disassemble_one(const Memory *mem, uint16_t addr, char *buf, size_t buf_size,
                     uint16_t *next_addr);

void disassembly_result_free(DisassemblyResult *result);

#endif /* BYTEVM_DISASSEMBLER_H */

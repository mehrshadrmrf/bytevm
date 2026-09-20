#ifndef BYTEVM_ASSEMBLER_H
#define BYTEVM_ASSEMBLER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Result of assembling source text into a complete .bvm image
 * (DESIGN.md section 10's file format, produced by section 11's
 * two-pass assembler). */
typedef struct {
    bool ok;

    uint8_t *image; /* ok == true: malloc'd .bvm bytes, caller frees via
                     * assembly_result_free */
    size_t image_size;

    char error[256]; /* ok == false: "line N: message" (section 11.3) */
    int error_line;
} AssemblyResult;

/* Assembles `source` (NUL-terminated .asm text) into a .bvm image.
 * Never partially succeeds: on any lexical, syntax, or semantic error,
 * ok is false, image is NULL, and no output is produced at all
 * (DESIGN.md section 11.3). */
void assemble(const char *source, AssemblyResult *out);

void assembly_result_free(AssemblyResult *result);

#endif /* BYTEVM_ASSEMBLER_H */

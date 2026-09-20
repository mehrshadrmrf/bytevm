#include "bytevm/assembler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assembler/lexer.h"
#include "assembler/parser.h"
#include "bytevm/loader.h"

static void write_u16_le(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)(value & 0xFFu);
    p[1] = (uint8_t)(value >> 8);
}

void assemble(const char *source, AssemblyResult *out)
{
    memset(out, 0, sizeof(*out));

    TokenList tokens;
    char lex_error[256];
    int lex_error_line = 0;

    if (!lex(source, &tokens, lex_error, sizeof(lex_error), &lex_error_line)) {
        out->ok = false;
        out->error_line = lex_error_line;
        snprintf(out->error, sizeof(out->error), "line %d: %.200s", lex_error_line, lex_error);
        token_list_free(&tokens);
        return;
    }

    /* ParseResult carries two 64KB scratch buffers, so it's heap-allocated
     * rather than living on the stack. */
    ParseResult *parsed = malloc(sizeof(ParseResult));
    if (parsed == NULL) {
        out->ok = false;
        snprintf(out->error, sizeof(out->error), "out of memory");
        token_list_free(&tokens);
        return;
    }

    assemble_tokens(&tokens, parsed);
    token_list_free(&tokens);

    if (!parsed->ok) {
        out->ok = false;
        out->error_line = parsed->error_line;
        /* parser.c stores the bare message; the "line N: " prefix
         * (DESIGN.md section 11.3's format) is added exactly once, here.
         * out->error is deliberately the same size as parsed->error, so
         * the message is truncated via an explicit precision rather than
         * letting snprintf silently overflow the prefix into it. */
        snprintf(out->error, sizeof(out->error), "line %d: %.200s", parsed->error_line,
                 parsed->error);
        free(parsed);
        return;
    }

    size_t image_size = BVM_HEADER_SIZE + parsed->code_size + parsed->data_size;
    uint8_t *image = malloc(image_size);
    if (image == NULL) {
        out->ok = false;
        snprintf(out->error, sizeof(out->error), "out of memory");
        free(parsed);
        return;
    }

    memcpy(image, "BVM1", 4);
    image[4] = 1; /* version */
    image[5] = 0; /* reserved */
    write_u16_le(&image[6], parsed->entry_point);
    write_u16_le(&image[8], parsed->code_size);
    write_u16_le(&image[10], parsed->code_addr);
    write_u16_le(&image[12], parsed->data_size);
    write_u16_le(&image[14], parsed->data_addr);

    /* The parser emitted into absolute-addressed 64KB scratch buffers, so
     * the blob for each section is just the slice starting at that
     * section's base address. */
    if (parsed->code_size > 0) {
        memcpy(&image[BVM_HEADER_SIZE], &parsed->code[parsed->code_addr], parsed->code_size);
    }
    if (parsed->data_size > 0) {
        memcpy(&image[BVM_HEADER_SIZE + parsed->code_size], &parsed->data[parsed->data_addr],
               parsed->data_size);
    }

    out->ok = true;
    out->image = image;
    out->image_size = image_size;
    free(parsed);
}

void assembly_result_free(AssemblyResult *result)
{
    free(result->image);
    result->image = NULL;
    result->image_size = 0;
}

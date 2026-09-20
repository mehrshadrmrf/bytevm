#ifndef BYTEVM_ASM_TOKEN_H
#define BYTEVM_ASM_TOKEN_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    TOK_EOF,
    TOK_NEWLINE,
    TOK_IDENT,     /* mnemonic, register (R0-R7), label name/reference,
                    * syscall name, or a directive name -- disambiguated
                    * by the parser, not the lexer */
    TOK_DIRECTIVE, /* .org / .data / .string / .word / .byte; text is the
                    * name WITHOUT the leading '.' */
    TOK_NUMBER,    /* decimal or 0x-prefixed hex literal, optionally
                    * signed; value in num_value */
    TOK_STRING,    /* a "..." literal with escapes already processed;
                    * str_value/str_len (may contain embedded NUL) */
    TOK_COMMA,
    TOK_COLON,
    TOK_LBRACKET,
    TOK_RBRACKET,
} TokenType;

typedef struct {
    TokenType type;
    int line;

    char *text; /* TOK_IDENT / TOK_DIRECTIVE: NUL-terminated lexeme,
                 * owned by this token */

    int64_t num_value; /* TOK_NUMBER */

    uint8_t *str_value; /* TOK_STRING: owned raw bytes */
    size_t str_len;
} Token;

#endif /* BYTEVM_ASM_TOKEN_H */

#ifndef BYTEVM_ASM_LEXER_H
#define BYTEVM_ASM_LEXER_H

#include <stdbool.h>
#include <stddef.h>

#include "token.h"

typedef struct {
    Token *tokens;
    size_t count;
    size_t capacity;
} TokenList;

/* Tokenizes the full source into `out` (always ending with a TOK_EOF
 * token on success). Comments (';' to end of line) and horizontal
 * whitespace are consumed silently; newlines are emitted as TOK_NEWLINE
 * since they're statement separators (DESIGN.md section 11.1's
 * line-oriented syntax).
 *
 * On a lexical error (unterminated string, bad escape, malformed
 * number, or a character that starts no valid token), returns false,
 * writes a message into error_buf, and sets *error_line -- `out` may
 * hold a partial token list in that case and should still be freed. */
bool lex(const char *source, TokenList *out, char *error_buf, size_t error_buf_size,
         int *error_line);

void token_list_free(TokenList *list);

#endif /* BYTEVM_ASM_LEXER_H */

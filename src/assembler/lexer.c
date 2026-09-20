#include "lexer.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INITIAL_CAPACITY 64

static bool tokenlist_push(TokenList *list, Token tok)
{
    if (list->count == list->capacity) {
        size_t new_cap = (list->capacity == 0) ? INITIAL_CAPACITY : list->capacity * 2;
        Token *new_tokens = realloc(list->tokens, new_cap * sizeof(Token));
        if (new_tokens == NULL) {
            return false;
        }
        list->tokens = new_tokens;
        list->capacity = new_cap;
    }
    list->tokens[list->count++] = tok;
    return true;
}

static char *dup_range(const char *start, size_t len)
{
    char *s = malloc(len + 1);
    if (s != NULL) {
        memcpy(s, start, len);
        s[len] = '\0';
    }
    return s;
}

static void set_error(char *error_buf, size_t error_buf_size, int *error_line, int line,
                      const char *msg)
{
    if (error_line != NULL) {
        *error_line = line;
    }
    /* Bare message only -- the "line N: " prefix (DESIGN.md section
     * 11.3's format) is added once, by assemble() in src/assembler.c,
     * so lexer and parser errors come out identically formatted. */
    if (error_buf != NULL && error_buf_size > 0) {
        snprintf(error_buf, error_buf_size, "%s", msg);
    }
}

/* Pushes a token with no payload beyond type+line, or bails out of the
 * enclosing `lex` call with an out-of-memory error. Used for the
 * single-character punctuation tokens and TOK_NEWLINE. */
#define PUSH_SIMPLE(tok_type, tok_line)                                                            \
    do {                                                                                           \
        Token simple_tok_ = {0};                                                                   \
        simple_tok_.type = (tok_type);                                                             \
        simple_tok_.line = (tok_line);                                                             \
        if (!tokenlist_push(out, simple_tok_)) {                                                   \
            set_error(error_buf, error_buf_size, error_line, (tok_line), "out of memory");         \
            return false;                                                                          \
        }                                                                                          \
    } while (0)

bool lex(const char *source, TokenList *out, char *error_buf, size_t error_buf_size,
         int *error_line)
{
    out->tokens = NULL;
    out->count = 0;
    out->capacity = 0;

    const char *p = source;
    int line = 1;

    while (*p != '\0') {
        char c = *p;

        if (c == ' ' || c == '\t' || c == '\r') {
            p++;
            continue;
        }
        if (c == ';') {
            while (*p != '\0' && *p != '\n') {
                p++;
            }
            continue;
        }
        if (c == '\n') {
            PUSH_SIMPLE(TOK_NEWLINE, line);
            line++;
            p++;
            continue;
        }
        if (c == ',') {
            PUSH_SIMPLE(TOK_COMMA, line);
            p++;
            continue;
        }
        if (c == ':') {
            PUSH_SIMPLE(TOK_COLON, line);
            p++;
            continue;
        }
        if (c == '[') {
            PUSH_SIMPLE(TOK_LBRACKET, line);
            p++;
            continue;
        }
        if (c == ']') {
            PUSH_SIMPLE(TOK_RBRACKET, line);
            p++;
            continue;
        }

        if (c == '.') {
            const char *start = p + 1;
            const char *q = start;
            while (isalpha((unsigned char)*q)) {
                q++;
            }
            if (q == start) {
                set_error(error_buf, error_buf_size, error_line, line,
                          "'.' must be followed by a directive name");
                return false;
            }
            Token tok = {0};
            tok.type = TOK_DIRECTIVE;
            tok.line = line;
            tok.text = dup_range(start, (size_t)(q - start));
            if (tok.text == NULL || !tokenlist_push(out, tok)) {
                free(tok.text);
                set_error(error_buf, error_buf_size, error_line, line, "out of memory");
                return false;
            }
            p = q;
            continue;
        }

        if (c == '"') {
            p++; /* skip opening quote */
            uint8_t *buf = NULL;
            size_t len = 0;
            size_t cap = 0;
            bool closed = false;

            while (*p != '\0' && *p != '\n') {
                if (*p == '"') {
                    closed = true;
                    p++;
                    break;
                }

                uint8_t byte;
                if (*p == '\\') {
                    p++;
                    switch (*p) {
                    case 'n':
                        byte = (uint8_t)'\n';
                        p++;
                        break;
                    case 'r':
                        byte = (uint8_t)'\r';
                        p++;
                        break;
                    case 't':
                        byte = (uint8_t)'\t';
                        p++;
                        break;
                    case '\\':
                        byte = (uint8_t)'\\';
                        p++;
                        break;
                    case '"':
                        byte = (uint8_t)'"';
                        p++;
                        break;
                    case '0':
                        byte = 0;
                        p++;
                        break;
                    case 'x':
                        p++;
                        if (!isxdigit((unsigned char)p[0]) || !isxdigit((unsigned char)p[1])) {
                            free(buf);
                            set_error(error_buf, error_buf_size, error_line, line,
                                      "\\x escape needs exactly two hex digits");
                            return false;
                        }
                        {
                            char hex[3] = {p[0], p[1], '\0'};
                            byte = (uint8_t)strtoul(hex, NULL, 16);
                        }
                        p += 2;
                        break;
                    default:
                        free(buf);
                        set_error(error_buf, error_buf_size, error_line, line,
                                  "unrecognized escape sequence in string literal "
                                  "(supported: \\n \\r \\t \\\\ \\\" \\0 \\xNN)");
                        return false;
                    }
                } else {
                    byte = (uint8_t)*p;
                    p++;
                }

                if (len == cap) {
                    size_t new_cap = (cap == 0) ? 16 : cap * 2;
                    uint8_t *nb = realloc(buf, new_cap);
                    if (nb == NULL) {
                        free(buf);
                        set_error(error_buf, error_buf_size, error_line, line, "out of memory");
                        return false;
                    }
                    buf = nb;
                    cap = new_cap;
                }
                buf[len++] = byte;
            }

            if (!closed) {
                free(buf);
                set_error(error_buf, error_buf_size, error_line, line,
                          "unterminated string literal");
                return false;
            }

            Token tok = {0};
            tok.type = TOK_STRING;
            tok.line = line;
            tok.str_value = buf;
            tok.str_len = len;
            if (!tokenlist_push(out, tok)) {
                free(buf);
                set_error(error_buf, error_buf_size, error_line, line, "out of memory");
                return false;
            }
            continue;
        }

        if (c == '-' || isdigit((unsigned char)c)) {
            bool negative = false;
            if (c == '-') {
                negative = true;
                p++;
            }
            if (!isdigit((unsigned char)*p)) {
                set_error(error_buf, error_buf_size, error_line, line,
                          "expected a digit after '-'");
                return false;
            }

            int64_t value = 0;
            if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
                p += 2;
                if (!isxdigit((unsigned char)*p)) {
                    set_error(error_buf, error_buf_size, error_line, line,
                              "expected hex digits after '0x'");
                    return false;
                }
                while (isxdigit((unsigned char)*p)) {
                    char ch = (char)tolower((unsigned char)*p);
                    int digit = (ch >= '0' && ch <= '9') ? (ch - '0') : (ch - 'a' + 10);
                    value = value * 16 + digit;
                    p++;
                }
            } else {
                while (isdigit((unsigned char)*p)) {
                    value = value * 10 + (*p - '0');
                    p++;
                }
            }
            if (negative) {
                value = -value;
            }

            Token tok = {0};
            tok.type = TOK_NUMBER;
            tok.line = line;
            tok.num_value = value;
            if (!tokenlist_push(out, tok)) {
                set_error(error_buf, error_buf_size, error_line, line, "out of memory");
                return false;
            }
            continue;
        }

        if (isalpha((unsigned char)c) || c == '_') {
            const char *start = p;
            while (isalnum((unsigned char)*p) || *p == '_') {
                p++;
            }
            Token tok = {0};
            tok.type = TOK_IDENT;
            tok.line = line;
            tok.text = dup_range(start, (size_t)(p - start));
            if (tok.text == NULL || !tokenlist_push(out, tok)) {
                free(tok.text);
                set_error(error_buf, error_buf_size, error_line, line, "out of memory");
                return false;
            }
            continue;
        }

        {
            char msg[64];
            snprintf(msg, sizeof(msg), "unexpected character '%c'", c);
            set_error(error_buf, error_buf_size, error_line, line, msg);
            return false;
        }
    }

    PUSH_SIMPLE(TOK_EOF, line);
    return true;
}

void token_list_free(TokenList *list)
{
    size_t i;
    for (i = 0; i < list->count; i++) {
        free(list->tokens[i].text);
        free(list->tokens[i].str_value);
    }
    free(list->tokens);
    list->tokens = NULL;
    list->count = 0;
    list->capacity = 0;
}

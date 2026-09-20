#include "parser.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bytevm/instruction.h"
#include "symtab.h"

/* ==================== Statement IR ====================
 * The lexer's token list is parsed into this list exactly once; both
 * "passes" from DESIGN.md section 11 are two walks over THIS list, not
 * two re-tokenizations of the source. An operand's addressing mode is
 * determined purely by its syntax and the opcode (never by a label's
 * eventual value), so pass 1 can compute every statement's exact byte
 * size -- and therefore every address -- without labels being resolved
 * yet; pass 2 then resolves labels and emits bytes into the two 64KB
 * scratch buffers in ParseResult. */

typedef enum {
    OPKIND_NONE,
    OPKIND_REG,          /* bare register, e.g. R3 */
    OPKIND_REG_INDIRECT, /* [R3] */
    OPKIND_NUMBER,       /* bare number */
    OPKIND_MEM_NUMBER,   /* [0x2000] */
    OPKIND_LABEL,        /* bare identifier (label reference) */
    OPKIND_MEM_LABEL,    /* [message] */
} OperandKind;

typedef struct {
    OperandKind kind;
    int reg_index;     /* OPKIND_REG / OPKIND_REG_INDIRECT -- NOT range-checked yet */
    int64_t number;    /* OPKIND_NUMBER / OPKIND_MEM_NUMBER */
    const char *label; /* OPKIND_LABEL / OPKIND_MEM_LABEL -- points into token text */
    int line;
} ParsedOperand;

typedef enum {
    STMT_LABEL,
    STMT_INSTRUCTION,
    STMT_ORG,
    STMT_DATA_SECTION,
    STMT_STRING,
    STMT_WORD,
    STMT_BYTE,
} StmtKind;

typedef struct {
    StmtKind kind;
    int line;
    bool in_data;     /* which scratch buffer this statement targets */
    uint16_t address; /* absolute address this statement starts at (pass 1) */
    int encoded_size;

    const char *label_name; /* STMT_LABEL */

    Opcode opcode; /* STMT_INSTRUCTION */
    int operand_count;
    ParsedOperand op1;
    ParsedOperand op2;
    AddrMode op1_mode;
    AddrMode op2_mode;

    const uint8_t *string_bytes; /* STMT_STRING -- points into the TOK_STRING's owned bytes */
    size_t string_len;

    ParsedOperand word_operand; /* STMT_WORD */
    ParsedOperand byte_operand; /* STMT_BYTE */
} Statement;

typedef struct {
    Statement *items;
    size_t count;
    size_t capacity;
} StatementList;

static void statement_list_init(StatementList *list)
{
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

static bool statement_list_push(StatementList *list, Statement st)
{
    if (list->count == list->capacity) {
        size_t new_cap = (list->capacity == 0) ? 64 : list->capacity * 2;
        Statement *grown = realloc(list->items, new_cap * sizeof(Statement));
        if (grown == NULL) {
            return false;
        }
        list->items = grown;
        list->capacity = new_cap;
    }
    list->items[list->count++] = st;
    return true;
}

static void statement_list_free(StatementList *list)
{
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

/* ==================== Mnemonic / register / syscall tables ==================== */

typedef struct {
    const char *name;
    Opcode opcode;
} MnemonicEntry;

static const MnemonicEntry MNEMONICS[] = {
    {"NOP", OP_NOP},   {"MOV", OP_MOV}, {"ADD", OP_ADD},   {"SUB", OP_SUB},   {"MUL", OP_MUL},
    {"DIV", OP_DIV},   {"MOD", OP_MOD}, {"AND", OP_AND},   {"OR", OP_OR},     {"XOR", OP_XOR},
    {"NOT", OP_NOT},   {"SHL", OP_SHL}, {"SHR", OP_SHR},   {"LOAD", OP_LOAD}, {"STORE", OP_STORE},
    {"PUSH", OP_PUSH}, {"POP", OP_POP}, {"CMP", OP_CMP},   {"JMP", OP_JMP},   {"JZ", OP_JZ},
    {"JNZ", OP_JNZ},   {"JG", OP_JG},   {"JGE", OP_JGE},   {"JL", OP_JL},     {"JLE", OP_JLE},
    {"JB", OP_JB},     {"JAE", OP_JAE}, {"JA", OP_JA},     {"JBE", OP_JBE},   {"CALL", OP_CALL},
    {"RET", OP_RET},   {"SYS", OP_SYS}, {"HALT", OP_HALT},
};
#define NUM_MNEMONICS (sizeof(MNEMONICS) / sizeof(MNEMONICS[0]))

typedef struct {
    const char *name;
    int code;
} SyscallEntry;

static const SyscallEntry SYSCALLS[] = {
    {"PRINT_INT", 0x01},
    {"PRINT_CHAR", 0x02},
    {"READ_INT", 0x03},
    {"EXIT", 0x04},
};
#define NUM_SYSCALLS (sizeof(SYSCALLS) / sizeof(SYSCALLS[0]))

static bool ident_equals(const char *ident, const char *keyword)
{
    while (*ident != '\0' && *keyword != '\0') {
        if (tolower((unsigned char)*ident) != tolower((unsigned char)*keyword)) {
            return false;
        }
        ident++;
        keyword++;
    }
    return *ident == '\0' && *keyword == '\0';
}

static bool lookup_mnemonic(const char *ident, Opcode *out)
{
    size_t i;
    for (i = 0; i < NUM_MNEMONICS; i++) {
        if (ident_equals(ident, MNEMONICS[i].name)) {
            *out = MNEMONICS[i].opcode;
            return true;
        }
    }
    return false;
}

static bool try_syscall_name(const char *ident, int64_t *out_code)
{
    size_t i;
    for (i = 0; i < NUM_SYSCALLS; i++) {
        if (ident_equals(ident, SYSCALLS[i].name)) {
            *out_code = SYSCALLS[i].code;
            return true;
        }
    }
    return false;
}

/* Accepts any "Rn"/"rn" form (n = one or more digits), returning the
 * parsed index even if it's outside 0-7 -- the caller reports a specific
 * "register out of range" error rather than this silently falling
 * through to a confusing "undefined label R8". */
static bool try_parse_register(const char *ident, int *out_index)
{
    if (ident[0] != 'R' && ident[0] != 'r') {
        return false;
    }
    const char *digits = ident + 1;
    if (!isdigit((unsigned char)*digits)) {
        return false;
    }
    long value = 0;
    while (isdigit((unsigned char)*digits)) {
        value = value * 10 + (*digits - '0');
        digits++;
        if (value > 999) {
            break; /* clearly not a real register; stop before this gets silly */
        }
    }
    if (*digits != '\0') {
        return false; /* trailing non-digits -- not a register token at all */
    }
    *out_index = (int)value;
    return true;
}

/* ==================== Parser state ==================== */

typedef struct {
    const TokenList *tokens;
    size_t pos;
    bool had_error;
    char error[256];
    int error_line;
} Parser;

static const Token *cur(const Parser *ps)
{
    return &ps->tokens->tokens[ps->pos];
}

static void advance(Parser *ps)
{
    if (ps->pos + 1 < ps->tokens->count) {
        ps->pos++;
    }
}

static void fail(Parser *ps, int line, const char *fmt, ...)
{
    if (ps->had_error) {
        return; /* keep the first error */
    }
    ps->had_error = true;
    ps->error_line = line;
    va_list args;
    va_start(args, fmt);
    vsnprintf(ps->error, sizeof(ps->error), fmt, args);
    va_end(args);
}

typedef struct {
    bool in_data;
    bool data_seen;
    uint16_t code_cursor;
    uint16_t data_cursor;
    bool code_started;
    bool data_started;
    uint16_t code_start_addr;
    uint16_t data_start_addr;
} SectionState;

static uint16_t *active_cursor(SectionState *sec)
{
    return sec->in_data ? &sec->data_cursor : &sec->code_cursor;
}

static void note_section_start(SectionState *sec, bool in_data, uint16_t address)
{
    if (in_data) {
        if (!sec->data_started) {
            sec->data_started = true;
            sec->data_start_addr = address;
        }
    } else {
        if (!sec->code_started) {
            sec->code_started = true;
            sec->code_start_addr = address;
        }
    }
}

/* ==================== Operand parsing ==================== */

static ParsedOperand parse_operand(Parser *ps, Opcode context_opcode)
{
    ParsedOperand op = {0};
    op.kind = OPKIND_NONE;
    op.line = cur(ps)->line;

    if (cur(ps)->type == TOK_LBRACKET) {
        advance(ps);
        if (cur(ps)->type == TOK_IDENT) {
            int reg_index;
            if (try_parse_register(cur(ps)->text, &reg_index)) {
                op.kind = OPKIND_REG_INDIRECT;
                op.reg_index = reg_index;
            } else {
                op.kind = OPKIND_MEM_LABEL;
                op.label = cur(ps)->text;
            }
            advance(ps);
        } else if (cur(ps)->type == TOK_NUMBER) {
            op.kind = OPKIND_MEM_NUMBER;
            op.number = cur(ps)->num_value;
            advance(ps);
        } else {
            fail(ps, cur(ps)->line, "expected a register, number, or label inside '['");
            return op;
        }
        if (cur(ps)->type != TOK_RBRACKET) {
            fail(ps, cur(ps)->line, "expected ']'");
            return op;
        }
        advance(ps);
        return op;
    }

    if (cur(ps)->type == TOK_IDENT) {
        int reg_index;
        if (try_parse_register(cur(ps)->text, &reg_index)) {
            op.kind = OPKIND_REG;
            op.reg_index = reg_index;
            advance(ps);
            return op;
        }
        if (context_opcode == OP_SYS) {
            int64_t code;
            if (try_syscall_name(cur(ps)->text, &code)) {
                op.kind = OPKIND_NUMBER;
                op.number = code;
                advance(ps);
                return op;
            }
        }
        op.kind = OPKIND_LABEL;
        op.label = cur(ps)->text;
        advance(ps);
        return op;
    }

    if (cur(ps)->type == TOK_NUMBER) {
        op.kind = OPKIND_NUMBER;
        op.number = cur(ps)->num_value;
        advance(ps);
        return op;
    }

    fail(ps, cur(ps)->line, "expected an operand (register, number, label, or '[...]')");
    return op;
}

/* Resolves the AddrMode a syntactic operand must encode as, given the
 * legal-mode bitmask for its position (instruction_opcode_modes,
 * DESIGN.md section 7.3). A bare (non-bracketed, non-register) operand
 * infers its mode from what's legal here (IMM16, else MEM_DIRECT, else
 * IMM8) -- except when the position's *only* legal modes are
 * {MEM_DIRECT, REG_INDIRECT} (LOAD's address / STORE's address), where a
 * bare operand is rejected: those positions require the bracket syntax
 * to disambiguate MEM_DIRECT from REG_INDIRECT (DESIGN.md section 11.1). */
static bool resolve_operand_mode(const ParsedOperand *op, uint8_t legal_mask, AddrMode *out_mode,
                                 char *err, size_t err_size)
{
    switch (op->kind) {
    case OPKIND_REG:
        if (!(legal_mask & (uint8_t)(1u << MODE_REG))) {
            snprintf(err, err_size, "a register is not a legal operand here");
            return false;
        }
        if (op->reg_index < 0 || op->reg_index > 7) {
            snprintf(err, err_size, "register index R%d is out of range (must be R0-R7)",
                     op->reg_index);
            return false;
        }
        *out_mode = MODE_REG;
        return true;

    case OPKIND_REG_INDIRECT:
        if (!(legal_mask & (uint8_t)(1u << MODE_REG_INDIRECT))) {
            snprintf(err, err_size, "a register-indirect [Rn] operand is not legal here");
            return false;
        }
        if (op->reg_index < 0 || op->reg_index > 7) {
            snprintf(err, err_size, "register index R%d is out of range (must be R0-R7)",
                     op->reg_index);
            return false;
        }
        *out_mode = MODE_REG_INDIRECT;
        return true;

    case OPKIND_MEM_NUMBER:
    case OPKIND_MEM_LABEL:
        if (!(legal_mask & (uint8_t)(1u << MODE_MEM_DIRECT))) {
            snprintf(err, err_size, "a [address] operand is not legal here");
            return false;
        }
        *out_mode = MODE_MEM_DIRECT;
        return true;

    case OPKIND_NUMBER:
    case OPKIND_LABEL: {
        uint8_t mem_and_indirect = (uint8_t)((1u << MODE_MEM_DIRECT) | (1u << MODE_REG_INDIRECT));
        if (legal_mask == mem_and_indirect) {
            snprintf(err, err_size,
                     "this operand must be a memory reference in brackets, e.g. [addr] or "
                     "[Rn]");
            return false;
        }
        if (legal_mask & (uint8_t)(1u << MODE_IMM16)) {
            *out_mode = MODE_IMM16;
            return true;
        }
        if (legal_mask & (uint8_t)(1u << MODE_MEM_DIRECT)) {
            *out_mode = MODE_MEM_DIRECT;
            return true;
        }
        if (legal_mask & (uint8_t)(1u << MODE_IMM8)) {
            *out_mode = MODE_IMM8;
            return true;
        }
        snprintf(err, err_size, "no immediate or address form of this operand is legal here");
        return false;
    }

    case OPKIND_NONE:
        break;
    }
    snprintf(err, err_size, "internal error: unresolved operand kind");
    return false;
}

static int mode_byte_size(AddrMode mode)
{
    switch (mode) {
    case MODE_NONE:
        return 0;
    case MODE_REG:
    case MODE_REG_INDIRECT:
    case MODE_IMM8:
        return 1;
    case MODE_IMM16:
    case MODE_MEM_DIRECT:
        return 2;
    }
    return 0;
}

/* ==================== Statement parsing (pass 1: parse + address/size) ==================== */

static void parse_instruction_statement(Parser *ps, StatementList *stmts, SectionState *sec,
                                        Opcode opcode, int line)
{
    Statement st = {0};
    st.kind = STMT_INSTRUCTION;
    st.line = line;
    st.opcode = opcode;
    st.operand_count = 0;
    st.in_data = sec->in_data;

    if (cur(ps)->type != TOK_NEWLINE && cur(ps)->type != TOK_EOF) {
        st.op1 = parse_operand(ps, opcode);
        if (ps->had_error) {
            return;
        }
        st.operand_count = 1;
        if (cur(ps)->type == TOK_COMMA) {
            advance(ps);
            st.op2 = parse_operand(ps, opcode);
            if (ps->had_error) {
                return;
            }
            st.operand_count = 2;
        }
    }

    uint8_t op1_modes = 0;
    uint8_t op2_modes = 0;
    instruction_opcode_modes(opcode, &op1_modes, &op2_modes); /* always succeeds: opcode is real */

    uint8_t modes_none = (uint8_t)(1u << MODE_NONE);
    int expected_count = (op1_modes == modes_none) ? 0 : ((op2_modes == modes_none) ? 1 : 2);
    if (st.operand_count != expected_count) {
        fail(ps, line, "expected %d operand(s), got %d", expected_count, st.operand_count);
        return;
    }

    char errbuf[128];
    if (expected_count >= 1) {
        if (!resolve_operand_mode(&st.op1, op1_modes, &st.op1_mode, errbuf, sizeof(errbuf))) {
            fail(ps, st.op1.line, "%s", errbuf);
            return;
        }
    } else {
        st.op1_mode = MODE_NONE;
    }
    if (expected_count >= 2) {
        if (!resolve_operand_mode(&st.op2, op2_modes, &st.op2_mode, errbuf, sizeof(errbuf))) {
            fail(ps, st.op2.line, "%s", errbuf);
            return;
        }
    } else {
        st.op2_mode = MODE_NONE;
    }

    st.address = *active_cursor(sec);
    st.encoded_size = 2 + mode_byte_size(st.op1_mode) + mode_byte_size(st.op2_mode);
    note_section_start(sec, st.in_data, st.address);
    *active_cursor(sec) = (uint16_t)(*active_cursor(sec) + st.encoded_size);

    if (!statement_list_push(stmts, st)) {
        fail(ps, line, "out of memory");
    }
}

static void parse_directive_statement(Parser *ps, StatementList *stmts, SectionState *sec,
                                      const char *directive_name, int line)
{
    advance(ps); /* consume the TOK_DIRECTIVE token */

    if (ident_equals(directive_name, "org")) {
        if (cur(ps)->type != TOK_NUMBER) {
            fail(ps, line, ".org requires a numeric address");
            return;
        }
        int64_t value = cur(ps)->num_value;
        advance(ps);
        if (value < 0 || value > 0xFFFF) {
            fail(ps, line, ".org value %lld is out of range (must be 0-65535)", (long long)value);
            return;
        }
        uint16_t *cursor = active_cursor(sec);
        if ((uint16_t)value < *cursor) {
            fail(ps, line,
                 ".org would move the address counter backward (currently at 0x%04X, requested "
                 "0x%04X)",
                 *cursor, (unsigned)value);
            return;
        }
        *cursor = (uint16_t)value;

        Statement st = {0};
        st.kind = STMT_ORG;
        st.line = line;
        if (!statement_list_push(stmts, st)) {
            fail(ps, line, "out of memory");
        }
        return;
    }

    if (ident_equals(directive_name, "data")) {
        if (sec->data_seen) {
            fail(ps, line, ".data may only appear once");
            return;
        }
        sec->data_seen = true;
        sec->in_data = true;
        /* The data section defaults to starting immediately after the
         * code section. Without this, the data cursor would start at 0
         * independently and overlap the code -- which vm_load rejects as
         * overlapping regions (DESIGN.md section 10), or worse, silently
         * overwrites code if the sizes happen to line up. An explicit
         * `.org` after `.data` can still move it further forward. */
        sec->data_cursor = sec->code_cursor;

        Statement st = {0};
        st.kind = STMT_DATA_SECTION;
        st.line = line;
        if (!statement_list_push(stmts, st)) {
            fail(ps, line, "out of memory");
        }
        return;
    }

    if (ident_equals(directive_name, "string")) {
        if (cur(ps)->type != TOK_STRING) {
            fail(ps, line, ".string requires a quoted string literal");
            return;
        }
        Statement st = {0};
        st.kind = STMT_STRING;
        st.line = line;
        st.in_data = sec->in_data;
        st.string_bytes = cur(ps)->str_value;
        st.string_len = cur(ps)->str_len;
        st.address = *active_cursor(sec);
        st.encoded_size = (int)(st.string_len + 1); /* + NUL terminator, section 11.2 */
        advance(ps);
        note_section_start(sec, st.in_data, st.address);
        *active_cursor(sec) = (uint16_t)(*active_cursor(sec) + st.encoded_size);
        if (!statement_list_push(stmts, st)) {
            fail(ps, line, "out of memory");
        }
        return;
    }

    if (ident_equals(directive_name, "word")) {
        ParsedOperand operand = parse_operand(ps, OP_NOP);
        if (ps->had_error) {
            return;
        }
        if (operand.kind != OPKIND_NUMBER && operand.kind != OPKIND_LABEL) {
            fail(ps, line, ".word requires a number or a label");
            return;
        }
        Statement st = {0};
        st.kind = STMT_WORD;
        st.line = line;
        st.in_data = sec->in_data;
        st.word_operand = operand;
        st.address = *active_cursor(sec);
        st.encoded_size = 2;
        note_section_start(sec, st.in_data, st.address);
        *active_cursor(sec) = (uint16_t)(*active_cursor(sec) + 2);
        if (!statement_list_push(stmts, st)) {
            fail(ps, line, "out of memory");
        }
        return;
    }

    if (ident_equals(directive_name, "byte")) {
        ParsedOperand operand = parse_operand(ps, OP_NOP);
        if (ps->had_error) {
            return;
        }
        if (operand.kind != OPKIND_NUMBER) {
            fail(ps, line, ".byte requires a plain number (0-255)");
            return;
        }
        Statement st = {0};
        st.kind = STMT_BYTE;
        st.line = line;
        st.in_data = sec->in_data;
        st.byte_operand = operand;
        st.address = *active_cursor(sec);
        st.encoded_size = 1;
        note_section_start(sec, st.in_data, st.address);
        *active_cursor(sec) = (uint16_t)(*active_cursor(sec) + 1);
        if (!statement_list_push(stmts, st)) {
            fail(ps, line, "out of memory");
        }
        return;
    }

    fail(ps, line, "unknown directive '.%s'", directive_name);
}

static void parse_line(Parser *ps, StatementList *stmts, SymbolTable *symtab, SectionState *sec)
{
    if (cur(ps)->type == TOK_IDENT) {
        size_t save = ps->pos;
        const char *name = cur(ps)->text;
        int name_line = cur(ps)->line;
        advance(ps);

        if (cur(ps)->type == TOK_COLON) {
            advance(ps);
            uint16_t addr = *active_cursor(sec);
            if (!symtab_define(symtab, name, addr)) {
                fail(ps, name_line, "label '%s' is already defined", name);
                return;
            }
            Statement st = {0};
            st.kind = STMT_LABEL;
            st.line = name_line;
            st.label_name = name;
            st.address = addr;
            if (!statement_list_push(stmts, st)) {
                fail(ps, name_line, "out of memory");
                return;
            }
            if (cur(ps)->type == TOK_NEWLINE || cur(ps)->type == TOK_EOF) {
                return; /* label alone on its line */
            }
            /* fall through: an instruction/directive follows on the same line */
        } else {
            ps->pos = save; /* not a label after all -- rewind and reparse below */
        }
    }

    if (cur(ps)->type == TOK_DIRECTIVE) {
        parse_directive_statement(ps, stmts, sec, cur(ps)->text, cur(ps)->line);
        return;
    }

    if (cur(ps)->type == TOK_IDENT) {
        int line = cur(ps)->line;
        const char *mnemonic = cur(ps)->text;
        Opcode opcode;
        if (!lookup_mnemonic(mnemonic, &opcode)) {
            fail(ps, line, "unknown mnemonic '%s'", mnemonic);
            return;
        }
        advance(ps);
        parse_instruction_statement(ps, stmts, sec, opcode, line);
        return;
    }

    fail(ps, cur(ps)->line, "expected a label, directive, or instruction");
}

static void build_statements(Parser *ps, StatementList *stmts, SymbolTable *symtab,
                             SectionState *sec)
{
    while (cur(ps)->type != TOK_EOF) {
        if (cur(ps)->type == TOK_NEWLINE) {
            advance(ps);
            continue;
        }
        parse_line(ps, stmts, symtab, sec);
        if (ps->had_error) {
            return;
        }
        if (cur(ps)->type == TOK_NEWLINE) {
            advance(ps);
        } else if (cur(ps)->type != TOK_EOF) {
            fail(ps, cur(ps)->line, "expected end of line");
            return;
        }
    }

    if (!sec->code_started) {
        fail(ps, cur(ps)->line, "program contains no instructions or code-section data");
    }
}

/* ==================== Pass 2: resolve + emit ==================== */

static void set_result_error(ParseResult *out, int line, const char *fmt, ...)
{
    out->ok = false;
    out->error_line = line;
    va_list args;
    va_start(args, fmt);
    vsnprintf(out->error, sizeof(out->error), fmt, args);
    va_end(args);
}

static bool resolve_value(const ParsedOperand *op, const SymbolTable *symtab, int64_t *out_value,
                          char *err, size_t err_size)
{
    switch (op->kind) {
    case OPKIND_REG:
    case OPKIND_REG_INDIRECT:
        *out_value = op->reg_index;
        return true;
    case OPKIND_NUMBER:
    case OPKIND_MEM_NUMBER:
        *out_value = op->number;
        return true;
    case OPKIND_LABEL:
    case OPKIND_MEM_LABEL: {
        uint16_t addr;
        if (!symtab_lookup(symtab, op->label, &addr)) {
            snprintf(err, err_size, "undefined label '%s'", op->label);
            return false;
        }
        *out_value = addr;
        return true;
    }
    case OPKIND_NONE:
        *out_value = 0;
        return true;
    }
    snprintf(err, err_size, "internal error: unresolved operand kind");
    return false;
}

static bool range_check(AddrMode mode, int64_t value, char *err, size_t err_size)
{
    switch (mode) {
    case MODE_IMM16:
    case MODE_MEM_DIRECT:
        if (value < -32768 || value > 65535) {
            snprintf(err, err_size, "immediate value %lld is out of range (-32768 to 65535)",
                     (long long)value);
            return false;
        }
        return true;
    case MODE_IMM8:
        if (value < 0 || value > 255) {
            snprintf(err, err_size, "immediate value %lld is out of range (0 to 255)",
                     (long long)value);
            return false;
        }
        return true;
    case MODE_REG:
    case MODE_REG_INDIRECT:
    case MODE_NONE:
        return true; /* register range already validated during parsing */
    }
    return true;
}

static void emit_operand_bytes(uint8_t *target, uint16_t *addr, AddrMode mode, int64_t value)
{
    switch (mode) {
    case MODE_NONE:
        break;
    case MODE_REG:
    case MODE_REG_INDIRECT:
    case MODE_IMM8:
        target[*addr] = (uint8_t)value;
        *addr = (uint16_t)(*addr + 1);
        break;
    case MODE_IMM16:
    case MODE_MEM_DIRECT: {
        uint16_t v16 = (uint16_t)value;
        target[*addr] = (uint8_t)(v16 & 0xFFu);
        target[*addr + 1] = (uint8_t)(v16 >> 8);
        *addr = (uint16_t)(*addr + 2);
        break;
    }
    }
}

static bool emit_instruction(const Statement *st, const SymbolTable *symtab, uint8_t *target,
                             ParseResult *out)
{
    char errbuf[128];
    int64_t v1 = 0;
    int64_t v2 = 0;

    if (st->operand_count >= 1) {
        if (!resolve_value(&st->op1, symtab, &v1, errbuf, sizeof(errbuf))) {
            set_result_error(out, st->op1.line, "%s", errbuf);
            return false;
        }
        if (!range_check(st->op1_mode, v1, errbuf, sizeof(errbuf))) {
            set_result_error(out, st->op1.line, "%s", errbuf);
            return false;
        }
    }
    if (st->operand_count >= 2) {
        if (!resolve_value(&st->op2, symtab, &v2, errbuf, sizeof(errbuf))) {
            set_result_error(out, st->op2.line, "%s", errbuf);
            return false;
        }
        if (!range_check(st->op2_mode, v2, errbuf, sizeof(errbuf))) {
            set_result_error(out, st->op2.line, "%s", errbuf);
            return false;
        }
    }

    uint16_t addr = st->address;
    target[addr] = (uint8_t)st->opcode;
    target[addr + 1u] = (uint8_t)((st->op1_mode << 4) | st->op2_mode);
    addr = (uint16_t)(addr + 2);

    emit_operand_bytes(target, &addr, st->op1_mode, v1);
    emit_operand_bytes(target, &addr, st->op2_mode, v2);
    return true;
}

static bool emit_statement(const Statement *st, const SymbolTable *symtab, ParseResult *out)
{
    uint8_t *target = st->in_data ? out->data : out->code;

    switch (st->kind) {
    case STMT_LABEL:
    case STMT_ORG:
    case STMT_DATA_SECTION:
        return true;

    case STMT_STRING:
        if (st->string_len > 0) {
            memcpy(&target[st->address], st->string_bytes, st->string_len);
        }
        target[st->address + st->string_len] = 0;
        return true;

    case STMT_WORD: {
        char errbuf[128];
        int64_t value;
        if (!resolve_value(&st->word_operand, symtab, &value, errbuf, sizeof(errbuf))) {
            set_result_error(out, st->line, "%s", errbuf);
            return false;
        }
        uint16_t v16 = (uint16_t)value;
        target[st->address] = (uint8_t)(v16 & 0xFFu);
        target[st->address + 1u] = (uint8_t)(v16 >> 8);
        return true;
    }

    case STMT_BYTE: {
        int64_t value = st->byte_operand.number;
        if (value < 0 || value > 255) {
            set_result_error(out, st->line, ".byte value %lld is out of range (0-255)",
                             (long long)value);
            return false;
        }
        target[st->address] = (uint8_t)value;
        return true;
    }

    case STMT_INSTRUCTION:
        return emit_instruction(st, symtab, target, out);
    }

    set_result_error(out, st->line, "internal error: unknown statement kind");
    return false;
}

/* ==================== Top level ==================== */

void assemble_tokens(const TokenList *tokens, ParseResult *out)
{
    memset(out, 0, sizeof(*out));

    Parser ps;
    ps.tokens = tokens;
    ps.pos = 0;
    ps.had_error = false;
    ps.error[0] = '\0';
    ps.error_line = 0;

    StatementList stmts;
    statement_list_init(&stmts);
    SymbolTable symtab;
    symtab_init(&symtab);
    SectionState sec;
    memset(&sec, 0, sizeof(sec));

    build_statements(&ps, &stmts, &symtab, &sec);

    if (ps.had_error) {
        out->ok = false;
        out->error_line = ps.error_line;
        strncpy(out->error, ps.error, sizeof(out->error) - 1);
        statement_list_free(&stmts);
        symtab_free(&symtab);
        return;
    }

    size_t i;
    for (i = 0; i < stmts.count; i++) {
        if (!emit_statement(&stmts.items[i], &symtab, out)) {
            statement_list_free(&stmts);
            symtab_free(&symtab);
            return; /* emit_statement already called set_result_error */
        }
    }

    out->code_addr = sec.code_start_addr;
    out->code_size = (uint16_t)(sec.code_cursor - sec.code_start_addr);
    out->data_addr = sec.data_start_addr;
    out->data_size = sec.data_started ? (uint16_t)(sec.data_cursor - sec.data_start_addr) : 0;
    out->entry_point = sec.code_start_addr;
    out->ok = true;

    statement_list_free(&stmts);
    symtab_free(&symtab);
}

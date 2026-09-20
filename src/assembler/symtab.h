#ifndef BYTEVM_ASM_SYMTAB_H
#define BYTEVM_ASM_SYMTAB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    char *name; /* owned */
    uint16_t address;
} Symbol;

typedef struct {
    Symbol *symbols;
    size_t count;
    size_t capacity;
} SymbolTable;

void symtab_init(SymbolTable *table);
void symtab_free(SymbolTable *table);

/* Defines `name` at `address`. Returns false without modifying the table
 * if `name` is already defined -- duplicate label definitions are a
 * compile error (DESIGN.md section 11.3 lists "undefined label" as an
 * error; a duplicate definition is the same class of programmer mistake
 * and gets caught here for the same reason). Returns false (out of
 * memory) if growing the table fails. */
bool symtab_define(SymbolTable *table, const char *name, uint16_t address);

/* Writes *out_address and returns true if `name` is defined. */
bool symtab_lookup(const SymbolTable *table, const char *name, uint16_t *out_address);

#endif /* BYTEVM_ASM_SYMTAB_H */

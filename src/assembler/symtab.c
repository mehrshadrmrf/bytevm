#include "symtab.h"

#include <stdlib.h>
#include <string.h>

#define INITIAL_CAPACITY 32

void symtab_init(SymbolTable *table)
{
    table->symbols = NULL;
    table->count = 0;
    table->capacity = 0;
}

void symtab_free(SymbolTable *table)
{
    size_t i;
    for (i = 0; i < table->count; i++) {
        free(table->symbols[i].name);
    }
    free(table->symbols);
    table->symbols = NULL;
    table->count = 0;
    table->capacity = 0;
}

bool symtab_lookup(const SymbolTable *table, const char *name, uint16_t *out_address)
{
    size_t i;
    for (i = 0; i < table->count; i++) {
        if (strcmp(table->symbols[i].name, name) == 0) {
            *out_address = table->symbols[i].address;
            return true;
        }
    }
    return false;
}

bool symtab_define(SymbolTable *table, const char *name, uint16_t address)
{
    uint16_t existing;
    if (symtab_lookup(table, name, &existing)) {
        return false; /* duplicate definition */
    }

    if (table->count == table->capacity) {
        size_t new_cap = (table->capacity == 0) ? INITIAL_CAPACITY : table->capacity * 2;
        Symbol *grown = realloc(table->symbols, new_cap * sizeof(Symbol));
        if (grown == NULL) {
            return false;
        }
        table->symbols = grown;
        table->capacity = new_cap;
    }

    char *owned_name = malloc(strlen(name) + 1);
    if (owned_name == NULL) {
        return false;
    }
    strcpy(owned_name, name);

    table->symbols[table->count].name = owned_name;
    table->symbols[table->count].address = address;
    table->count++;
    return true;
}

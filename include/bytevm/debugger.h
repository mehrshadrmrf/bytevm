#ifndef BYTEVM_DEBUGGER_H
#define BYTEVM_DEBUGGER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "bytevm/vm.h"

#define DEBUGGER_MAX_BREAKPOINTS 64

typedef struct {
    VM *vm;

    /* The original .bvm image, kept so `reset` can re-run the full load
     * sequence rather than approximating it (DESIGN.md section 13):
     * memory is re-zeroed and the blobs re-copied, discarding runtime
     * writes including self-modifying code. Owned by the caller. */
    const uint8_t *image;
    size_t image_size;

    uint16_t breakpoints[DEBUGGER_MAX_BREAKPOINTS];
    size_t breakpoint_count;

    bool quit_requested;
} Debugger;

/* vm must already have `image` loaded. The debugger borrows both. */
void debugger_init(Debugger *dbg, VM *vm, const uint8_t *image, size_t image_size);

/* Executes one REPL command line (DESIGN.md section 13's table), writing
 * all output to `out`. Unknown commands and bad arguments produce an
 * error line rather than terminating the session. Returns false once the
 * user has asked to quit. Separating this from the input loop is what
 * makes the debugger unit-testable: tests drive it with command strings
 * and a temp-file stream, with no terminal involved. */
bool debugger_execute(Debugger *dbg, const char *line, FILE *out);

/* Reads commands from `in` until EOF or `quit`, prompting on `out`. */
void debugger_repl(Debugger *dbg, FILE *in, FILE *out);

#endif /* BYTEVM_DEBUGGER_H */

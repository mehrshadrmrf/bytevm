#include "bytevm/debugger.h"

#include <stdlib.h>
#include <string.h>

#include "bytevm/disassembler.h"
#include "bytevm/loader.h"

#define CONTINUE_STEP_LIMIT 10000000

void debugger_init(Debugger *dbg, VM *vm, const uint8_t *image, size_t image_size)
{
    dbg->vm = vm;
    dbg->image = image;
    dbg->image_size = image_size;
    dbg->breakpoint_count = 0;
    dbg->quit_requested = false;
}

static bool has_breakpoint(const Debugger *dbg, uint16_t addr)
{
    size_t i;
    for (i = 0; i < dbg->breakpoint_count; i++) {
        if (dbg->breakpoints[i] == addr) {
            return true;
        }
    }
    return false;
}

/* ---- argument parsing ---- */

/* Accepts decimal or 0x-prefixed hex. Returns false on anything that
 * isn't a complete, in-range number. */
static bool parse_number(const char *s, long *out)
{
    if (s == NULL || *s == '\0') {
        return false;
    }
    char *end = NULL;
    long base = (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) ? 16 : 10;
    long v = strtol(s, &end, (int)base);
    if (end == s || *end != '\0' || v < 0 || v > 0xFFFF) {
        return false;
    }
    *out = v;
    return true;
}

/* ---- individual commands ---- */

static void cmd_registers(const Debugger *dbg, FILE *out)
{
    const CPU *cpu = &dbg->vm->cpu;
    int i;
    for (i = 0; i < 8; i++) {
        fprintf(out, "R%d=0x%04X%s", i, cpu->r[i], (i == 3 || i == 7) ? "\n" : "  ");
    }
    fprintf(out, "PC=0x%04X  SP=0x%04X  FLAGS=0x%04X [%c%c%c%c]\n", cpu->pc, cpu->sp, cpu->flags,
            cpu_get_flag(cpu, CPU_FLAG_Z) ? 'Z' : '-', cpu_get_flag(cpu, CPU_FLAG_C) ? 'C' : '-',
            cpu_get_flag(cpu, CPU_FLAG_S) ? 'S' : '-', cpu_get_flag(cpu, CPU_FLAG_O) ? 'O' : '-');
    if (dbg->vm->halted) {
        fprintf(out, "halted: %s\n", vm_result_str(dbg->vm->last_error));
    }
}

static void report_stop(const Debugger *dbg, FILE *out)
{
    const VM *vm = dbg->vm;
    if (vm->halted) {
        if (vm->last_error != VM_OK) {
            fprintf(out, "halted: %s at PC=0x%04X\n", vm_result_str(vm->last_error), vm->error_pc);
        } else {
            fprintf(out, "halted\n");
        }
        return;
    }
    char text[128];
    uint16_t unused;
    disassemble_one(&vm->memory, vm->cpu.pc, text, sizeof(text), &unused);
    fprintf(out, "PC=0x%04X: %s\n", vm->cpu.pc, text);
}

static void cmd_step(Debugger *dbg, const char *arg, FILE *out)
{
    long n = 1;
    if (arg != NULL && !parse_number(arg, &n)) {
        fprintf(out, "error: step takes a count\n");
        return;
    }
    long i;
    for (i = 0; i < n; i++) {
        if (dbg->vm->halted) {
            break;
        }
        vm_step(dbg->vm);
    }
    report_stop(dbg, out);
}

static void cmd_continue(Debugger *dbg, FILE *out)
{
    if (dbg->vm->halted) {
        fprintf(out, "halted\n");
        return;
    }

    /* Step once unconditionally before checking breakpoints: otherwise
     * `continue` while already stopped AT a breakpoint would re-trigger
     * that same breakpoint immediately and never make progress. */
    vm_step(dbg->vm);

    long steps = 1;
    while (!dbg->vm->halted && steps < CONTINUE_STEP_LIMIT) {
        if (has_breakpoint(dbg, dbg->vm->cpu.pc)) {
            /* Breakpoints fire BEFORE the instruction at that address
             * executes (DESIGN.md section 13), so stop here with PC
             * still pointing at it and nothing fetched. */
            fprintf(out, "breakpoint at 0x%04X\n", dbg->vm->cpu.pc);
            report_stop(dbg, out);
            return;
        }
        vm_step(dbg->vm);
        steps++;
    }

    if (!dbg->vm->halted) {
        fprintf(out, "step limit (%d) reached without halting\n", CONTINUE_STEP_LIMIT);
    }
    report_stop(dbg, out);
}

static void cmd_break(Debugger *dbg, const char *arg, FILE *out)
{
    long addr;
    if (!parse_number(arg, &addr)) {
        fprintf(out, "error: break takes an address\n");
        return;
    }
    if (has_breakpoint(dbg, (uint16_t)addr)) {
        fprintf(out, "breakpoint at 0x%04X already set\n", (unsigned)addr);
        return;
    }
    if (dbg->breakpoint_count == DEBUGGER_MAX_BREAKPOINTS) {
        fprintf(out, "error: too many breakpoints (max %d)\n", DEBUGGER_MAX_BREAKPOINTS);
        return;
    }
    dbg->breakpoints[dbg->breakpoint_count++] = (uint16_t)addr;
    fprintf(out, "breakpoint set at 0x%04X\n", (unsigned)addr);
}

static void cmd_delete(Debugger *dbg, const char *arg, FILE *out)
{
    long addr;
    if (!parse_number(arg, &addr)) {
        fprintf(out, "error: delete takes an address\n");
        return;
    }
    size_t i;
    for (i = 0; i < dbg->breakpoint_count; i++) {
        if (dbg->breakpoints[i] == (uint16_t)addr) {
            dbg->breakpoints[i] = dbg->breakpoints[dbg->breakpoint_count - 1];
            dbg->breakpoint_count--;
            fprintf(out, "breakpoint at 0x%04X removed\n", (unsigned)addr);
            return;
        }
    }
    fprintf(out, "error: no breakpoint at 0x%04X\n", (unsigned)addr);
}

static void hex_dump(const Memory *mem, uint16_t addr, long count, FILE *out)
{
    long i = 0;
    while (i < count) {
        uint16_t line_addr = (uint16_t)(addr + i);
        fprintf(out, "0x%04X:", line_addr);
        long j;
        for (j = 0; j < 16 && i + j < count; j++) {
            fprintf(out, " %02X", mem_read8(mem, (uint16_t)(addr + i + j)));
        }
        fprintf(out, "\n");
        i += 16;
    }
}

static void cmd_memory(Debugger *dbg, const char *addr_arg, const char *count_arg, FILE *out)
{
    long addr;
    if (!parse_number(addr_arg, &addr)) {
        fprintf(out, "error: memory takes an address and an optional byte count\n");
        return;
    }
    long count = 16;
    if (count_arg != NULL && !parse_number(count_arg, &count)) {
        fprintf(out, "error: memory takes an address and an optional byte count\n");
        return;
    }
    if (count <= 0) {
        fprintf(out, "error: count must be positive\n");
        return;
    }
    hex_dump(&dbg->vm->memory, (uint16_t)addr, count, out);
}

static void cmd_stack(Debugger *dbg, FILE *out)
{
    uint16_t sp = dbg->vm->cpu.sp;
    /* SP is the next FREE slot (empty-descending, DESIGN.md section 6),
     * so the live stack is everything above it -- (0xFFFF - SP) bytes. */
    if (sp == CPU_INITIAL_SP) {
        fprintf(out, "stack is empty (SP=0x%04X)\n", sp);
        return;
    }
    long count = 0xFFFF - (long)sp + 1;
    fprintf(out, "stack (SP=0x%04X, %ld bytes live):\n", sp, count - 2);
    hex_dump(&dbg->vm->memory, (uint16_t)(sp + 2), count - 2, out);
}

static void cmd_disassemble(Debugger *dbg, const char *addr_arg, const char *count_arg, FILE *out)
{
    long addr;
    if (addr_arg == NULL) {
        addr = dbg->vm->cpu.pc; /* default to where execution sits */
    } else if (!parse_number(addr_arg, &addr)) {
        fprintf(out, "error: disassemble takes an address and an optional instruction count\n");
        return;
    }
    long count = 8;
    if (count_arg != NULL && !parse_number(count_arg, &count)) {
        fprintf(out, "error: disassemble takes an address and an optional instruction count\n");
        return;
    }
    if (count <= 0) {
        fprintf(out, "error: count must be positive\n");
        return;
    }

    uint16_t pc = (uint16_t)addr;
    long i;
    for (i = 0; i < count; i++) {
        char text[128];
        uint16_t next;
        disassemble_one(&dbg->vm->memory, pc, text, sizeof(text), &next);
        fprintf(out, "%c0x%04X: %s\n", (pc == dbg->vm->cpu.pc) ? '>' : ' ', pc, text);
        pc = next;
    }
}

static void cmd_reset(Debugger *dbg, FILE *out)
{
    VmResult r = vm_load(dbg->vm, dbg->image, dbg->image_size);
    if (r != VM_OK) {
        fprintf(out, "error: reload failed: %s\n", vm_result_str(r));
        return;
    }
    /* Breakpoints are debugger-session state, not VM state, so reset
     * deliberately preserves them (DESIGN.md section 13). */
    fprintf(out, "reset; PC=0x%04X, %zu breakpoint(s) kept\n", dbg->vm->cpu.pc,
            dbg->breakpoint_count);
}

/* ---- dispatch ---- */

bool debugger_execute(Debugger *dbg, const char *line, FILE *out)
{
    char buf[256];
    snprintf(buf, sizeof(buf), "%s", line);

    char *cmd = strtok(buf, " \t\r\n");
    if (cmd == NULL) {
        return !dbg->quit_requested; /* blank line */
    }
    char *arg1 = strtok(NULL, " \t\r\n");
    char *arg2 = strtok(NULL, " \t\r\n");

    if (strcmp(cmd, "registers") == 0 || strcmp(cmd, "r") == 0) {
        cmd_registers(dbg, out);
    } else if (strcmp(cmd, "step") == 0 || strcmp(cmd, "s") == 0) {
        cmd_step(dbg, arg1, out);
    } else if (strcmp(cmd, "continue") == 0 || strcmp(cmd, "c") == 0) {
        cmd_continue(dbg, out);
    } else if (strcmp(cmd, "break") == 0 || strcmp(cmd, "b") == 0) {
        cmd_break(dbg, arg1, out);
    } else if (strcmp(cmd, "delete") == 0 || strcmp(cmd, "d") == 0) {
        cmd_delete(dbg, arg1, out);
    } else if (strcmp(cmd, "memory") == 0 || strcmp(cmd, "m") == 0) {
        cmd_memory(dbg, arg1, arg2, out);
    } else if (strcmp(cmd, "stack") == 0) {
        cmd_stack(dbg, out);
    } else if (strcmp(cmd, "disassemble") == 0 || strcmp(cmd, "dis") == 0) {
        cmd_disassemble(dbg, arg1, arg2, out);
    } else if (strcmp(cmd, "reset") == 0) {
        cmd_reset(dbg, out);
    } else if (strcmp(cmd, "quit") == 0 || strcmp(cmd, "q") == 0) {
        dbg->quit_requested = true;
        return false;
    } else if (strcmp(cmd, "help") == 0 || strcmp(cmd, "h") == 0) {
        fprintf(out, "registers          print R0-R7, PC, SP, FLAGS\n"
                     "step [n]           execute n instructions (default 1)\n"
                     "continue           run until a breakpoint or HALT\n"
                     "break <addr>       set a breakpoint\n"
                     "delete <addr>      remove a breakpoint\n"
                     "memory <addr> [n]  hex-dump n bytes (default 16)\n"
                     "stack              dump the live stack\n"
                     "disassemble [addr] [n]  disassemble n instructions (default: at PC, 8)\n"
                     "reset              reload the program and reset CPU state\n"
                     "quit               exit\n");
    } else {
        fprintf(out, "error: unknown command '%s' (try 'help')\n", cmd);
    }

    return true;
}

void debugger_repl(Debugger *dbg, FILE *in, FILE *out)
{
    char line[256];
    fprintf(out, "ByteVM debugger. Type 'help' for commands.\n");
    report_stop(dbg, out);
    for (;;) {
        fprintf(out, "(bytevm) ");
        fflush(out);
        if (fgets(line, sizeof(line), in) == NULL) {
            fprintf(out, "\n");
            return;
        }
        if (!debugger_execute(dbg, line, out)) {
            return;
        }
    }
}

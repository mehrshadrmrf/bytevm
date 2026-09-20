#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bytevm/assembler.h"
#include "bytevm/debugger.h"
#include "bytevm/loader.h"
#include "test_framework.h"

/* Runs one command and captures everything it printed. debugger_execute
 * takes the output stream as a parameter precisely so this needs no
 * terminal and no global-stdout hijacking. */
static void run_cmd(Debugger *dbg, const char *cmd, char *out_buf, size_t out_buf_size)
{
    out_buf[0] = '\0';
    FILE *tmp = tmpfile();
    if (tmp == NULL) {
        return;
    }
    debugger_execute(dbg, cmd, tmp);
    fflush(tmp);
    rewind(tmp);
    size_t n = fread(out_buf, 1, out_buf_size - 1, tmp);
    out_buf[n] = '\0';
    fclose(tmp);
}

int test_debugger(void)
{
    int failures = 0;

    /* A program with a recognizable shape: a 3-iteration loop, so
     * breakpoints get hit repeatedly and `continue` has somewhere to go.
     *
     *   0x0000  MOV R0, 0
     *   0x0005  MOV R1, 3
     *   0x000A  ADD R0, R1      <- loop body starts here
     *   0x000E  SUB R1, 1
     *   0x0013  JNZ 0x000A
     *   0x0017  HALT
     */
    const char *src = "    MOV R0, 0\n"
                      "    MOV R1, 3\n"
                      "loop:\n"
                      "    ADD R0, R1\n"
                      "    SUB R1, 1\n"
                      "    JNZ loop\n"
                      "    HALT\n";

    AssemblyResult asm_result;
    assemble(src, &asm_result);
    TEST_ASSERT(asm_result.ok, "the debugger test program assembles", &failures);
    if (!asm_result.ok) {
        assembly_result_free(&asm_result);
        return failures;
    }

    VM vm;
    Debugger dbg;
    char buf[4096];

    vm_init(&vm);
    vm_load(&vm, asm_result.image, asm_result.image_size);
    debugger_init(&dbg, &vm, asm_result.image, asm_result.image_size);

    /* ---- registers ---- */
    run_cmd(&dbg, "registers", buf, sizeof(buf));
    TEST_ASSERT(strstr(buf, "R0=0x0000") != NULL, "registers prints R0", &failures);
    TEST_ASSERT(strstr(buf, "SP=0xFFFE") != NULL, "registers prints the initial SP", &failures);
    TEST_ASSERT(strstr(buf, "PC=0x0000") != NULL, "registers prints PC", &failures);

    /* ---- step ---- */
    run_cmd(&dbg, "step", buf, sizeof(buf));
    TEST_ASSERT_EQ_U16(vm.cpu.pc, 0x0005, "step executes exactly one instruction", &failures);
    run_cmd(&dbg, "step 2", buf, sizeof(buf));
    TEST_ASSERT_EQ_U16(vm.cpu.pc, 0x000E, "step 2 executes exactly two instructions", &failures);
    TEST_ASSERT_EQ_U16(vm.cpu.r[0], 3, "after three steps R0 == 3 (first ADD ran)", &failures);

    /* ---- disassemble ---- */
    run_cmd(&dbg, "disassemble 0x0000 3", buf, sizeof(buf));
    TEST_ASSERT(strstr(buf, "MOV R0, 0") != NULL, "disassemble shows the first instruction",
                &failures);
    TEST_ASSERT(strstr(buf, "0x000A") != NULL, "disassemble advances by real instruction lengths",
                &failures);

    /* ---- breakpoints, and the section 13 timing rule ---- */
    run_cmd(&dbg, "reset", buf, sizeof(buf));
    TEST_ASSERT_EQ_U16(vm.cpu.pc, 0x0000, "reset returns PC to the entry point", &failures);
    TEST_ASSERT_EQ_U16(vm.cpu.r[0], 0, "reset clears registers", &failures);

    run_cmd(&dbg, "break 0x000E", buf, sizeof(buf));
    TEST_ASSERT(strstr(buf, "breakpoint set") != NULL, "break reports success", &failures);

    run_cmd(&dbg, "continue", buf, sizeof(buf));
    TEST_ASSERT(strstr(buf, "breakpoint at 0x000E") != NULL, "continue stops at the breakpoint",
                &failures);
    TEST_ASSERT_EQ_U16(vm.cpu.pc, 0x000E, "PC sits AT the breakpoint address", &failures);
    /* The breakpoint is on `SUB R1, 1`. Firing BEFORE that instruction
     * executes (DESIGN.md section 13) means R1 must still be 3 -- if the
     * breakpoint fired after, R1 would already be 2. */
    TEST_ASSERT_EQ_U16(vm.cpu.r[1], 3,
                       "the breakpoint fired BEFORE its instruction executed (section 13)",
                       &failures);

    /* Continuing from a breakpoint must make progress rather than
     * immediately re-triggering the breakpoint it's already sitting on. */
    run_cmd(&dbg, "continue", buf, sizeof(buf));
    TEST_ASSERT(strstr(buf, "breakpoint at 0x000E") != NULL,
                "continue from a breakpoint reaches the next iteration's hit", &failures);
    TEST_ASSERT_EQ_U16(vm.cpu.r[1], 2, "one full loop iteration elapsed between hits", &failures);

    /* ---- delete ---- */
    run_cmd(&dbg, "delete 0x000E", buf, sizeof(buf));
    TEST_ASSERT(strstr(buf, "removed") != NULL, "delete reports success", &failures);
    run_cmd(&dbg, "delete 0x000E", buf, sizeof(buf));
    TEST_ASSERT(strstr(buf, "error") != NULL, "deleting a missing breakpoint is an error",
                &failures);

    run_cmd(&dbg, "continue", buf, sizeof(buf));
    TEST_ASSERT(vm.halted, "with no breakpoints left, continue runs to HALT", &failures);
    TEST_ASSERT_EQ_U16(vm.cpu.r[0], 6, "the finished program computed 3+2+1", &failures);

    /* ---- reset preserves breakpoints (section 13) ---- */
    run_cmd(&dbg, "break 0x000A", buf, sizeof(buf));
    run_cmd(&dbg, "reset", buf, sizeof(buf));
    TEST_ASSERT(strstr(buf, "1 breakpoint") != NULL, "reset keeps breakpoints", &failures);
    TEST_ASSERT(!vm.halted, "reset clears the halted flag", &failures);
    run_cmd(&dbg, "continue", buf, sizeof(buf));
    TEST_ASSERT(strstr(buf, "breakpoint at 0x000A") != NULL,
                "the preserved breakpoint still fires after reset", &failures);

    /* ---- stack ---- */
    run_cmd(&dbg, "reset", buf, sizeof(buf));
    run_cmd(&dbg, "stack", buf, sizeof(buf));
    TEST_ASSERT(strstr(buf, "empty") != NULL, "stack reports an empty stack on a fresh program",
                &failures);

    /* ---- memory ---- */
    run_cmd(&dbg, "memory 0x0000 4", buf, sizeof(buf));
    TEST_ASSERT(strstr(buf, "0x0000:") != NULL, "memory prints an address label", &failures);
    TEST_ASSERT(strstr(buf, "01") != NULL, "memory dumps the MOV opcode byte", &failures);

    /* ---- argument validation and unknown commands don't kill the session ---- */
    run_cmd(&dbg, "break notanumber", buf, sizeof(buf));
    TEST_ASSERT(strstr(buf, "error") != NULL, "break rejects a non-numeric address", &failures);
    run_cmd(&dbg, "memory", buf, sizeof(buf));
    TEST_ASSERT(strstr(buf, "error") != NULL, "memory requires an address", &failures);
    run_cmd(&dbg, "frobnicate", buf, sizeof(buf));
    TEST_ASSERT(strstr(buf, "unknown command") != NULL, "an unknown command is reported",
                &failures);

    {
        FILE *tmp = tmpfile();
        if (tmp != NULL) {
            bool keep_going = debugger_execute(&dbg, "step", tmp);
            TEST_ASSERT(keep_going, "a normal command keeps the session alive", &failures);
            keep_going = debugger_execute(&dbg, "quit", tmp);
            TEST_ASSERT(!keep_going, "quit ends the session", &failures);
            fclose(tmp);
        }
    }

    /* ---- stepping past HALT is safe (the section 9 no-op contract) ---- */
    run_cmd(&dbg, "reset", buf, sizeof(buf));
    run_cmd(&dbg, "step 1000", buf, sizeof(buf));
    TEST_ASSERT(vm.halted, "stepping well past the program's end halts cleanly", &failures);
    TEST_ASSERT(strstr(buf, "halted") != NULL, "step reports the halted state", &failures);

    assembly_result_free(&asm_result);
    return failures;
}

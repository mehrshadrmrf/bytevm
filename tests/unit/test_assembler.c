#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bytevm/assembler.h"
#include "bytevm/loader.h"
#include "bytevm/vm.h"
#include "test_framework.h"

/* Assembles source, loads it, and runs to completion (or the step cap).
 * Returns true if every stage succeeded. */
static bool assemble_and_run(const char *source, VM *vm, int *failures, const char *what)
{
    AssemblyResult asm_result;
    assemble(source, &asm_result);
    if (!asm_result.ok) {
        fprintf(stderr, "  (assembler error for %s: %s)\n", what, asm_result.error);
        TEST_ASSERT(false, what, failures);
        assembly_result_free(&asm_result);
        return false;
    }

    vm_init(vm);
    VmResult load = vm_load(vm, asm_result.image, asm_result.image_size);
    assembly_result_free(&asm_result);
    if (load != VM_OK) {
        TEST_ASSERT(false, what, failures);
        return false;
    }

    int steps = 0;
    while (!vm->halted && steps < 10000) {
        vm_step(vm);
        steps++;
    }
    return true;
}

static void expect_error(const char *source, const char *what, int *failures)
{
    AssemblyResult result;
    assemble(source, &result);
    TEST_ASSERT(!result.ok, what, failures);
    if (result.ok) {
        fprintf(stderr, "  (expected an error but assembly succeeded: %s)\n", what);
    }
    assembly_result_free(&result);
}

int test_assembler(void)
{
    int failures = 0;
    VM vm;

    /* ---- End-to-end: the exact program shape from DESIGN.md section
     * 11.1, exercising labels, CALL/RET, and a symbolic syscall name. ---- */
    {
        const char *src = "; sum 5 + 10 and print it\n"
                          "start:\n"
                          "    MOV R0, 5\n"
                          "    MOV R1, 10\n"
                          "    ADD R0, R1\n"
                          "    CALL print_result\n"
                          "    HALT\n"
                          "\n"
                          "print_result:\n"
                          "    SYS PRINT_INT      ; symbolic syscall name\n"
                          "    RET\n";

        if (assemble_and_run(src, &vm, &failures, "the section 11.1 example program assembles")) {
            TEST_ASSERT(vm.halted, "the assembled program runs to HALT", &failures);
            TEST_ASSERT(vm.last_error == VM_OK, "it halts without error", &failures);
            TEST_ASSERT_EQ_U16(vm.cpu.r[0], 15, "R0 == 5 + 10", &failures);
        }
    }

    /* ---- A backward-jump loop: proves labels resolve to real addresses
     * and that a label defined BEFORE its reference works. ---- */
    {
        const char *src = "    MOV R0, 0\n"
                          "    MOV R1, 5\n"
                          "loop:\n"
                          "    ADD R0, R1\n"
                          "    SUB R1, 1\n"
                          "    JNZ loop\n"
                          "    HALT\n";

        if (assemble_and_run(src, &vm, &failures, "a backward-jump loop assembles")) {
            TEST_ASSERT_EQ_U16(vm.cpu.r[0], 15, "the loop sums 5+4+3+2+1", &failures);
        }
    }

    /* ---- Forward reference: a label used before it's defined. This is
     * the whole reason the assembler needs two passes. ---- */
    {
        const char *src = "    JMP skip\n"
                          "    MOV R0, 99\n" /* skipped over */
                          "skip:\n"
                          "    MOV R0, 42\n"
                          "    HALT\n";

        if (assemble_and_run(src, &vm, &failures, "a forward label reference assembles")) {
            TEST_ASSERT_EQ_U16(vm.cpu.r[0], 42, "the forward JMP skipped the MOV R0, 99",
                               &failures);
        }
    }

    /* ---- Data directives + LOAD from a data label ---- */
    {
        const char *src = "    LOAD R0, [count]\n"
                          "    HALT\n"
                          ".data\n"
                          "count:\n"
                          "    .word 1234\n";

        if (assemble_and_run(src, &vm, &failures, ".word + LOAD from a label assembles")) {
            TEST_ASSERT_EQ_U16(vm.cpu.r[0], 1234, "LOAD read the .word value", &failures);
        }
    }

    /* ---- .string with escapes, verified byte-for-byte in memory ---- */
    {
        const char *src = "    HALT\n"
                          ".data\n"
                          "message:\n"
                          "    .string \"hi\\n\\x41\"\n";

        AssemblyResult r;
        assemble(src, &r);
        TEST_ASSERT(r.ok, ".string with escapes assembles", &failures);
        if (r.ok) {
            vm_init(&vm);
            vm_load(&vm, r.image, r.image_size);
            /* data section starts right after the code; find it via the
             * loader's own placement rather than assuming an address. */
            uint16_t data_addr = (uint16_t)(r.image[14] | (r.image[15] << 8));
            TEST_ASSERT_EQ_U16(mem_read8(&vm.memory, data_addr), 'h', ".string byte 0 is 'h'",
                               &failures);
            TEST_ASSERT_EQ_U16(mem_read8(&vm.memory, (uint16_t)(data_addr + 1)), 'i',
                               ".string byte 1 is 'i'", &failures);
            TEST_ASSERT_EQ_U16(mem_read8(&vm.memory, (uint16_t)(data_addr + 2)), '\n',
                               "\\n escape becomes a real newline byte", &failures);
            TEST_ASSERT_EQ_U16(mem_read8(&vm.memory, (uint16_t)(data_addr + 3)), 0x41,
                               "\\x41 escape becomes byte 0x41", &failures);
            TEST_ASSERT_EQ_U16(mem_read8(&vm.memory, (uint16_t)(data_addr + 4)), 0,
                               ".string is NUL-terminated", &failures);
        }
        assembly_result_free(&r);
    }

    /* ---- .byte, and REG_INDIRECT addressing via [Rn] ---- */
    {
        const char *src = "    MOV R1, 0x2000\n"
                          "    MOV R2, 7\n"
                          "    STORE [R1], R2\n"
                          "    LOAD R0, [R1]\n"
                          "    HALT\n";

        if (assemble_and_run(src, &vm, &failures, "[Rn] indirect addressing assembles")) {
            TEST_ASSERT_EQ_U16(vm.cpu.r[0], 7, "STORE/LOAD via [R1] round-trips", &failures);
            TEST_ASSERT_EQ_U16(mem_read16(&vm.memory, 0x2000), 7, "the value landed at 0x2000",
                               &failures);
        }
    }

    /* ---- Negative immediates and the -1 == 65535 equivalence
     * (DESIGN.md section 11.4) ---- */
    {
        const char *src_neg = "    MOV R0, -1\n"
                              "    HALT\n";
        const char *src_pos = "    MOV R0, 65535\n"
                              "    HALT\n";

        AssemblyResult a;
        AssemblyResult b;
        assemble(src_neg, &a);
        assemble(src_pos, &b);
        TEST_ASSERT(a.ok && b.ok, "both -1 and 65535 assemble", &failures);
        if (a.ok && b.ok) {
            TEST_ASSERT(a.image_size == b.image_size && memcmp(a.image, b.image, a.image_size) == 0,
                        "MOV R0, -1 and MOV R0, 65535 produce identical bytes (section 11.4)",
                        &failures);
        }
        assembly_result_free(&a);
        assembly_result_free(&b);
    }

    /* ---- .org moves the address counter forward ---- */
    {
        const char *src = ".org 0x0100\n"
                          "    MOV R0, 1\n"
                          "    HALT\n";

        AssemblyResult r;
        assemble(src, &r);
        TEST_ASSERT(r.ok, ".org assembles", &failures);
        if (r.ok) {
            uint16_t code_addr = (uint16_t)(r.image[10] | (r.image[11] << 8));
            uint16_t entry_point = (uint16_t)(r.image[6] | (r.image[7] << 8));
            TEST_ASSERT_EQ_U16(code_addr, 0x0100, ".org 0x0100 sets code_addr", &failures);
            TEST_ASSERT_EQ_U16(entry_point, 0x0100, "entry_point follows code_addr", &failures);
        }
        assembly_result_free(&r);
    }

    /* ---- The data section must not overlap the code section. Without
     * an explicit default, the data cursor would start at 0 alongside
     * code and silently overwrite it (this exact bug existed during
     * development: a .word test "passed" only because the data blob
     * happened to overwrite the code bytes it was reading back). ---- */
    {
        const char *src = "    LOAD R0, [count]\n"
                          "    HALT\n"
                          ".data\n"
                          "count:\n"
                          "    .word 1234\n";

        AssemblyResult r;
        assemble(src, &r);
        TEST_ASSERT(r.ok, "a code+data program assembles", &failures);
        if (r.ok) {
            uint16_t code_addr = (uint16_t)(r.image[10] | (r.image[11] << 8));
            uint16_t code_size = (uint16_t)(r.image[8] | (r.image[9] << 8));
            uint16_t data_addr = (uint16_t)(r.image[14] | (r.image[15] << 8));
            TEST_ASSERT(data_addr >= (uint16_t)(code_addr + code_size),
                        "the data section starts at or after the end of the code section",
                        &failures);
        }
        assembly_result_free(&r);
    }

    /* ---- Error cases (DESIGN.md section 11.3) ---- */
    expect_error("    FOO R0, R1\n    HALT\n", "an unknown mnemonic is rejected", &failures);
    expect_error("    JMP nowhere\n    HALT\n", "an undefined label is rejected", &failures);
    expect_error("    MOV R9, 1\n    HALT\n", "register R9 (out of range) is rejected", &failures);
    expect_error("    MOV R0, 65536\n    HALT\n", "an out-of-range immediate is rejected",
                 &failures);
    expect_error("    ADD 5, R1\n    HALT\n",
                 "an immediate destination for ADD is rejected (section 7.3)", &failures);
    expect_error("    MOV R0, 1\ndup:\n    HALT\ndup:\n    NOP\n",
                 "a duplicate label definition is rejected", &failures);
    expect_error(".org 0x0100\n    MOV R0, 1\n.org 0x0000\n    HALT\n",
                 "a backward .org is rejected (section 11.2)", &failures);
    expect_error("    HALT\n.data\n    .word 1\n.data\n    .word 2\n",
                 "a second .data directive is rejected", &failures);
    expect_error("    HALT\n.data\nm:\n    .string \"bad\\q\"\n",
                 "an unrecognized string escape is rejected", &failures);
    expect_error("    HALT\n.data\nm:\n    .string \"unterminated\n",
                 "an unterminated string literal is rejected", &failures);
    expect_error("    MOV R0\n    HALT\n", "too few operands is rejected", &failures);
    expect_error("    HALT R0\n    HALT\n", "too many operands is rejected", &failures);
    expect_error("    LOAD R0, 0x2000\n    HALT\n",
                 "LOAD without brackets is rejected (needs [addr] or [Rn])", &failures);

    return failures;
}

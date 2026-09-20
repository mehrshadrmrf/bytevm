#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bytevm/assembler.h"
#include "bytevm/disassembler.h"
#include "test_framework.h"

/* The core guarantee from DESIGN.md section 12/section 14:
 *   asm -> bvm -> asm -> bvm
 * and the two .bvm images must be byte-identical. */
static void expect_round_trip(const char *source, const char *what, int *failures)
{
    AssemblyResult first;
    assemble(source, &first);
    if (!first.ok) {
        fprintf(stderr, "  (assembly failed for %s: %s)\n", what, first.error);
        TEST_ASSERT(false, what, failures);
        assembly_result_free(&first);
        return;
    }

    DisassemblyResult disasm;
    disassemble(first.image, first.image_size, &disasm);
    if (!disasm.ok) {
        fprintf(stderr, "  (disassembly failed for %s: %s)\n", what, disasm.error);
        TEST_ASSERT(false, what, failures);
        assembly_result_free(&first);
        disassembly_result_free(&disasm);
        return;
    }

    AssemblyResult second;
    assemble(disasm.text, &second);
    if (!second.ok) {
        fprintf(stderr, "  (reassembly failed for %s: %s)\n  --- disassembly was ---\n%s\n", what,
                second.error, disasm.text);
        TEST_ASSERT(false, what, failures);
        assembly_result_free(&first);
        assembly_result_free(&second);
        disassembly_result_free(&disasm);
        return;
    }

    bool identical = (first.image_size == second.image_size) &&
                     memcmp(first.image, second.image, first.image_size) == 0;
    if (!identical) {
        fprintf(stderr,
                "  (round-trip differs for %s: %zu vs %zu bytes)\n  --- disassembly ---\n%s\n",
                what, first.image_size, second.image_size, disasm.text);
    }
    TEST_ASSERT(identical, what, failures);

    assembly_result_free(&first);
    assembly_result_free(&second);
    disassembly_result_free(&disasm);
}

int test_disassembler(void)
{
    int failures = 0;

    /* Every addressing mode and operand shape, so no formatting rule
     * goes unexercised. */
    expect_round_trip("    MOV R0, 5\n    HALT\n", "round-trip: REG + IMM16", &failures);
    expect_round_trip("    ADD R0, R1\n    HALT\n", "round-trip: REG + REG", &failures);
    expect_round_trip("    NOT R3\n    HALT\n", "round-trip: single REG operand", &failures);
    expect_round_trip("    NOP\n    RET\n    HALT\n", "round-trip: no-operand instructions",
                      &failures);
    expect_round_trip("    LOAD R2, [0x2000]\n    HALT\n", "round-trip: LOAD via MEM_DIRECT",
                      &failures);
    expect_round_trip("    LOAD R0, [R1]\n    HALT\n", "round-trip: LOAD via REG_INDIRECT",
                      &failures);
    expect_round_trip("    STORE [R1], R0\n    HALT\n", "round-trip: STORE via REG_INDIRECT",
                      &failures);
    expect_round_trip("    STORE [0x3000], R2\n    HALT\n", "round-trip: STORE via MEM_DIRECT",
                      &failures);
    expect_round_trip("    SYS PRINT_INT\n    HALT\n", "round-trip: SYS with a symbolic name",
                      &failures);
    expect_round_trip("    PUSH R4\n    POP R5\n    HALT\n", "round-trip: PUSH/POP", &failures);
    expect_round_trip("    CMP R0, 100\n    HALT\n", "round-trip: CMP with an immediate",
                      &failures);

    /* Jump/CALL targets are MEM_DIRECT but must be emitted WITHOUT
     * brackets -- the bracket form is only for LOAD/STORE, where it
     * disambiguates against REG_INDIRECT. Getting this backwards
     * produces text that no longer reassembles. */
    expect_round_trip("start:\n    JMP start\n    HALT\n", "round-trip: JMP target has no brackets",
                      &failures);
    expect_round_trip("    CALL sub\n    HALT\nsub:\n    RET\n",
                      "round-trip: CALL target has no brackets", &failures);
    expect_round_trip("    JZ a\n    JNZ a\n    JG a\n    JGE a\n    JL a\n    JLE a\n"
                      "    JB a\n    JAE a\n    JA a\n    JBE a\na:\n    HALT\n",
                      "round-trip: all eight conditional jumps", &failures);

    /* Immediates at the edges of section 11.4's accepted range. */
    expect_round_trip("    MOV R0, 65535\n    HALT\n", "round-trip: maximum unsigned immediate",
                      &failures);
    expect_round_trip("    MOV R0, -32768\n    HALT\n",
                      "round-trip: minimum signed immediate (re-emitted unsigned)", &failures);
    expect_round_trip("    MOV R0, 0\n    HALT\n", "round-trip: zero immediate", &failures);

    /* Data sections, both adjacent to the code and displaced by .org. */
    expect_round_trip("    HALT\n.data\nm:\n    .string \"hi\\n\"\n",
                      "round-trip: a .string data section", &failures);
    expect_round_trip("    HALT\n.data\nc:\n    .word 1234\n", "round-trip: a .word data section",
                      &failures);
    expect_round_trip("    LOAD R0, [n]\n    HALT\n.data\nn:\n    .word 7\n    .word 8\n",
                      "round-trip: code referencing a data label", &failures);
    expect_round_trip(".org 0x0100\n    MOV R0, 1\n    HALT\n",
                      "round-trip: code displaced by .org", &failures);

    /* Explicit .byte in the code section: the disassembler's own
     * fallback output must be something the assembler accepts, which is
     * the whole reason .byte exists (DESIGN.md section 0, item 14). */
    expect_round_trip("    HALT\n    .byte 0xAB\n    .byte 0xCD\n",
                      "round-trip: raw .byte data embedded in code", &failures);

    /* A full, realistic program. */
    expect_round_trip("    MOV R0, 0\n"
                      "    MOV R1, numbers\n"
                      "    MOV R2, 3\n"
                      "loop:\n"
                      "    LOAD R3, [R1]\n"
                      "    ADD R0, R3\n"
                      "    ADD R1, 2\n"
                      "    SUB R2, 1\n"
                      "    JNZ loop\n"
                      "    SYS PRINT_INT\n"
                      "    HALT\n"
                      ".data\n"
                      "numbers:\n"
                      "    .word 10\n"
                      "    .word 20\n"
                      "    .word 30\n",
                      "round-trip: a complete array-summing program", &failures);

    /* Undecodable bytes fall back to .byte rather than failing. 0x20 is
     * not a valid opcode (it sits between SYS=0x1F and HALT=0xFF). */
    {
        const char *src = "    HALT\n    .byte 0x20\n    .byte 0x00\n";
        AssemblyResult asm_result;
        assemble(src, &asm_result);
        TEST_ASSERT(asm_result.ok, "a program with an invalid opcode byte assembles", &failures);
        if (asm_result.ok) {
            DisassemblyResult d;
            disassemble(asm_result.image, asm_result.image_size, &d);
            TEST_ASSERT(d.ok, "disassembly succeeds despite an undecodable byte", &failures);
            if (d.ok) {
                TEST_ASSERT(strstr(d.text, ".byte 0x20") != NULL,
                            "the undecodable byte is emitted as a .byte directive", &failures);
            }
            disassembly_result_free(&d);
        }
        assembly_result_free(&asm_result);
    }

    /* A truncated instruction at the very end of the code blob must not
     * be decoded as a real instruction -- its operand bytes aren't part
     * of the program, so consuming them would invent bytes that don't
     * exist in the image. */
    {
        /* MOV's opcode + mode byte with no operand bytes following. */
        const char *src = "    .byte 0x01\n    .byte 0x12\n";
        AssemblyResult asm_result;
        assemble(src, &asm_result);
        if (asm_result.ok) {
            DisassemblyResult d;
            disassemble(asm_result.image, asm_result.image_size, &d);
            TEST_ASSERT(d.ok, "a truncated trailing instruction disassembles without failing",
                        &failures);
            if (d.ok) {
                TEST_ASSERT(strstr(d.text, ".byte 0x01") != NULL,
                            "a truncated instruction falls back to .byte", &failures);
            }
            disassembly_result_free(&d);
        }
        assembly_result_free(&asm_result);
    }

    /* Rejections. */
    {
        uint8_t junk[16] = {'X', 'X', 'X', 'X', 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        DisassemblyResult d;
        disassemble(junk, sizeof(junk), &d);
        TEST_ASSERT(!d.ok, "a non-BVM1 image is rejected", &failures);
        disassembly_result_free(&d);
    }

    return failures;
}

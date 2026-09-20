#include <stddef.h>
#include <stdint.h>

#include "bytevm/instruction.h"
#include "bytevm/memory.h"
#include "test_framework.h"

static void put(Memory *mem, uint16_t addr, const uint8_t *bytes, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        mem_write8(mem, (uint16_t)(addr + i), bytes[i]);
    }
}

int test_instruction(void)
{
    int failures = 0;
    Memory mem;
    mem_init(&mem);

    /* MOV R0, 10 -- 01 12 00 0A 00 (DESIGN.md section 7.2's own example) */
    {
        const uint8_t bytes[] = {0x01, 0x12, 0x00, 0x0A, 0x00};
        put(&mem, 0x0000, bytes, sizeof(bytes));

        uint16_t pc = 0x0000;
        Instruction instr;
        VmResult r = instruction_decode(&mem, &pc, &instr);

        TEST_ASSERT(r == VM_OK, "MOV R0, 10 decodes successfully", &failures);
        TEST_ASSERT(instr.opcode == OP_MOV, "opcode is OP_MOV", &failures);
        TEST_ASSERT(instr.op1.mode == MODE_REG, "op1 mode is REG", &failures);
        TEST_ASSERT_EQ_U16(instr.op1.value, 0, "op1 is R0", &failures);
        TEST_ASSERT(instr.op2.mode == MODE_IMM16, "op2 mode is IMM16", &failures);
        TEST_ASSERT_EQ_U16(instr.op2.value, 10, "op2 is 10", &failures);
        TEST_ASSERT_EQ_U16(pc, 5, "pc advances past all 5 bytes", &failures);
    }

    /* LOAD R0, [R1] -- 0D 14 00 01 (REG_INDIRECT, DESIGN.md section 7.2) */
    {
        const uint8_t bytes[] = {0x0D, 0x14, 0x00, 0x01};
        put(&mem, 0x0100, bytes, sizeof(bytes));

        uint16_t pc = 0x0100;
        Instruction instr;
        VmResult r = instruction_decode(&mem, &pc, &instr);

        TEST_ASSERT(r == VM_OK, "LOAD R0, [R1] decodes successfully", &failures);
        TEST_ASSERT(instr.op2.mode == MODE_REG_INDIRECT, "op2 mode is REG_INDIRECT", &failures);
        TEST_ASSERT_EQ_U16(instr.op2.value, 1, "op2 is R1", &failures);
        TEST_ASSERT_EQ_U16(pc, 0x0104, "pc advances past all 4 bytes", &failures);
    }

    /* SYS PRINT_INT -- 1F 50 01 (IMM8, DESIGN.md section 7.2) */
    {
        const uint8_t bytes[] = {0x1F, 0x50, 0x01};
        put(&mem, 0x0200, bytes, sizeof(bytes));

        uint16_t pc = 0x0200;
        Instruction instr;
        VmResult r = instruction_decode(&mem, &pc, &instr);

        TEST_ASSERT(r == VM_OK, "SYS PRINT_INT decodes successfully", &failures);
        TEST_ASSERT(instr.opcode == OP_SYS, "opcode is OP_SYS", &failures);
        TEST_ASSERT(instr.op1.mode == MODE_IMM8, "op1 mode is IMM8", &failures);
        TEST_ASSERT_EQ_U16(instr.op1.value, 1, "op1 is syscall code 1", &failures);
        TEST_ASSERT_EQ_U16(pc, 0x0203, "pc advances past all 3 bytes", &failures);
    }

    /* Unknown opcode byte -> VM_ERR_INVALID_OPCODE. 0x20 is not in the
     * opcode table (between SYS=0x1F and HALT=0xFF). */
    {
        const uint8_t bytes[] = {0x20, 0x00};
        put(&mem, 0x0300, bytes, sizeof(bytes));

        uint16_t pc = 0x0300;
        Instruction instr;
        VmResult r = instruction_decode(&mem, &pc, &instr);

        TEST_ASSERT(r == VM_ERR_INVALID_OPCODE, "unknown opcode byte is rejected", &failures);
        TEST_ASSERT_EQ_U16(pc, 0x0300, "pc is untouched on a decode failure", &failures);
    }

    /* Unknown mode nibble (0x6-0xF) -> VM_ERR_INVALID_MODE. NOP with
     * mode byte 0x60 (op1 nibble = 0x6, undefined). */
    {
        const uint8_t bytes[] = {0x00, 0x60};
        put(&mem, 0x0400, bytes, sizeof(bytes));

        uint16_t pc = 0x0400;
        Instruction instr;
        VmResult r = instruction_decode(&mem, &pc, &instr);

        TEST_ASSERT(r == VM_ERR_INVALID_MODE, "unknown mode nibble is rejected", &failures);
        TEST_ASSERT_EQ_U16(pc, 0x0400, "pc is untouched on a decode failure", &failures);
    }

    /* Mode legal in general but not for this opcode/operand ->
     * VM_ERR_INVALID_MODE. ADD's op1 must be REG (section 7.3); this
     * encodes ADD with op1 = IMM16 instead. */
    {
        const uint8_t bytes[] = {0x02, 0x21, 0x05, 0x00, 0x00};
        put(&mem, 0x0500, bytes, sizeof(bytes));

        uint16_t pc = 0x0500;
        Instruction instr;
        VmResult r = instruction_decode(&mem, &pc, &instr);

        TEST_ASSERT(r == VM_ERR_INVALID_MODE,
                    "ADD with an immediate destination is rejected (section 7.3)", &failures);
    }

    /* Register index out of range (8-255) -> VM_ERR_INVALID_MODE. MOV
     * with a REG operand whose index byte is 9. */
    {
        const uint8_t bytes[] = {0x01, 0x12, 0x09, 0x05, 0x00};
        put(&mem, 0x0600, bytes, sizeof(bytes));

        uint16_t pc = 0x0600;
        Instruction instr;
        VmResult r = instruction_decode(&mem, &pc, &instr);

        TEST_ASSERT(r == VM_ERR_INVALID_MODE, "register index 9 is rejected", &failures);
    }

    /* End-of-memory wraparound (DESIGN.md section 2.1/section 3): a NOP
     * whose opcode byte sits at 0xFFFF and whose mode byte wraps to
     * 0x0000 must still decode successfully. */
    {
        mem_init(&mem);                 /* fresh memory so 0x0000 is a clean NOP+NONE mode byte */
        mem_write8(&mem, 0xFFFF, 0x00); /* NOP opcode */
        mem_write8(&mem, 0x0000, 0x00); /* mode byte: NONE, NONE */

        uint16_t pc = 0xFFFF;
        Instruction instr;
        VmResult r = instruction_decode(&mem, &pc, &instr);

        TEST_ASSERT(r == VM_OK, "an instruction straddling 0xFFFF/0x0000 decodes successfully",
                    &failures);
        TEST_ASSERT(instr.opcode == OP_NOP, "opcode is OP_NOP", &failures);
        TEST_ASSERT_EQ_U16(pc, 0x0001, "pc wraps to 0x0001 after a 2-byte instruction at 0xFFFF",
                           &failures);
    }

    return failures;
}

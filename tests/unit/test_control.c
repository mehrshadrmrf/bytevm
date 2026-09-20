#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bytevm/vm.h"
#include "test_framework.h"

/* Writes bytes at addr, sets PC = addr, and executes exactly one step --
 * lets each test place its instruction wherever it wants and check the
 * resulting PC independent of that placement. */
static VmResult exec_at(VM *vm, uint16_t addr, const uint8_t *bytes, size_t len)
{
    size_t i;
    for (i = 0; i < len; i++) {
        mem_write8(&vm->memory, (uint16_t)(addr + i), bytes[i]);
    }
    vm->cpu.pc = addr;
    return vm_step(vm);
}

static void set_flags(CPU *cpu, bool z, bool c, bool s, bool o)
{
    cpu_set_flag(cpu, CPU_FLAG_Z, z);
    cpu_set_flag(cpu, CPU_FLAG_C, c);
    cpu_set_flag(cpu, CPU_FLAG_S, s);
    cpu_set_flag(cpu, CPU_FLAG_O, o);
}

/* Every conditional jump here is encoded the same way: opcode, mode byte
 * 0x30 (op1 = MEM_DIRECT, op2 = NONE), target address 0x2000 LE. Falling
 * through (not taken) lands at addr + 4 (the instruction's own length). */
#define JUMP_TARGET 0x2000u

static VmResult run_jump(VM *vm, uint8_t opcode, bool z, bool c, bool s, bool o)
{
    vm_init(vm);
    set_flags(&vm->cpu, z, c, s, o);
    const uint8_t bytes[] = {opcode, 0x30, 0x00, 0x20}; /* target 0x2000 */
    return exec_at(vm, 0x0000, bytes, sizeof(bytes));
}

int test_control(void)
{
    int failures = 0;
    VM vm;

    /* ---- JMP: unconditional (DESIGN.md section 8.5) ---- */
    {
        vm_init(&vm);
        const uint8_t bytes[] = {0x12, 0x30, 0x00, 0x20}; /* JMP 0x2000 */
        VmResult r = exec_at(&vm, 0x0000, bytes, sizeof(bytes));
        TEST_ASSERT(r == VM_OK, "JMP succeeds", &failures);
        TEST_ASSERT_EQ_U16(vm.cpu.pc, JUMP_TARGET, "JMP sets PC to the target unconditionally",
                           &failures);
    }

    /* ---- JZ / JNZ ---- */
    {
        run_jump(&vm, 0x13, true, false, false, false); /* JZ, Z=1 */
        TEST_ASSERT_EQ_U16(vm.cpu.pc, JUMP_TARGET, "JZ taken when Z=1", &failures);
        run_jump(&vm, 0x13, false, false, false, false); /* JZ, Z=0 */
        TEST_ASSERT_EQ_U16(vm.cpu.pc, 0x0004, "JZ not taken when Z=0", &failures);

        run_jump(&vm, 0x14, false, false, false, false); /* JNZ, Z=0 */
        TEST_ASSERT_EQ_U16(vm.cpu.pc, JUMP_TARGET, "JNZ taken when Z=0", &failures);
        run_jump(&vm, 0x14, true, false, false, false); /* JNZ, Z=1 */
        TEST_ASSERT_EQ_U16(vm.cpu.pc, 0x0004, "JNZ not taken when Z=1", &failures);
    }

    /* ---- Signed jumps: JG/JGE/JL/JLE (DESIGN.md section 5's table) ----
     * Three flag setups exercise every branch of every one of the four
     * conditions in a single pass each. */
    {
        /* S != O (e.g. after CMP found a < b, signed), Z = 0: only the
         * "less than" family should fire. */
        run_jump(&vm, 0x15, false, false, true, false); /* JG */
        TEST_ASSERT_EQ_U16(vm.cpu.pc, 0x0004, "JG not taken when S!=O", &failures);
        run_jump(&vm, 0x16, false, false, true, false); /* JGE */
        TEST_ASSERT_EQ_U16(vm.cpu.pc, 0x0004, "JGE not taken when S!=O", &failures);
        run_jump(&vm, 0x17, false, false, true, false); /* JL */
        TEST_ASSERT_EQ_U16(vm.cpu.pc, JUMP_TARGET, "JL taken when S!=O", &failures);
        run_jump(&vm, 0x18, false, false, true, false); /* JLE */
        TEST_ASSERT_EQ_U16(vm.cpu.pc, JUMP_TARGET, "JLE taken when S!=O", &failures);
    }
    {
        /* S == O, Z = 0 (strictly greater, signed): only the "greater
         * than" family should fire. */
        run_jump(&vm, 0x15, false, false, false, false); /* JG */
        TEST_ASSERT_EQ_U16(vm.cpu.pc, JUMP_TARGET, "JG taken when S==O and Z=0", &failures);
        run_jump(&vm, 0x16, false, false, false, false); /* JGE */
        TEST_ASSERT_EQ_U16(vm.cpu.pc, JUMP_TARGET, "JGE taken when S==O", &failures);
        run_jump(&vm, 0x17, false, false, false, false); /* JL */
        TEST_ASSERT_EQ_U16(vm.cpu.pc, 0x0004, "JL not taken when S==O", &failures);
        run_jump(&vm, 0x18, false, false, false, false); /* JLE */
        TEST_ASSERT_EQ_U16(vm.cpu.pc, 0x0004, "JLE not taken when S==O and Z=0", &failures);
    }
    {
        /* S == O, Z = 1 (equal, signed): JG must NOT fire despite S==O
         * (Z blocks it); JLE must fire because of Z alone. */
        run_jump(&vm, 0x15, true, false, false, false); /* JG */
        TEST_ASSERT_EQ_U16(vm.cpu.pc, 0x0004, "JG not taken when Z=1, even with S==O", &failures);
        run_jump(&vm, 0x16, true, false, false, false); /* JGE */
        TEST_ASSERT_EQ_U16(vm.cpu.pc, JUMP_TARGET, "JGE taken when S==O regardless of Z",
                           &failures);
        run_jump(&vm, 0x17, true, false, false, false); /* JL */
        TEST_ASSERT_EQ_U16(vm.cpu.pc, 0x0004, "JL not taken when S==O", &failures);
        run_jump(&vm, 0x18, true, false, false, false); /* JLE */
        TEST_ASSERT_EQ_U16(vm.cpu.pc, JUMP_TARGET, "JLE taken when Z=1 alone", &failures);
    }

    /* ---- Unsigned jumps: JB/JAE/JA/JBE ---- */
    {
        /* C = 1, Z = 0 ("below"): only the "below" family fires. */
        run_jump(&vm, 0x19, false, true, false, false); /* JB */
        TEST_ASSERT_EQ_U16(vm.cpu.pc, JUMP_TARGET, "JB taken when C=1", &failures);
        run_jump(&vm, 0x1A, false, true, false, false); /* JAE */
        TEST_ASSERT_EQ_U16(vm.cpu.pc, 0x0004, "JAE not taken when C=1", &failures);
        run_jump(&vm, 0x1B, false, true, false, false); /* JA */
        TEST_ASSERT_EQ_U16(vm.cpu.pc, 0x0004, "JA not taken when C=1", &failures);
        run_jump(&vm, 0x1C, false, true, false, false); /* JBE */
        TEST_ASSERT_EQ_U16(vm.cpu.pc, JUMP_TARGET, "JBE taken when C=1", &failures);
    }
    {
        /* C = 0, Z = 0 ("above"): only the "above" family fires. */
        run_jump(&vm, 0x19, false, false, false, false); /* JB */
        TEST_ASSERT_EQ_U16(vm.cpu.pc, 0x0004, "JB not taken when C=0", &failures);
        run_jump(&vm, 0x1A, false, false, false, false); /* JAE */
        TEST_ASSERT_EQ_U16(vm.cpu.pc, JUMP_TARGET, "JAE taken when C=0", &failures);
        run_jump(&vm, 0x1B, false, false, false, false); /* JA */
        TEST_ASSERT_EQ_U16(vm.cpu.pc, JUMP_TARGET, "JA taken when C=0 and Z=0", &failures);
        run_jump(&vm, 0x1C, false, false, false, false); /* JBE */
        TEST_ASSERT_EQ_U16(vm.cpu.pc, 0x0004, "JBE not taken when C=0 and Z=0", &failures);
    }
    {
        /* C = 0, Z = 1 ("equal"): JA must NOT fire despite C=0 (Z blocks
         * it); JBE must fire because of Z alone. */
        run_jump(&vm, 0x1A, true, false, false, false); /* JAE */
        TEST_ASSERT_EQ_U16(vm.cpu.pc, JUMP_TARGET, "JAE taken when C=0 regardless of Z", &failures);
        run_jump(&vm, 0x1B, true, false, false, false); /* JA */
        TEST_ASSERT_EQ_U16(vm.cpu.pc, 0x0004, "JA not taken when Z=1, even with C=0", &failures);
        run_jump(&vm, 0x1C, true, false, false, false); /* JBE */
        TEST_ASSERT_EQ_U16(vm.cpu.pc, JUMP_TARGET, "JBE taken when Z=1 alone", &failures);
    }

    /* ---- PUSH / POP (DESIGN.md section 6, section 8.1) ---- */
    {
        /* Round trip: PUSH a register, POP it into another, recovering
         * both the value and SP. */
        vm_init(&vm);
        vm.cpu.r[0] = 0x1234;
        uint16_t sp_before = vm.cpu.sp;
        const uint8_t push[] = {0x0F, 0x10, 0x00}; /* PUSH R0 */
        VmResult r1 = exec_at(&vm, 0x0000, push, sizeof(push));

        TEST_ASSERT(r1 == VM_OK, "PUSH R0 succeeds", &failures);
        TEST_ASSERT_EQ_U16(vm.cpu.sp, (uint16_t)(sp_before - 2), "PUSH decrements SP by 2",
                           &failures);
        TEST_ASSERT_EQ_U16(mem_read16(&vm.memory, sp_before), 0x1234,
                           "PUSH writes the value at the pre-push SP", &failures);

        const uint8_t pop[] = {0x10, 0x10, 0x01}; /* POP R1 */
        VmResult r2 = exec_at(&vm, 0x0003, pop, sizeof(pop));

        TEST_ASSERT(r2 == VM_OK, "POP R1 succeeds", &failures);
        TEST_ASSERT_EQ_U16(vm.cpu.r[1], 0x1234, "POP recovers the pushed value", &failures);
        TEST_ASSERT_EQ_U16(vm.cpu.sp, sp_before, "POP restores SP to its pre-push value",
                           &failures);
    }
    {
        /* PUSH accepts an IMM16 source too (section 7.3). */
        vm_init(&vm);
        const uint8_t push[] = {0x0F, 0x20, 0xCD, 0xAB}; /* PUSH 0xABCD */
        exec_at(&vm, 0x0000, push, sizeof(push));
        TEST_ASSERT_EQ_U16(mem_read16(&vm.memory, CPU_INITIAL_SP), 0xABCD,
                           "PUSH 0xABCD (immediate) writes the right value", &failures);
    }
    {
        /* Stack overflow: stack_limit set close enough to SP that the
         * next push's post-decrement address would dip into it. */
        vm_init(&vm);
        vm.stack_limit = (uint16_t)(CPU_INITIAL_SP - 1); /* SP - 2 < this */
        uint16_t sp_before = vm.cpu.sp;
        const uint8_t push[] = {0x0F, 0x10, 0x00}; /* PUSH R0 */
        VmResult r = exec_at(&vm, 0x0000, push, sizeof(push));

        TEST_ASSERT(r == VM_ERR_STACK_OVERFLOW, "PUSH reports overflow against stack_limit",
                    &failures);
        TEST_ASSERT(vm.halted, "the VM halts on stack overflow", &failures);
        TEST_ASSERT_EQ_U16(vm.cpu.sp, sp_before, "SP is untouched when PUSH overflows", &failures);
    }
    {
        /* SP < 2 guard: the wraparound case that stack_limit alone can't
         * catch (section 6) -- SP itself is far below any plausible
         * stack_limit, so only the explicit SP < 2 check catches this. */
        vm_init(&vm);
        vm.cpu.sp = 1;
        vm.stack_limit = 0;
        const uint8_t push[] = {0x0F, 0x10, 0x00};
        VmResult r = exec_at(&vm, 0x0000, push, sizeof(push));

        TEST_ASSERT(r == VM_ERR_STACK_OVERFLOW, "PUSH at SP=1 reports overflow (would wrap)",
                    &failures);
        TEST_ASSERT_EQ_U16(vm.cpu.sp, 1, "SP is untouched when PUSH would wrap", &failures);
    }
    {
        /* Stack underflow: a fresh VM's SP is the empty-stack sentinel. */
        vm_init(&vm);
        const uint8_t pop[] = {0x10, 0x10, 0x00}; /* POP R0 */
        VmResult r = exec_at(&vm, 0x0000, pop, sizeof(pop));

        TEST_ASSERT(r == VM_ERR_STACK_UNDERFLOW, "POP on an empty stack reports underflow",
                    &failures);
        TEST_ASSERT(vm.halted, "the VM halts on stack underflow", &failures);
        TEST_ASSERT_EQ_U16(vm.cpu.sp, CPU_INITIAL_SP, "SP is untouched when POP underflows",
                           &failures);
    }

    return failures;
}

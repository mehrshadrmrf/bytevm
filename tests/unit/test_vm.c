#include <stddef.h>
#include <stdint.h>

#include "bytevm/vm.h"
#include "test_framework.h"

static void put(VM *vm, uint16_t addr, const uint8_t *bytes, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        mem_write8(&vm->memory, (uint16_t)(addr + i), bytes[i]);
    }
}

int test_vm(void)
{
    int failures = 0;
    VM vm;

    /* MOV R0, 5 ; MOV R1, 10 ; ADD R0, R1 ; HALT
     * The exact byte-for-byte program from DESIGN.md's own encoding
     * rules (section 7) -- proves the full fetch/decode/execute loop,
     * not just decode in isolation. */
    {
        const uint8_t program[] = {
            0x01, 0x12, 0x00, 0x05, 0x00, /* MOV R0, 5  */
            0x01, 0x12, 0x01, 0x0A, 0x00, /* MOV R1, 10 */
            0x02, 0x11, 0x00, 0x01,       /* ADD R0, R1 */
            0xFF, 0x00,                   /* HALT       */
        };
        vm_init(&vm);
        put(&vm, 0x0000, program, sizeof(program));

        VmResult r = VM_OK;
        int steps = 0;
        while (!vm.halted && steps < 10) {
            r = vm_step(&vm);
            steps++;
        }

        TEST_ASSERT(vm.halted, "the program runs to HALT", &failures);
        TEST_ASSERT(r == VM_OK, "HALT is not an error", &failures);
        TEST_ASSERT(vm.last_error == VM_OK, "last_error is VM_OK after a normal HALT", &failures);
        TEST_ASSERT_EQ_U16(steps, 4, "exactly 4 instructions executed", &failures);
        TEST_ASSERT_EQ_U16(vm.cpu.r[0], 15, "R0 == 5 + 10", &failures);
        TEST_ASSERT_EQ_U16(vm.cpu.pc, sizeof(program), "PC ends just past HALT", &failures);

        /* Already-halted no-op contract (DESIGN.md section 9). */
        uint16_t pc_before = vm.cpu.pc;
        uint16_t r0_before = vm.cpu.r[0];
        r = vm_step(&vm);
        TEST_ASSERT(r == VM_OK, "stepping a halted VM returns its last_error (VM_OK here)",
                    &failures);
        TEST_ASSERT_EQ_U16(vm.cpu.pc, pc_before, "stepping a halted VM does not move PC",
                           &failures);
        TEST_ASSERT_EQ_U16(vm.cpu.r[0], r0_before, "stepping a halted VM does not touch registers",
                           &failures);
    }

    /* ADD flag boundary cases named explicitly in DESIGN.md section 14. */
    {
        const uint8_t program[] = {
            0x01, 0x12, 0x00, 0xFF, 0xFF, /* MOV R0, 0xFFFF */
            0x01, 0x12, 0x01, 0x01, 0x00, /* MOV R1, 1      */
            0x02, 0x11, 0x00, 0x01,       /* ADD R0, R1     */
            0xFF, 0x00,                   /* HALT           */
        };
        vm_init(&vm);
        put(&vm, 0x0000, program, sizeof(program));
        while (!vm.halted) {
            vm_step(&vm);
        }

        TEST_ASSERT(vm.last_error == VM_OK, "the ADD program runs without error", &failures);
        TEST_ASSERT_EQ_U16(vm.cpu.r[0], 0, "0xFFFF + 1 wraps to 0", &failures);
        TEST_ASSERT(cpu_get_flag(&vm.cpu, CPU_FLAG_Z), "0xFFFF + 1: Z set", &failures);
        TEST_ASSERT(cpu_get_flag(&vm.cpu, CPU_FLAG_C), "0xFFFF + 1: C set (unsigned carry)",
                    &failures);
        TEST_ASSERT(!cpu_get_flag(&vm.cpu, CPU_FLAG_O),
                    "0xFFFF + 1: O clear (operands have different signs)", &failures);
    }
    {
        const uint8_t program[] = {
            0x01, 0x12, 0x00, 0x00, 0x80, /* MOV R0, 0x8000 */
            0x01, 0x12, 0x01, 0x00, 0x80, /* MOV R1, 0x8000 */
            0x02, 0x11, 0x00, 0x01,       /* ADD R0, R1     */
            0xFF, 0x00,                   /* HALT           */
        };
        vm_init(&vm);
        put(&vm, 0x0000, program, sizeof(program));
        while (!vm.halted) {
            vm_step(&vm);
        }

        TEST_ASSERT(vm.last_error == VM_OK, "the ADD program runs without error", &failures);
        TEST_ASSERT_EQ_U16(vm.cpu.r[0], 0, "0x8000 + 0x8000 wraps to 0", &failures);
        TEST_ASSERT(cpu_get_flag(&vm.cpu, CPU_FLAG_C), "0x8000 + 0x8000: C set", &failures);
        TEST_ASSERT(cpu_get_flag(&vm.cpu, CPU_FLAG_O),
                    "0x8000 + 0x8000: O set (both negative, result positive)", &failures);
    }

    /* SUB flag boundary cases named explicitly in DESIGN.md section 14. */
    {
        const uint8_t program[] = {
            0x01, 0x12, 0x00, 0x00, 0x00, /* MOV R0, 0      */
            0x01, 0x12, 0x01, 0x01, 0x00, /* MOV R1, 1      */
            0x03, 0x11, 0x00, 0x01,       /* SUB R0, R1     */
            0xFF, 0x00,                   /* HALT           */
        };
        vm_init(&vm);
        put(&vm, 0x0000, program, sizeof(program));
        while (!vm.halted) {
            vm_step(&vm);
        }

        TEST_ASSERT(vm.last_error == VM_OK, "the SUB program runs without error", &failures);
        TEST_ASSERT_EQ_U16(vm.cpu.r[0], 0xFFFF, "0 - 1 wraps to 0xFFFF", &failures);
        TEST_ASSERT(cpu_get_flag(&vm.cpu, CPU_FLAG_C), "0 - 1: C set (unsigned borrow)", &failures);
        TEST_ASSERT(!cpu_get_flag(&vm.cpu, CPU_FLAG_O), "0 - 1: O clear (no signed overflow)",
                    &failures);
    }
    {
        const uint8_t program[] = {
            0x01, 0x12, 0x00, 0x00, 0x80, /* MOV R0, 0x8000 */
            0x01, 0x12, 0x01, 0x01, 0x00, /* MOV R1, 1      */
            0x03, 0x11, 0x00, 0x01,       /* SUB R0, R1     */
            0xFF, 0x00,                   /* HALT           */
        };
        vm_init(&vm);
        put(&vm, 0x0000, program, sizeof(program));
        while (!vm.halted) {
            vm_step(&vm);
        }

        TEST_ASSERT(vm.last_error == VM_OK, "the SUB program runs without error", &failures);
        TEST_ASSERT_EQ_U16(vm.cpu.r[0], 0x7FFF, "0x8000 - 1 == 0x7FFF", &failures);
        TEST_ASSERT(!cpu_get_flag(&vm.cpu, CPU_FLAG_C), "0x8000 - 1: C clear (no unsigned borrow)",
                    &failures);
        TEST_ASSERT(cpu_get_flag(&vm.cpu, CPU_FLAG_O),
                    "0x8000 - 1: O set (signed overflow, INT16_MIN - 1)", &failures);
    }

    /* LOAD/STORE, both addressing modes. */
    {
        vm_init(&vm);
        mem_write16(&vm.memory, 0x3000, 0xBEEF);

        const uint8_t program[] = {
            0x0D, 0x13, 0x00, 0x00, 0x30, /* LOAD R0, [0x3000]  (MEM_DIRECT) */
            0x01, 0x12, 0x01, 0x00, 0x30, /* MOV R1, 0x3000               */
            0x0D, 0x14, 0x02, 0x01,       /* LOAD R2, [R1]     (REG_INDIRECT) */
            0x01, 0x12, 0x03, 0xAD, 0xDE, /* MOV R3, 0xDEAD               */
            0x0E, 0x41, 0x01, 0x03,       /* STORE [R1], R3    (REG_INDIRECT) */
            0xFF, 0x00,                   /* HALT                          */
        };
        put(&vm, 0x0000, program, sizeof(program));
        while (!vm.halted) {
            vm_step(&vm);
        }

        TEST_ASSERT(vm.last_error == VM_OK, "the LOAD/STORE program runs without error", &failures);
        TEST_ASSERT_EQ_U16(vm.cpu.r[0], 0xBEEF, "LOAD via MEM_DIRECT reads the right value",
                           &failures);
        TEST_ASSERT_EQ_U16(vm.cpu.r[2], 0xBEEF, "LOAD via REG_INDIRECT reads the same value",
                           &failures);
        TEST_ASSERT_EQ_U16(mem_read16(&vm.memory, 0x3000), 0xDEAD,
                           "STORE via REG_INDIRECT writes to the right address", &failures);
    }

    /* Decode failure mid-run: halts with the right error and error_pc,
     * and is a no-op afterward (DESIGN.md section 9). */
    {
        vm_init(&vm);
        const uint8_t program[] = {
            0x00, 0x00, /* NOP */
            0x20, 0x00, /* unknown opcode byte, at address 2 */
        };
        put(&vm, 0x0000, program, sizeof(program));

        VmResult r1 = vm_step(&vm); /* NOP: fine */
        TEST_ASSERT(r1 == VM_OK, "the NOP before the bad opcode succeeds", &failures);
        TEST_ASSERT(!vm.halted, "the VM is not halted yet", &failures);

        VmResult r2 = vm_step(&vm); /* hits the unknown opcode */
        TEST_ASSERT(r2 == VM_ERR_INVALID_OPCODE, "the unknown opcode is reported", &failures);
        TEST_ASSERT(vm.halted, "the VM halts on a decode failure", &failures);
        TEST_ASSERT(vm.last_error == VM_ERR_INVALID_OPCODE, "last_error matches", &failures);
        TEST_ASSERT_EQ_U16(vm.error_pc, 2, "error_pc is the faulting instruction's address",
                           &failures);

        VmResult r3 = vm_step(&vm);
        TEST_ASSERT(r3 == VM_ERR_INVALID_OPCODE,
                    "stepping again after the error returns the same error (no-op)", &failures);
        TEST_ASSERT_EQ_U16(vm.error_pc, 2, "error_pc does not change on the no-op step", &failures);
    }

    return failures;
}

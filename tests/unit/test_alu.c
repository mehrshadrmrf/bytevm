#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bytevm/vm.h"
#include "test_framework.h"

/* Writes one hand-encoded instruction at address 0 and executes exactly
 * it -- tests set up operand registers directly via vm.cpu.r[] first,
 * so each case here is a single, focused decode+execute rather than a
 * multi-instruction program (test_vm.c already covers full programs). */
static VmResult exec_one(VM *vm, const uint8_t *bytes, size_t len)
{
    size_t i;
    for (i = 0; i < len; i++) {
        mem_write8(&vm->memory, (uint16_t)i, bytes[i]);
    }
    vm->cpu.pc = 0;
    return vm_step(vm);
}

int test_alu(void)
{
    int failures = 0;
    VM vm;

    /* ---- MUL (DESIGN.md section 8.2) ---- */
    {
        vm_init(&vm);
        vm.cpu.r[0] = 3;
        vm.cpu.r[1] = 4;
        const uint8_t mul[] = {0x04, 0x11, 0x00, 0x01}; /* MUL R0, R1 */
        VmResult r = exec_one(&vm, mul, sizeof(mul));

        TEST_ASSERT(r == VM_OK, "MUL 3*4 succeeds", &failures);
        TEST_ASSERT_EQ_U16(vm.cpu.r[0], 12, "3 * 4 == 12", &failures);
        TEST_ASSERT(!cpu_get_flag(&vm.cpu, CPU_FLAG_C), "3*4: C clear (no truncation)", &failures);
        TEST_ASSERT(!cpu_get_flag(&vm.cpu, CPU_FLAG_O), "3*4: O clear (no signed overflow)",
                    &failures);
        TEST_ASSERT(!cpu_get_flag(&vm.cpu, CPU_FLAG_Z), "3*4: Z clear", &failures);
        TEST_ASSERT(!cpu_get_flag(&vm.cpu, CPU_FLAG_S), "3*4: S clear", &failures);
    }
    {
        /* Unsigned truncation without signed overflow: 0xFFFF * 0xFFFF
         * == 0xFFFE0001; low 16 bits are 0x0001, and as signed values
         * (-1)*(-1) == 1, which fits fine -- C set, O clear. */
        vm_init(&vm);
        vm.cpu.r[0] = 0xFFFF;
        vm.cpu.r[1] = 0xFFFF;
        const uint8_t mul[] = {0x04, 0x11, 0x00, 0x01};
        exec_one(&vm, mul, sizeof(mul));

        TEST_ASSERT_EQ_U16(vm.cpu.r[0], 1, "0xFFFF * 0xFFFF low 16 bits == 1", &failures);
        TEST_ASSERT(cpu_get_flag(&vm.cpu, CPU_FLAG_C), "0xFFFF*0xFFFF: C set (unsigned truncation)",
                    &failures);
        TEST_ASSERT(!cpu_get_flag(&vm.cpu, CPU_FLAG_O), "0xFFFF*0xFFFF: O clear (-1*-1 fits)",
                    &failures);
    }
    {
        /* Signed overflow without unsigned truncation: 0x7FFF * 2 ==
         * 65534, which fits in 16 unsigned bits but not 16 signed bits
         * -- C clear, O set. Proves C and O are genuinely independent
         * formulas, not the same check under two names. */
        vm_init(&vm);
        vm.cpu.r[0] = 0x7FFF;
        vm.cpu.r[1] = 2;
        const uint8_t mul[] = {0x04, 0x11, 0x00, 0x01};
        exec_one(&vm, mul, sizeof(mul));

        TEST_ASSERT_EQ_U16(vm.cpu.r[0], 0xFFFE, "0x7FFF * 2 == 0xFFFE", &failures);
        TEST_ASSERT(!cpu_get_flag(&vm.cpu, CPU_FLAG_C), "0x7FFF*2: C clear (fits 16 unsigned bits)",
                    &failures);
        TEST_ASSERT(cpu_get_flag(&vm.cpu, CPU_FLAG_O), "0x7FFF*2: O set (signed overflow)",
                    &failures);
    }

    /* ---- DIV / MOD (DESIGN.md section 8.2) ---- */
    {
        vm_init(&vm);
        vm.cpu.r[0] = 10;
        vm.cpu.r[1] = 3;
        const uint8_t div[] = {0x05, 0x11, 0x00, 0x01}; /* DIV R0, R1 */
        VmResult r = exec_one(&vm, div, sizeof(div));

        TEST_ASSERT(r == VM_OK, "DIV 10/3 succeeds", &failures);
        TEST_ASSERT_EQ_U16(vm.cpu.r[0], 3, "10 / 3 == 3", &failures);
    }
    {
        vm_init(&vm);
        vm.cpu.r[0] = 10;
        vm.cpu.r[1] = 3;
        const uint8_t mod[] = {0x06, 0x11, 0x00, 0x01}; /* MOD R0, R1 */
        exec_one(&vm, mod, sizeof(mod));

        TEST_ASSERT_EQ_U16(vm.cpu.r[0], 1, "10 % 3 == 1", &failures);
    }
    {
        vm_init(&vm);
        vm.cpu.r[0] = 10;
        vm.cpu.r[1] = 0;
        const uint8_t div[] = {0x05, 0x11, 0x00, 0x01};
        VmResult r = exec_one(&vm, div, sizeof(div));

        TEST_ASSERT(r == VM_ERR_DIV_BY_ZERO, "DIV by zero is reported", &failures);
        TEST_ASSERT(vm.halted, "the VM halts on division by zero", &failures);
    }
    {
        /* C and O are left unmodified by DIV (DESIGN.md section 8.2) --
         * set C via a prior SUB, then confirm DIV doesn't clear it. */
        vm_init(&vm);
        vm.cpu.r[0] = 0;
        vm.cpu.r[1] = 1;
        const uint8_t sub[] = {0x03, 0x11, 0x00, 0x01}; /* SUB R0, R1: 0 - 1 sets C */
        exec_one(&vm, sub, sizeof(sub));
        TEST_ASSERT(cpu_get_flag(&vm.cpu, CPU_FLAG_C), "setup: SUB sets C", &failures);

        vm.cpu.r[0] = 10;
        vm.cpu.r[1] = 3;
        const uint8_t div[] = {0x05, 0x11, 0x00, 0x01};
        exec_one(&vm, div, sizeof(div));
        TEST_ASSERT(cpu_get_flag(&vm.cpu, CPU_FLAG_C), "DIV leaves C unmodified", &failures);
    }

    /* ---- AND / OR / XOR / NOT (DESIGN.md section 8.3) ---- */
    {
        vm_init(&vm);
        vm.cpu.r[0] = 0xF0F0;
        vm.cpu.r[1] = 0x0FF0;
        const uint8_t and_[] = {0x07, 0x11, 0x00, 0x01}; /* AND R0, R1 */
        exec_one(&vm, and_, sizeof(and_));
        TEST_ASSERT_EQ_U16(vm.cpu.r[0], 0x00F0, "0xF0F0 AND 0x0FF0 == 0x00F0", &failures);
    }
    {
        vm_init(&vm);
        vm.cpu.r[0] = 0xF0F0;
        vm.cpu.r[1] = 0x0FF0;
        const uint8_t or_[] = {0x08, 0x11, 0x00, 0x01}; /* OR R0, R1 */
        exec_one(&vm, or_, sizeof(or_));
        TEST_ASSERT_EQ_U16(vm.cpu.r[0], 0xFFF0, "0xF0F0 OR 0x0FF0 == 0xFFF0", &failures);
    }
    {
        vm_init(&vm);
        vm.cpu.r[0] = 0xF0F0;
        vm.cpu.r[1] = 0x0FF0;
        const uint8_t xor_[] = {0x09, 0x11, 0x00, 0x01}; /* XOR R0, R1 */
        exec_one(&vm, xor_, sizeof(xor_));
        TEST_ASSERT_EQ_U16(vm.cpu.r[0], 0xFF00, "0xF0F0 XOR 0x0FF0 == 0xFF00", &failures);
    }
    {
        vm_init(&vm);
        vm.cpu.r[0] = 0x0000;
        const uint8_t not_[] = {0x0A, 0x10, 0x00}; /* NOT R0 */
        exec_one(&vm, not_, sizeof(not_));
        TEST_ASSERT_EQ_U16(vm.cpu.r[0], 0xFFFF, "NOT 0x0000 == 0xFFFF", &failures);
        TEST_ASSERT(cpu_get_flag(&vm.cpu, CPU_FLAG_S), "NOT 0x0000: S set", &failures);
        TEST_ASSERT(!cpu_get_flag(&vm.cpu, CPU_FLAG_Z), "NOT 0x0000: Z clear", &failures);
    }

    /* ---- SHL / SHR (DESIGN.md section 8.3's shift-count table) ---- */
    {
        vm_init(&vm);
        vm.cpu.r[0] = 0x1234;
        vm.cpu.r[1] = 0;                                /* n == 0 */
        const uint8_t shl[] = {0x0B, 0x11, 0x00, 0x01}; /* SHL R0, R1 */
        exec_one(&vm, shl, sizeof(shl));
        TEST_ASSERT_EQ_U16(vm.cpu.r[0], 0x1234, "SHL by 0 leaves the value unchanged", &failures);
        TEST_ASSERT(!cpu_get_flag(&vm.cpu, CPU_FLAG_C), "SHL by 0: C clear", &failures);
    }
    {
        vm_init(&vm);
        vm.cpu.r[0] = 0x8001;
        vm.cpu.r[1] = 1;
        const uint8_t shl[] = {0x0B, 0x11, 0x00, 0x01};
        exec_one(&vm, shl, sizeof(shl));
        TEST_ASSERT_EQ_U16(vm.cpu.r[0], 0x0002, "0x8001 SHL 1 == 0x0002", &failures);
        TEST_ASSERT(cpu_get_flag(&vm.cpu, CPU_FLAG_C), "0x8001 SHL 1: C set (bit 15 shifted out)",
                    &failures);
    }
    {
        vm_init(&vm);
        vm.cpu.r[0] = 0x0001;
        vm.cpu.r[1] = 16;
        const uint8_t shl[] = {0x0B, 0x11, 0x00, 0x01};
        exec_one(&vm, shl, sizeof(shl));
        TEST_ASSERT_EQ_U16(vm.cpu.r[0], 0, "SHL by 16 zeroes the register", &failures);
        TEST_ASSERT(cpu_get_flag(&vm.cpu, CPU_FLAG_C), "0x0001 SHL 16: C set (bit 0 was last out)",
                    &failures);
    }
    {
        vm_init(&vm);
        vm.cpu.r[0] = 0xFFFF;
        vm.cpu.r[1] = 17; /* n > 16 */
        const uint8_t shl[] = {0x0B, 0x11, 0x00, 0x01};
        exec_one(&vm, shl, sizeof(shl));
        TEST_ASSERT_EQ_U16(vm.cpu.r[0], 0, "SHL by 17 (n > 16) zeroes the register", &failures);
        TEST_ASSERT(!cpu_get_flag(&vm.cpu, CPU_FLAG_C), "SHL by 17: C clear", &failures);
    }
    {
        vm_init(&vm);
        vm.cpu.r[0] = 0x0003;
        vm.cpu.r[1] = 1;
        const uint8_t shr[] = {0x0C, 0x11, 0x00, 0x01}; /* SHR R0, R1 */
        exec_one(&vm, shr, sizeof(shr));
        TEST_ASSERT_EQ_U16(vm.cpu.r[0], 0x0001, "0x0003 SHR 1 == 0x0001", &failures);
        TEST_ASSERT(cpu_get_flag(&vm.cpu, CPU_FLAG_C), "0x0003 SHR 1: C set (bit 0 shifted out)",
                    &failures);
    }
    {
        vm_init(&vm);
        vm.cpu.r[0] = 0x8000;
        vm.cpu.r[1] = 16;
        const uint8_t shr[] = {0x0C, 0x11, 0x00, 0x01};
        exec_one(&vm, shr, sizeof(shr));
        TEST_ASSERT_EQ_U16(vm.cpu.r[0], 0, "SHR by 16 zeroes the register", &failures);
        TEST_ASSERT(cpu_get_flag(&vm.cpu, CPU_FLAG_C), "0x8000 SHR 16: C set (bit 15 was last out)",
                    &failures);
    }

    /* ---- CMP (DESIGN.md section 8.4) ---- */
    {
        vm_init(&vm);
        vm.cpu.r[0] = 5;
        vm.cpu.r[1] = 5;
        const uint8_t cmp[] = {0x11, 0x11, 0x00, 0x01}; /* CMP R0, R1 */
        exec_one(&vm, cmp, sizeof(cmp));
        TEST_ASSERT(cpu_get_flag(&vm.cpu, CPU_FLAG_Z), "CMP 5,5: Z set", &failures);
        TEST_ASSERT_EQ_U16(vm.cpu.r[0], 5, "CMP does not modify its operands", &failures);
    }
    {
        /* Signed vs. unsigned disagree here on purpose: 1 vs 0xFFFF
         * (== -1 signed). Unsigned: 1 < 65535 (a JB would fire). Signed:
         * 1 > -1 (a JG would fire). This is exactly why both flag
         * families exist (DESIGN.md section 5). */
        vm_init(&vm);
        vm.cpu.r[0] = 1;
        vm.cpu.r[1] = 0xFFFF;
        const uint8_t cmp[] = {0x11, 0x11, 0x00, 0x01};
        exec_one(&vm, cmp, sizeof(cmp));

        TEST_ASSERT(cpu_get_flag(&vm.cpu, CPU_FLAG_C), "CMP 1,0xFFFF: C set (1 < 0xFFFF unsigned)",
                    &failures);
        bool s = cpu_get_flag(&vm.cpu, CPU_FLAG_S);
        bool o = cpu_get_flag(&vm.cpu, CPU_FLAG_O);
        TEST_ASSERT(s == o, "CMP 1,0xFFFF: S == O (1 > -1 signed)", &failures);
    }

    return failures;
}

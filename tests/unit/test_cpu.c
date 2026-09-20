#include "bytevm/cpu.h"
#include "test_framework.h"

int test_cpu(void)
{
    int failures = 0;
    CPU cpu;
    int i;

    cpu_init(&cpu);

    for (i = 0; i < 8; i++) {
        TEST_ASSERT_EQ_U16(cpu.r[i], 0, "cpu_init zeroes R0-R7", &failures);
    }
    TEST_ASSERT_EQ_U16(cpu.pc, 0x0000, "cpu_init sets PC to 0", &failures);
    TEST_ASSERT_EQ_U16(cpu.sp, CPU_INITIAL_SP,
                       "cpu_init sets SP to 0xFFFE (not 0xFFFF -- see DESIGN.md section 6)",
                       &failures);
    TEST_ASSERT_EQ_U16(cpu.flags, 0, "cpu_init sets FLAGS to 0", &failures);

    /* Flag bit accessors are independent of one another. */
    cpu_set_flag(&cpu, CPU_FLAG_Z, true);
    TEST_ASSERT(cpu_get_flag(&cpu, CPU_FLAG_Z) == true,
                "cpu_set_flag(Z, true) is observed by cpu_get_flag", &failures);
    TEST_ASSERT(cpu_get_flag(&cpu, CPU_FLAG_C) == false, "setting Z does not affect C", &failures);

    cpu_set_flag(&cpu, CPU_FLAG_Z, false);
    TEST_ASSERT(cpu_get_flag(&cpu, CPU_FLAG_Z) == false, "cpu_set_flag(Z, false) clears it again",
                &failures);

    cpu_set_flag(&cpu, CPU_FLAG_C, true);
    cpu_set_flag(&cpu, CPU_FLAG_S, true);
    cpu_set_flag(&cpu, CPU_FLAG_O, true);
    TEST_ASSERT_EQ_U16(cpu.flags, 0x000E, "setting C, S, O independently produces FLAGS = 0x000E",
                       &failures);

    return failures;
}

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bytevm/vm.h"
#include "test_framework.h"

static VmResult exec_at(VM *vm, uint16_t addr, const uint8_t *bytes, size_t len)
{
    size_t i;
    for (i = 0; i < len; i++) {
        mem_write8(&vm->memory, (uint16_t)(addr + i), bytes[i]);
    }
    vm->cpu.pc = addr;
    return vm_step(vm);
}

int test_functions(void)
{
    int failures = 0;
    VM vm;

    /* ---- CALL / RET (DESIGN.md section 8.6) ---- */
    {
        vm_init(&vm);
        uint16_t sp_before = vm.cpu.sp;
        const uint8_t call[] = {0x1D, 0x30, 0x00, 0x20}; /* CALL 0x2000, at 0x0000 */
        VmResult r1 = exec_at(&vm, 0x0000, call, sizeof(call));

        TEST_ASSERT(r1 == VM_OK, "CALL succeeds", &failures);
        TEST_ASSERT_EQ_U16(vm.cpu.pc, 0x2000, "CALL jumps to its target", &failures);
        TEST_ASSERT_EQ_U16(vm.cpu.sp, (uint16_t)(sp_before - 2), "CALL decrements SP by 2",
                           &failures);
        TEST_ASSERT_EQ_U16(mem_read16(&vm.memory, sp_before), 0x0004,
                           "CALL pushes the address right after itself (section 2.1)", &failures);

        const uint8_t ret[] = {0x1E, 0x00}; /* RET, placed at the call target */
        VmResult r2 = exec_at(&vm, 0x2000, ret, sizeof(ret));

        TEST_ASSERT(r2 == VM_OK, "RET succeeds", &failures);
        TEST_ASSERT_EQ_U16(vm.cpu.pc, 0x0004, "RET returns to the address CALL pushed", &failures);
        TEST_ASSERT_EQ_U16(vm.cpu.sp, sp_before, "RET restores SP", &failures);
    }
    {
        /* CALL mirrors PUSH's overflow rule exactly. */
        vm_init(&vm);
        vm.stack_limit = (uint16_t)(CPU_INITIAL_SP - 1);
        uint16_t sp_before = vm.cpu.sp;
        const uint8_t call[] = {0x1D, 0x30, 0x00, 0x20};
        VmResult r = exec_at(&vm, 0x0000, call, sizeof(call));

        TEST_ASSERT(r == VM_ERR_STACK_OVERFLOW, "CALL reports overflow against stack_limit",
                    &failures);
        TEST_ASSERT_EQ_U16(vm.cpu.sp, sp_before, "SP is untouched when CALL overflows", &failures);
    }
    {
        /* RET mirrors POP's underflow rule exactly. */
        vm_init(&vm);
        const uint8_t ret[] = {0x1E, 0x00};
        VmResult r = exec_at(&vm, 0x0000, ret, sizeof(ret));

        TEST_ASSERT(r == VM_ERR_STACK_UNDERFLOW, "RET on an empty stack reports underflow",
                    &failures);
    }

    /* ---- SYS (DESIGN.md section 8.7) ---- */
    {
        /* PRINT_INT: prints R0 as a signed decimal. */
        vm_init(&vm);
        vm.cpu.r[0] = (uint16_t)-5; /* 0xFFFB, i.e. -5 as int16_t */

        FILE *tmp = tmpfile();
        TEST_ASSERT(tmp != NULL, "tmpfile() for stdout capture succeeds", &failures);
        if (tmp != NULL) {
            FILE *saved_stdout = stdout;
            stdout = tmp;
            const uint8_t sys[] = {0x1F, 0x50, 0x01}; /* SYS PRINT_INT */
            exec_at(&vm, 0x0000, sys, sizeof(sys));
            fflush(tmp);
            stdout = saved_stdout;

            char buf[32] = {0};
            rewind(tmp);
            fread(buf, 1, sizeof(buf) - 1, tmp);
            fclose(tmp);
            TEST_ASSERT(strcmp(buf, "-5") == 0, "PRINT_INT prints '-5' for R0 = -5", &failures);
        }
    }
    {
        /* PRINT_CHAR: prints the low byte of R0 as ASCII. */
        vm_init(&vm);
        vm.cpu.r[0] = 0x0041; /* 'A' */

        FILE *tmp = tmpfile();
        if (tmp != NULL) {
            FILE *saved_stdout = stdout;
            stdout = tmp;
            const uint8_t sys[] = {0x1F, 0x50, 0x02}; /* SYS PRINT_CHAR */
            exec_at(&vm, 0x0000, sys, sizeof(sys));
            fflush(tmp);
            stdout = saved_stdout;

            char buf[8] = {0};
            rewind(tmp);
            fread(buf, 1, sizeof(buf) - 1, tmp);
            fclose(tmp);
            TEST_ASSERT(strcmp(buf, "A") == 0, "PRINT_CHAR prints 'A' for R0 = 0x0041", &failures);
        }
    }
    {
        /* READ_INT: parses a signed int, leaves the trailing non-digit
         * on the stream unconsumed (DESIGN.md section 8.7). */
        vm_init(&vm);

        FILE *tmp = tmpfile();
        if (tmp != NULL) {
            fputs("  -123abc", tmp);
            rewind(tmp);
            FILE *saved_stdin = stdin;
            stdin = tmp;
            const uint8_t sys[] = {0x1F, 0x50, 0x03}; /* SYS READ_INT */
            exec_at(&vm, 0x0000, sys, sizeof(sys));

            TEST_ASSERT_EQ_U16(vm.cpu.r[0], (uint16_t)-123, "READ_INT parses '  -123' as -123",
                               &failures);
            int next = fgetc(tmp);
            TEST_ASSERT(next == 'a', "READ_INT leaves the first non-digit ('a') unconsumed",
                        &failures);
            stdin = saved_stdin;
            fclose(tmp);
        }
    }
    {
        /* READ_INT with no digits at all: R0 = 0, no error, no halt. */
        vm_init(&vm);

        FILE *tmp = tmpfile();
        if (tmp != NULL) {
            fputs("xyz", tmp);
            rewind(tmp);
            FILE *saved_stdin = stdin;
            stdin = tmp;
            const uint8_t sys[] = {0x1F, 0x50, 0x03};
            VmResult r = exec_at(&vm, 0x0000, sys, sizeof(sys));

            TEST_ASSERT(r == VM_OK, "READ_INT with no digits does not error", &failures);
            TEST_ASSERT_EQ_U16(vm.cpu.r[0], 0, "READ_INT with no digits sets R0 = 0", &failures);
            stdin = saved_stdin;
            fclose(tmp);
        }
    }
    {
        /* EXIT: halts normally; R0 holds the VM-level exit code, and the
         * host mapping (R0 & 0xFF) is the caller's job, not the VM's. */
        vm_init(&vm);
        vm.cpu.r[0] = 0x01FF;
        const uint8_t sys[] = {0x1F, 0x50, 0x04}; /* SYS EXIT */
        VmResult r = exec_at(&vm, 0x0000, sys, sizeof(sys));

        TEST_ASSERT(r == VM_OK, "EXIT is not itself an error", &failures);
        TEST_ASSERT(vm.halted, "EXIT halts the VM", &failures);
        TEST_ASSERT(vm.last_error == VM_OK, "last_error is VM_OK after EXIT", &failures);
        TEST_ASSERT_EQ_U16((uint16_t)(vm.cpu.r[0] & 0xFFu), 0xFF,
                           "host exit code would be R0 & 0xFF == 0xFF", &failures);
    }
    {
        /* Unrecognized syscall code. */
        vm_init(&vm);
        const uint8_t sys[] = {0x1F, 0x50, 0x99};
        VmResult r = exec_at(&vm, 0x0000, sys, sizeof(sys));

        TEST_ASSERT(r == VM_ERR_INVALID_SYSCALL, "an unknown SYS code is reported", &failures);
        TEST_ASSERT(vm.halted, "the VM halts on an invalid syscall", &failures);
    }

    return failures;
}

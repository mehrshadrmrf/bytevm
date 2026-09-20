#include <stdio.h>

/* Each test_<module>() lives in tests/unit/test_<module>.c and returns its
 * own failure count. This file is the only one allowed to define main()
 * (DESIGN.md section 14) -- add a forward declaration and a call below for
 * every new test module as phases land. */
int test_memory(void);
int test_cpu(void);
int test_instruction(void);
int test_vm(void);
int test_alu(void);
int test_control(void);
int test_functions(void);
int test_loader(void);
int test_assembler(void);
int test_disassembler(void);
int test_debugger(void);
int test_golden(void);
int test_fuzz(void);

static int run(const char *name, int (*test_fn)(void))
{
    int failures;
    printf("== %s ==\n", name);
    failures = test_fn();
    printf("  %d failure(s)\n", failures);
    return failures;
}

int main(void)
{
    int total = 0;

    total += run("test_memory", test_memory);
    total += run("test_cpu", test_cpu);
    total += run("test_instruction", test_instruction);
    total += run("test_vm", test_vm);
    total += run("test_alu", test_alu);
    total += run("test_control", test_control);
    total += run("test_functions", test_functions);
    total += run("test_loader", test_loader);
    total += run("test_assembler", test_assembler);
    total += run("test_disassembler", test_disassembler);
    total += run("test_debugger", test_debugger);
    total += run("test_golden", test_golden);
    total += run("test_fuzz", test_fuzz);

    if (total == 0) {
        printf("\nAll tests passed.\n");
    } else {
        printf("\n%d total failure(s).\n", total);
    }

    return total == 0 ? 0 : 1;
}
